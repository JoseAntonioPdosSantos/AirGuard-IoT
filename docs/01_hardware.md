# 01 — Hardware: componentes, ligações e análise elétrica

> Placa: **ESP32 DevKit com módulo ESP32-WROOM-32** (DOIT DevKit V1 de 30 pinos ou DevKitC de
> 38 pinos, `board = esp32dev`). Os GPIO escolhidos existem nas duas versões. Se a sua placa for
> **ESP32-S2, S3, C3 ou C6**, o mapa de pinos **não serve**: confira antes de ligar.
> A versão original do nó, para a Waveshare RP2040-Zero, está na branch `main`.
>
> Itens marcados **[MEDIR]** ou **[PENDENTE]** precisam ser conferidos na bancada.

## 1. Lista de componentes

| # | Componente | Modelo | Alimentação | Lógica | Observações |
|---|---|---|---|---|---|
| 1 | MCU | **ESP32 DevKit** (ESP32-WROOM-32) | 5 V (USB ou VIN) → regulador 3,3 V | **3,3 V, GPIOs NÃO tolerantes a 5 V** | console USB pelo CP2102/CH340 (UART0) |
| 2 | Sensor de gás | Módulo **MQ-135** (placa com LM393) | **5 V** (aquecedor) | AO analógica 0…~5 V | Aquecedor ≈ 29–31 Ω → **~160 mA / ~0,8 W** |
| 3 | Temperatura/umidade | Módulo **DHT11** (3 pinos: S, VCC, GND) | 3,3 V | 3,3 V | 0–50 °C ±2 °C; 20–90 %UR ±5 %; resolução de 1 °C / 1 % |
| 4 | Rádio LoRa | **REYAX RYLR998** (SX1262 interno, comandos AT por UART) | 3,3 V | 3,3 V (UART 115200) | Payload máx. **240 bytes**. **Nunca transmitir sem antena** |
| 5 | Display | OLED **0,96" SSD1306 128×64 I2C, amarelo/azul** | **3,3 V** | 3,3 V | Endereço 0x3C (impresso "0x78") |
| 6 | Buzzer | **ativo** (oscilador interno) | via GPIO + 1 kΩ | 3,3 V | Ver §4.3 |
| 7 | Relé | Módulo relé 1 canal de **3 V**, **entrada de nível alto** (bancada: com `RELAY_ACTIVE_HIGH = false` o LED verde ficava invertido). VCC no **3V3** | **ver §4.4** | IN | Contato NC usado para o LED verde |
| 8 | LED vermelho | LED + 1 kΩ | GPIO | 3,3 V | ~1,3 mA |
| 9 | LED amarelo | LED + 1 kΩ | GPIO | 3,3 V | ~1,2 mA |
| 10 | LED verde | LED + 1 kΩ | 3V3 via contato NC do relé | — | Aceso enquanto o relé está desligado |
| 10a | LED azul (mensagem do WhatsApp) | LED + **230 Ω** | GPIO | 3,3 V | ~1 mA (estimado: o LED azul derruba ~3 V; não medido). Pisca quando chega uma mensagem do WhatsApp (doc 12) |
| 11 | Resistores | 3× 10 kΩ (divisor MQ-135); 4× 1 kΩ; 1× 230 Ω (LED azul) | — | — | 1 % recomendado no divisor |
| 12 | Fonte | 5 V / ≥ 2 A regulada | — | — | Ver §5 |

## 2. Ligações (`src/config/hardware_pins.h`)

Folha para imprimir (A4, 3 folhas: diagrama e lista do sensor, diagrama e lista do receptor):
[`pinagem/AirGuard-pinagem.pdf`](pinagem/AirGuard-pinagem.pdf). Ela é gerada por
`tools/pinagem.py`, que lê os números dos pinos deste arquivo de configuração e do
`receiver/src/rx_config.h`. Depois de mudar um pino, gere de novo:

```
python3 tools/pinagem.py
google-chrome --headless --no-pdf-header-footer \
  --print-to-pdf=docs/pinagem/AirGuard-pinagem.pdf docs/pinagem/pinagem.html
```

