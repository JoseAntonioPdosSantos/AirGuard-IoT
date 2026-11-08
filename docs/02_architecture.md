# 02 — Arquitetura do firmware

## 1. Princípios

- **Execução cooperativa não bloqueante.** Um único `loop()` chama tarefas curtas. Nenhuma
  tarefa espera rádio, ACK ou display. Não há `delay()` no caminho normal; a única espera
  é de 50 ms no comando `reboot` do console, para o log sair antes de reiniciar.
- **Separação estrita de responsabilidades.** Leitura de sensor, decisão de alerta e
  transmissão LoRa ficam em módulos diferentes, e os dados fluem num só sentido.
- **Núcleo independente de plataforma.** Toda a lógica de decisão (classificação, histerese,
  alertas, protocolo, fila de ACK, telas, padrões sonoros, texto) não inclui `Arduino.h`. Ela
  é compilada e testada no PC (`make -C tests`).
- **Configuração centralizada.** Pinos, limites, tempos, rádio e políticas ficam apenas em
  `src/config/`.
- **Falha segura.** Uma leitura inválida nunca é tratada como "normal". As saídas começam
  desligadas e o watchdog reinicia o sistema se o loop travar.

## 2. Módulos

```
src/
├── main.cpp                      setup()/loop() → AirGuardApp
├── app/
│   ├── airguard_app.h/.cpp       orquestração das tarefas (única camada que "conhece" todos)
│   └── console_commands.cpp      console serial: calibração, diagnóstico, simulação
├── config/
│   ├── config.h                  identidade, tempos, rádio, políticas (relé, buzzer, ACK)
│   ├── thresholds.h              limites ambientais + constantes de calibração do MQ-135
│   └── hardware_pins.h           mapa de GPIO
├── sensors/                      [HW] acesso cru ao hardware
│   ├── mq135_sensor              ADC com sobreamostragem
│   └── temperature_sensor        DHT11 (padrão), DHT22 ou AHT20 (seleção por build flag)
├── monitoring/                   [PURO] decisão
│   ├── air_quality_monitor       ADC→V→Rs→mediana+EMA→Rs/R0→air_index, aquecimento, calibração
│   ├── environment_monitor       validação T/UR, detecção de falha, índice de calor
│   ├── severity_logic            classificador com histerese + rastreador de persistência
│   ├── alert_manager             nível confirmado por categoria, eventos, lembretes, relé
│   └── alert_types.h             enums e estruturas compartilhadas
├── communication/
│   ├── message_protocol          [PURO] JSON compacto: codificador limitado + parser seguro
│   ├── ack_manager               [PURO] fila com prioridade, ACK, timeout, retransmissão, dedup
│   ├── rylr998_codec             [PURO] AT+SEND, parse de +RCV (por comprimento), tempo no ar
│   └── lora_manager              [HW] driver não bloqueante do RYLR998 (UART, comandos AT)
├── display/
│   ├── screen_logic              [PURO] prioridade visual, mensagens remotas (3 s/página)
│   └── oled_manager              [HW] desenho SSD1306 (envia só quando o quadro muda)
├── actuators/
│   ├── buzzer_pattern            [PURO] padrão sonoro em função do tempo
│   ├── buzzer_manager            [HW] máquina de estados do buzzer
│   ├── relay_manager             [HW] relé com estado seguro e polaridade configurável
│   └── led_manager               [HW] LEDs vermelho e amarelo (o verde é ligado pelo contato NC do relé)
└── utils/
    ├── logger                    [HW] log serial com nível, sem bloqueio
    ├── persistent_store          [HW] flash (EEPROM emulada): boot count, R0, máscara HIGH
    └── text_utils                [PURO] UTF-8 → ASCII (acentos PT-BR), quebra de linha
```

`[PURO]` = sem dependência de hardware, coberto por testes unitários.

## 3. Fluxo de dados

