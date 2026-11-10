# 09 — Compilação, gravação e validação

## 1. Pré-requisitos

- Python 3 e **PlatformIO Core** (`pip install platformio`) ou a extensão PlatformIO do VS Code.
  Neste computador o PlatformIO está em `~/.platformio/penv/bin/pio`.
- Cabo USB **de dados**.
- Para os testes no PC: `g++` (C++17) e `make`.

O nó usa a plataforma oficial `espressif32 @ ^6.9.0` (core Arduino-ESP32 2.0.17). Na primeira
compilação, o PlatformIO baixa a plataforma, o toolchain e as bibliotecas do `lib_deps` (algumas
centenas de MB).

## 2. Testes unitários (sem hardware)

```bash
cd AirGuard-IoT
make -C tests            # nó: compila com ASan/UBSan e executa
make -C receiver/tests   # receptor (inclui o teste de interoperabilidade com o nó)
```

## 3. Compilação do firmware

```bash
cd AirGuard-IoT
pio run                  # ESP32 DevKit + DHT11
```

Antes de compilar, revise `src/config/`:

| Arquivo | O que conferir |
|---|---|
| `hardware_pins.h` | Pinos (se mudar a fiação) |
| `config.h` | `DEVICE_ID`, `GATEWAY_ID`, rádio RYLR998 (`LORA_ADDRESS`, `LORA_NETWORK_ID`, `LORA_SF`..., `LORA_CPIN`: iguais no receptor!), `RELAY_ACTIVE_HIGH`, `BUZZER_IS_PASSIVE`, `OLED_I2C_ADDR`, `MQ135_WARMUP_MS`, **valores de bancada** (`DEV_SKIP_MQ135_WARMUP`, `MQ135_FLOOR_IS_FAULT`, `AIR_RECOVER_MS`) |
| `thresholds.h` | `DIVIDER_R_BOTTOM` (20000 com 2× 10 kΩ em série), `CIRCUIT_VOLTAGE`, limites (`GAS_VOLTS`, `TEMPERATURE`, `HUMIDITY`), filtro do MQ-135 |

## 4. Gravação

```bash
pio run -t upload
```

No Linux, a porta aparece como `/dev/ttyUSB0` (CP2102/CH340). Se faltar permissão, adicione o
usuário ao grupo `dialout` (`sudo usermod -aG dialout $USER`) e entre de novo na sessão. Se o
upload ficar em `Connecting....`, segure o botão **BOOT** até começar a gravar.

O receptor é outro projeto: `cd receiver && pio run -t upload` (ver `receiver/README.md`).

## 5. Primeiro uso

```bash
pio device monitor     # 115200 baud; abrir o monitor pode reiniciar a placa
```

1. Confira o log de boot (`SSD1306 ok`, `RYLR998 ok: ...`, sensor `DHT11`).
2. Deixe o MQ-135 aquecer (burn-in de 24–48 h na primeira vez, doc 04).
3. `help` lista os comandos; `diag` testa a fiação (I2C, DHT11, RYLR998).
4. Para mandar comandos sem reiniciar a placa:
   `~/.platformio/penv/bin/python tools/console.py status`.
5. Execute o plano de testes (`docs/07_test_plan.md`).

## 6. Validação física (checklist dos critérios de aceitação)

| Critério | Como verificar | Teste |
|---|---|---|
| ESP32 inicia corretamente | log BOOT + tela de boot | 1 |
| MQ-135 fornece leituras válidas | STAT: `Vao` plausível, `air=ok` | 2, 3 |
| Temperatura e umidade exibidas | tela principal | 3 |
| Gás classificado | eventos AIR_QUALITY e tela de alerta | 3–6 |
| Alertas LOW, MEDIUM e HIGH | eventos e saídas | 4–10 |
| Buzzer responde à severidade | padrões do doc 06 | 4–6 |
| Relé conforme a política | liga em HIGH, desliga após a recuperação | 21, 22 |
| LED verde indica o estado normal | aceso em NORMAL, apagado em HIGH (relé) | 3, 6 |
| Envio de alertas LoRa | log/LCD do receptor | 4–10 |
| Recebimento de mensagens LoRa | TEXT do gateway | 11 |
| ACK funciona | `ACK OK` | 14 |
| Retransmissão após 60 s sem ACK | log `retransmission` | 15, 16 |
| Mensagem remota por 3 s | vídeo | 12 |
| Tela principal restaurada | automático | 13 |
| Monitoramento durante falhas de comunicação | alertas continuam com o rádio em falha | 18 |