| Componente | Pino do módulo | ESP32 DevKit | Observação |
|---|---|---|---|
| **RYLR998** | VDD | **3V3** | só 3,3 V |
| | GND | GND | |
| | TXD | **GPIO16** (RX2) | |
| | RXD | **GPIO17** (TX2) | |
| | NRST | **GPIO4** | ativo em LOW; o firmware pulsa 100 ms em LOW a cada inicialização |
| **OLED SSD1306** | VCC | **3V3** | não use 5 V: os pull-ups do módulo levariam 5 V aos GPIO |
| | GND | GND | |
| | SDA | **GPIO21** | |
| | SCL | **GPIO22** | |
| **DHT11 (módulo)** | VCC | **3V3** | idem: o pull-up do módulo vai para o VCC |
| | GND | GND | |
| | DATA (S) | **GPIO27** | |
| **MQ-135 (módulo)** | VCC | **VIN (5 V)** | aquecedor ~160 mA, precisa de 5 V |
| | GND | GND | |
| | AO | 10 kΩ → **GPIO34** → 20 kΩ (2× 10 kΩ em série) → GND | **divisor obrigatório**: AO chega a 5 V (§4.2) |
| | DO | **não ligar** | saída do comparador, 5 V |
| **Buzzer** | + | 1 kΩ → **GPIO25** | (−) no GND |
| **LED vermelho** | ânodo | 1 kΩ → **GPIO26** | cátodo no GND |
| **LED amarelo** | ânodo | 1 kΩ → **GPIO33** | cátodo no GND |
| **LED azul** (mensagem) | ânodo (perna longa) | **230 Ω** → **GPIO32** | cátodo (perna curta) no GND |
| **Relé (módulo)** | IN | **GPIO23** | VCC do módulo conforme o modelo (§4.4) |
| **LED verde** | — | 3V3 → 1 kΩ → LED → contato **NC** do relé, COM → GND | sem GPIO (§4.5) |

**GND comum** entre todos os módulos.

### 2.1 Pinos evitados e por quê

| GPIO | Motivo |
|---|---|
| 6–11 | ligados à memória flash; usá-los trava o chip |
| 1, 3 | UART0 = console USB |
| 0, 2, 5, 12, 15 | pinos de *strapping*: um módulo puxando esses pinos no reset pode impedir o boot. O GPIO12 em HIGH muda a tensão da flash para 1,8 V |
| 14 | gera PWM durante o boot |
| 34–39 | só entrada e sem pull-up interno. Só o MQ-135 (analógico) usa um deles |
| ADC2 (0, 2, 4, 12–15, 25–27) | o ADC2 não funciona com Wi-Fi ligado, por isso o MQ-135 fica no ADC1 (GPIO34). Os pinos 4, 25, 26 e 27 são usados só como digitais, o que não tem problema |

## 3. Diagrama de referência

```
                         +5V (fonte 5 V / 2 A)
     ┌───────────────────────┬──────────────────────────────┐
     │                       │                              │
 MQ-135 VCC        (relé VCC: ver §4.4)              ESP32 VIN
 MQ-135 AO ──[10k]──┬── GPIO34                     ┌────────────────┐
                 [10k]                             │  ESP32 DevKit  │
                 [10k]   (série)                   │                │
                   GND                      GPIO17 ┤ TX2 ──► RXD    │
                                            GPIO16 ┤ RX2 ◄── TXD  RYLR998 (3V3, antena!)
   3V3 ─┬─ OLED VCC     GPIO21 ── OLED SDA  GPIO4  ┤ ──► NRST       │
        ├─ DHT11 VCC    GPIO22 ── OLED SCL         │                │
        ├─ RYLR998 VDD  GPIO27 ── DHT11 S          │                │
        │               GPIO25 ─[1k]── Buzzer(+)  Buzzer(−) ── GND  │
        │               GPIO26 ─[1k]──►|── GND (LED vermelho)       │
        │               GPIO33 ─[1k]──►|── GND (LED amarelo)        │
        │               GPIO32 ─[230]─►|── GND (LED azul: mensagem) │
        │               GPIO23 ──────── Relé IN                     │
        └──[1k]──►|── Relé NC      Relé COM ── GND   (LED verde)    │
                                                   └────────────────┘
  GND ═══════════════ GND comum a todos os módulos ═══════════════════
```

## 4. Análise elétrica

### 4.1 MQ-135: aquecedor

