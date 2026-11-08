# 09 — Compilação, gravação e validação

## 1. Pré-requisitos

- Python 3 e **PlatformIO Core** (`pip install platformio`) ou a extensão PlatformIO do VS Code.
- Cabo USB-C **de dados**.
- Para os testes no PC: `g++` (C++17) e `make`.

O `platformio.ini` usa a plataforma comunitária `maxgerhardt/platform-raspberrypi` com o core
**Arduino-Pico (Earle Philhower)**, porque a plataforma oficial traz só o core Mbed, sem a API
de watchdog e EEPROM usada aqui. Na primeira compilação, o PlatformIO baixa a plataforma, o
toolchain ARM e as bibliotecas do `lib_deps` (algumas centenas de MB).

## 2. Testes unitários (sem hardware)

```bash
cd AirGuard-IoT
make -C tests          # compila com ASan/UBSan e executa
# 88 tests, 367 checks, 0 failed checks, 0 failed tests
```

## 3. Compilação do firmware

```bash
cd AirGuard-IoT
pio run                        # DHT11 (padrão, sensor instalado)
pio run -e rp2040zero_dht22    # variante DHT22
pio run -e rp2040zero_aht20    # variante AHT20 no I2C
```

Antes de compilar, revise `src/config/`:

| Arquivo | O que conferir |
|---|---|
| `hardware_pins.h` | Pinos (se mudar a fiação) |
| `config.h` | `DEVICE_ID`, `GATEWAY_ID`, rádio RYLR998 (`LORA_ADDRESS`, `LORA_NETWORK_ID`, `LORA_SF`..., `LORA_CPIN`: iguais no gateway!), `RELAY_ACTIVE_HIGH`, `BUZZER_IS_PASSIVE`, `OLED_I2C_ADDR`, `MQ135_WARMUP_MS` |
| `thresholds.h` | `LOAD_RESISTANCE` (RL_eff medido), `DIVIDER_R_BOTTOM` (20000 em série / 5000 em paralelo), `CIRCUIT_VOLTAGE` (5 V medido), limites |

## 4. Gravação

**Opção A (PlatformIO):**
```bash
pio run -t upload
```
Se a placa não for detectada, segure **BOOT**, aperte e solte **RESET** e solte BOOT. A placa
aparece como unidade `RPI-RP2`. Repita o comando.

**Opção B (arrastar e soltar UF2):**
1. Entre em modo BOOTSEL (como acima). Aparece a unidade `RPI-RP2`.
2. Copie `.pio/build/rp2040zero/firmware.uf2` para a unidade. A placa reinicia sozinha.

## 5. Primeiro uso

```bash
pio device monitor     # 115200 baud
```
1. Confira o log de boot (`SSD1306 ok`, `RYLR998 ok: ...`, sensor `DHT11`).
2. Deixe o MQ-135 aquecer (burn-in de 24–48 h na primeira vez).
3. Calibre: `cal start` em ar limpo (`docs/04_calibration_mq135.md`).
4. Execute o plano de testes (`docs/07_test_plan.md`).

## 6. Validação física (checklist dos critérios de aceitação)

| Critério | Como verificar | Teste |
|---|---|---|
| RP2040-Zero inicia corretamente | log BOOT + tela de boot | 1 |
| MQ-135 fornece leituras válidas | STAT: ADC entre 8 e 4085, Rs plausível, `air=ok` | 2, 3 |
| Temperatura e umidade exibidas | tela principal | 3 |
| Qualidade do ar classificada | `Air Quality: NORMAL/LOW/...` | 3–6 |
| Alertas LOW, MEDIUM e HIGH | eventos e saídas | 4–10 |
| Buzzer responde à severidade | padrões do doc 06 | 4–6 |
| Relé conforme a política | liga em HIGH, desliga após a recuperação | 21, 22 |
| LED verde indica o estado normal | aceso em NORMAL, apagado em HIGH (relé) | 3, 6 |
| Envio de alertas LoRa | log do gateway | 4–10 |
| Recebimento de mensagens LoRa | TEXT do gateway | 11 |
| ACK funciona | `ACK OK` | 14 |
| Retransmissão após 60 s sem ACK | log `retransmission` | 15, 16 |
| Mensagem remota por 3 s | vídeo | 12 |
| Tela principal restaurada | automático | 13 |
| Monitoramento durante falhas de comunicação | alertas continuam com o rádio em falha | 18 |