```
 MQ-135 ─► Mq135Sensor ─► AirQualityMonitor ─┐
                                             ├─► AlertManager ─► AlertEvent ─► message_protocol ─► AckManager ─► LoraManager ─► ar
 DHT22 ──► TemperatureSensor ─► EnvironmentMonitor ┘      │                                          ▲   │
                                                          │                                          │   ▼
                         estado confirmado por categoria  │                    ACK ◄── parser ◄──── RX LoRa
                                                          ▼                                  │
                              Buzzer / Relé / LED / OLED ◄─┘                    TEXT ─► RemoteMessageDisplay ─► OLED
                                                                                   └─► ACK de volta ao remetente
```

## 4. Laço principal (`AirGuardApp::loop`)

| Ordem | Tarefa | Período | Custo máximo |
|---|---|---|---|
| 0 | `rp2040.wdt_reset()` | toda iteração | — |
| 1 | Console serial | toda iteração (≤ 64 caracteres) | µs |
| 2 | Sensores: MQ-135 | 1 s | ~30 µs (16 conversões ADC) |
| 2 | Sensores: DHT11 / DHT22 / AHT20 | 2,5 s | ~25 ms (DHTxx: 20 ms de sinal de início + leitura) / ~80 ms (AHT20) |
| 3 | Alertas (classificação, eventos, flags de sistema) | toda iteração | µs |
| 4 | Rádio (UART, RX, TX, fila, retransmissão) | toda iteração | ~22 ms para escrever um `AT+SEND` de 240 B a 115200 baud |
| 5 | Saídas (buzzer, relé, LED) | toda iteração | µs |
| 6 | Display | 250 ms | ~23 ms **só quando o quadro mudou** |
| 7 | Log de status | 10 s | < 1 ms (descarta a linha se a USB estiver cheia) |

O watchdog é de 8 s, e o pior caso de uma iteração fica abaixo de 150 ms.

## 5. Máquinas de estado

### 5.1 Severidade por categoria (`HysteresisClassifier` + `SeverityTracker`)

```
 valor ──► classificador com histerese de VALOR ──► candidato ──► persistência TEMPORAL ──► confirmado
           (sai do nível só ao recuar `hyst`)       N/L/M/H        sobe: candidato ≥ L por escalate_ms
                                                                   desce: candidato < L por recover_ms
```

| Categoria | Subida (escalate) | Recuperação (recover) | Histerese de valor |
|---|---|---|---|
| AIR_QUALITY | 5 s | 10 s | 5 pontos de índice (modo tensão: 0,3 V) |
| TEMPERATURE | 10 s | 60 s | 1,0 °C |
| HUMIDITY | 30 s | 120 s | 3 %UR |
| SYSTEM | imediato (flags) | imediato | — |

A subida de MEDIUM para HIGH é confirmada após o tempo de subida e gera um evento **com
prioridade máxima**. Esse evento substitui na fila a mensagem MEDIUM ainda não confirmada da
mesma categoria e é transmitido logo após o intervalo mínimo entre transmissões.

### 5.2 Rádio (`LoraManager`, RYLR998)

```
 Uninit ─begin─► Init: NRST baixo 100 ms ─► espera +READY (≤ 3 s) ─► AT, MODE, ADDRESS,
                       NETWORKID, BAND, PARAMETER, CRFOP, [CPIN], um por vez, cada um com +OK
   Init ─todos +OK─► Rx ◄──────── +OK e tempo no ar decorrido ────────┐
                      │ startTransmit (AT+SEND)                        │
                      └──────────────────────────────► Tx ─────────────┘
   timeout / +ERR na configuração / AT de saúde sem resposta ─► Fault ─ a cada 30 s ─► Init
   +RCV=... chega a qualquer momento (parse por comprimento: o JSON tem vírgulas)
```

### 5.3 Entrega (`AckManager`, por mensagem)