- Resistência do aquecedor: 29 ± 3 Ω (Hanwei) / 30 ± 3 Ω (Winsen). A 5 V: **I ≈ 167 mA, P ≈ 0,83 W**.
- VCC no barramento **5 V** (VIN do DevKit).
- O sensor esquenta. Mantenha o DHT11 a pelo menos 5 cm dele.

### 4.2 MQ-135: divisor de tensão e ADC do ESP32

```
AO ──[ 10 kΩ ]──┬──► GPIO34 (ADC1_CH6)
                │
             [ 10 kΩ ]
                │            (2× 10 kΩ em SÉRIE = 20 kΩ)
             [ 10 kΩ ]
                │
GND ────────────┘
```

- Com AO = 5 V, o pino recebe 5 × 20 / 30 = **3,33 V**. O ADC do ESP32 (atenuação de 11 dB) mede
  até ~3,1 V, então o pino satura com AO ≈ 4,7 V. Isso só acontece com gás forte, e a saturação é
  tratada como **valor alto**, não como falha.
- O firmware lê `analogReadMilliVolts` (calibração de fábrica do eFuse), porque o ADC bruto do
  ESP32 não é linear, e converte para contagens de 12 bits referidas a 3,3 V
  (`board::adcRead12`).
- **Piso do ADC (bancada):** com o AO solto e com o pino sem sinal, o ADC devolveu sempre 176
  contagens (~0,14 V), não 0. Com o sensor ligado por horas, o AO em ar limpo caiu de ~0,45 V
  para menos de ~0,21 V, abaixo do que o ESP32 mede com este divisor. Nesse ponto, "ar muito
  limpo" e "fio do AO solto" dão a mesma leitura. Com `MQ135_FLOOR_IS_FAULT = false` (padrão
  atual de bancada), o piso é tratado como ar limpo: **um fio solto não é detectado**. Para uso
  real, volte a `true` (abaixo de `MQ135_ADC_MIN_VALID = 200`, ~0,16 V, o firmware acusa falha),
  ou meça o AO com mais resolução (divisor 10k/10k ou atenuação menor no ADC).
- **O valor configurado precisa corresponder à montagem** (`mq135::DIVIDER_R_BOTTOM` em
  `thresholds.h`). Com 2× 10 kΩ em paralelo seria 5 kΩ: o pino receberia no máximo 1,67 V,
  usando metade da escala.
- Um capacitor de 100 nF entre GPIO34 e GND (opcional) estabiliza a leitura.
- **Nunca** ligue o AO direto no GPIO.

### 4.3 Buzzer direto no GPIO com 1 kΩ

- A corrente fica limitada a **(3,3 V − queda no buzzer) / 1 kΩ ≈ 1–3 mA**. É seguro para o GPIO.
- Um buzzer **ativo** típico (com oscilador interno) é especificado para 3–5 V e ~20–30 mA.
  Com 1 kΩ em série, ele pode soar **fraco**. **[PENDENTE]** confirmar o volume na bancada.
- **Se o volume for insuficiente**, use um transistor NPN (BC547/2N2222): GPIO25 → 1 kΩ → base,
  emissor no GND, buzzer entre 5 V e o coletor, mais um diodo 1N4148 em antiparalelo se o
  buzzer for magnético. **Não remova o resistor ligando o buzzer direto no GPIO.**
- O buzzer instalado é **ativo** (`cfg::BUZZER_IS_PASSIVE = false`): o firmware só coloca o pino
  em HIGH/LOW. Se um dia voltar a usar um passivo, defina `true` (usa `tone()`).

### 4.4 Relé

- A maioria dos módulos de relé de 1 canal usa bobina de **5 V** (ex.: SRD-05VDC). Esse tipo de
  relé precisa de cerca de 75 % da tensão nominal (~3,75 V) para fechar com segurança. Com
  **3,3 V ele pode não acionar ou ficar "batendo"**, e a bobina puxa ~70–90 mA do regulador 3,3 V.
- **Se o módulo for de 5 V:** ligue o **VCC do módulo ao 5 V**. A entrada IN pode então ser de
  dois tipos:
  - **Ativo-alto com transistor, sem optoacoplador** (comum em módulos "1 canal" simples):
    3,3 V no IN costuma ser suficiente. Use `RELAY_ACTIVE_HIGH = true`.
  - **Ativo-baixo com optoacoplador e IN puxado para 5 V:** com o GPIO em ALTO (3,3 V) ainda
    sobra ~1,7 V sobre o LED do opto, e o relé pode **não desligar**. Além disso, o pull-up de
    5 V empurra corrente para o GPIO. **Solução:** um NPN entre IN e GND (GPIO23 → 1 kΩ → base,
    coletor no IN). Assim o GPIO em ALTO aciona o relé e `RELAY_ACTIVE_HIGH = true`.
- **Se o módulo for de 3 V** (bobina 3 V, ex.: SRD-03VDC), VCC em 3V3 serve, mas confira se o
  regulador da placa aguenta a corrente da bobina (§5).
- **[PENDENTE]** ligar e testar o relé: alimente o módulo, encoste IN em GND e depois em 3,3 V,
  observando o LED do relé.
- O módulo já tem diodo de roda-livre na bobina. O firmware não presume nada sobre a carga
  ligada aos contatos.

### 4.5 LED verde pelo contato NC do relé

```
3V3 ──[1 kΩ]──►|── NC ┐
           LED verde   │ relé (contato)
GND ─────────── COM ◄──┘      relé desligado: NC–COM fechado → LED aceso
                              relé energizado (HIGH): NC aberto → LED apagado
```

- ✅ Atende ao requisito: o LED fica aceso no funcionamento normal e apaga quando o relé
  aciona na condição HIGH.
- Consequências: o firmware **não controla** o LED verde. Ele indica "relé desligado", não
  "firmware rodando". Com a placa energizada e o firmware travado, o LED continua aceso até o
  watchdog (8 s) reiniciar a placa. Os LEDs vermelho e amarelo mostram o estado do firmware
  (doc 06).

### 4.6 RYLR998

- Alimentação de 3,3 V e lógica UART de 3,3 V, ligação direta no ESP32 (Serial2, buffer de RX
  de 512 B).
- O firmware configura o módulo a cada boot (endereço, rede, frequência, parâmetros, potência).
  Ver `docs/03_lora_protocol.md`.
- **[PENDENTE]** Verifique se o seu RYLR998 tem homologação ANATEL (doc 03 §2).

### 4.7 OLED e DHT11

- OLED em 3,3 V: os pull-ups de SDA/SCL do módulo vão para o VCC dele, então ficam em 3,3 V.
- DHT11 em 3,3 V. O firmware lê a cada 2,5 s (o mínimo do DHT11 é 1 s).
- Limitações do DHT11: precisão de ±2 °C / ±5 %UR; **não mede UR abaixo de 20 %**; faixa de
  0 a 50 °C (ver doc 05).

## 5. Orçamento de consumo

| Carga | Trilho | Corrente aproximada |
|---|---|---|
| Aquecedor MQ-135 | 5 V | ~167 mA contínuo |
| Bobina do relé | 5 V ou 3,3 V (conforme o modelo, §4.4) | ~70–90 mA quando acionado |
| ESP32 DevKit (Wi-Fi desligado) | 5 V → 3,3 V | **[MEDIR]** |
| RYLR998 em TX (+14 dBm) | 3,3 V | **[MEDIR / ver datasheet REYAX]** |
| OLED | 3,3 V | ~10–20 mA |
| DHT11, LEDs, buzzer | 3,3 V | < 10 mA |

- **Fonte externa 5 V ≥ 2 A** recomendada. Uma porta USB (500 mA) fica no limite.

## 6. Pendências de hardware

| # | Pendência | O que fazer |
|---|---|---|
| 1 | Relé | Ligar e testar; VCC conforme a bobina (§4.4) |
| 2 | Volume do buzzer (ativo) | Confirmar; transistor se necessário (§4.3) |
| 3 | Piso do ADC / fio do AO solto não detectado | Para uso real: `MQ135_FLOOR_IS_FAULT = true` ou mais resolução (§4.2) |
| 4 | Homologação ANATEL do RYLR998 | Verificar (doc 03) |
| 5 | Consumo total | Medir no 5 V em NORMAL e em HIGH com TX |

## 7. Compilar e gravar

Ver `docs/09_build_flash.md`. Resumo: `pio run -t upload` pela USB; se o upload ficar em
`Connecting....`, segure o botão **BOOT** até começar a gravar.