```
 Free ─enqueue─► Ready ─nextToTransmit─► Sending ─TxDone─► WaitAck ─ACK─► Free (entregue)
                   ▲                       │ busy/erro          │
                   └──── backoff ──────────┘                    └─ 60 s sem ACK ─► Ready (retransmite)
                                                                  └─ tentativas > 1+5 ─► Free (FALHA registrada)
```

### 5.4 Buzzer e display

- O buzzer é uma função pura `patternOutput(padrão, t − início)`. Uma mudança de severidade
  reinicia o padrão.
- O display segue a prioridade visual HIGH > MEDIUM > LOW > mensagem remota > tela principal.
  As exceções estão em `docs/06_alerts.md`.

## 6. Persistência e reinicialização

Os dados ficam em flash (EEPROM emulada, 256 bytes, CRC32):

- **contador de boot**, que forma `event_id = (boot << 16) | sequência`. Assim os IDs não se
  repetem entre reinicializações e o gateway deduplica com segurança.
- **R0** calibrado.
- **máscara HIGH**: as categorias que estavam em HIGH. A flash só é gravada ao entrar ou sair
  de HIGH, para limitar o desgaste.

Se o sistema reiniciar durante uma condição crítica (`RESTORE_HIGH_AFTER_REBOOT = true`), as
categorias HIGH voltam ativas, com relé e buzzer ligados. Um ALERT com `"note":"RESTORED"` é
enviado ao gateway. O estado só se normaliza depois que leituras novas e válidas provarem a
recuperação, incluindo o aquecimento do MQ-135.

## 7. Bibliotecas e justificativa

| Biblioteca | Uso | Por que esta |
|---|---|---|
| **Arduino-Pico (Earle Philhower)** | core RP2040 | Suporte completo ao RP2040: EEPROM emulada, `rp2040.wdt_*`, `hwrand32`, mux de pinos SPI/I2C. É mantido ativamente. O core Mbed oficial não tem watchdog nem EEPROM com essa API. |
| *(nenhuma para o LoRa)* | RYLR998 | O módulo é controlado por comandos AT simples. Um driver próprio, não bloqueante e com o parser testado no PC (`rylr998_codec`), evita depender de bibliotecas de terceiros que costumam usar `delay()` e esperar respostas de forma bloqueante. |
| **Adafruit SSD1306 + GFX** | OLED | Padrão de mercado, fonte 5×7, buffer acessível (necessário para enviar só quando muda). |
| *(nenhuma para o DHT)* | DHT11 / DHT22 | Driver próprio (`temperature_sensor.cpp` + `dht_codec`, testado no PC). A biblioteca da Adafruit foi removida porque, com a linha de dados instável, deixa as interrupções desligadas por segundos; no ESP32 isso dispara o watchdog de interrupção (reboot em loop observado na bancada). Cada borda agora tem timeout de 100 µs: no máximo ~8,5 ms sem interrupções. |
| **Adafruit AHTX0** | AHT20 (opcional) | Driver oficial do fabricante do módulo mais comum. |

Não é usada nenhuma biblioteca JSON: o parser próprio é limitado, não aloca memória, rejeita
aninhamento e é testado contra entradas malformadas. ArduinoJson funcionaria, mas aceitaria
mais do que o protocolo precisa.

## 8. Extensibilidade

- **Segundo dispositivo / gateway WhatsApp:** basta seguir `docs/03_lora_protocol.md`. O nó
  aceita `TEXT` de remetentes da lista `cfg::ALLOWED_TEXT_SOURCES` e confirma com `ACK`.
- **Novo sensor:** criar o driver em `sensors/`, a lógica pura em `monitoring/` e uma nova
  `Category` com um `ThresholdSet`. O AlertManager trata a categoria como as demais.
- **Novo tipo de mensagem:** acrescentar o tipo ao parser (o conjunto de campos é fechado) e
  testes em `tests/unit/test_protocol.cpp`.
