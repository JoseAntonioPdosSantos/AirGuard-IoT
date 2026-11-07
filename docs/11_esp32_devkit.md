# 11 — ESP32 DevKit (ESP32-WROOM-32)

Branch `esp32-devkit`. O mesmo firmware compila para o ESP32 DevKit (ambiente padrão) e para a
RP2040-Zero (`pio run -e rp2040zero`). O código específico de cada placa fica só em
`src/platform/board.h` (watchdog, reboot, I2C, UART do LoRa, ADC) e em
`src/config/hardware_pins.h` (mapa de GPIO).

> Placa assumida: **ESP32 DevKit com módulo ESP32-WROOM-32** (DOIT DevKit V1 de 30 pinos ou
> DevKitC de 38 pinos, `board = esp32dev`). Os GPIO escolhidos existem nas duas versões. Se a
> sua placa for **ESP32-S2, S3, C3 ou C6**, o mapa de pinos **não serve**: confira antes de ligar.

## 1. Ligações

| Componente | Pino do módulo | ESP32 DevKit | Observação |
|---|---|---|---|
| **RYLR998** | VDD | **3V3** | só 3,3 V |
| | GND | GND | |
| | TXD | **GPIO16** (RX2) | |
| | RXD | **GPIO17** (TX2) | |
| | NRST | **GPIO4** | ativo em LOW |
| **OLED SSD1306** | VCC | **3V3** | não use 5 V: os pull-ups do módulo levariam 5 V aos GPIO |
| | GND | GND | |
| | SDA | **GPIO21** | |
| | SCL | **GPIO22** | |
| **DHT11 (módulo)** | VCC | **3V3** | idem: o pull-up do módulo vai para o VCC |
| | GND | GND | |
| | DATA (S) | **GPIO27** | |
| **MQ-135 (módulo)** | VCC | **VIN (5 V)** | aquecedor ~150 mA, precisa de 5 V |
| | GND | GND | |
| | AO | 10 kΩ → **GPIO34** → 20 kΩ → GND | **divisor obrigatório**: AO chega a 5 V |
| **Buzzer** | + | 1 kΩ → **GPIO25** | |
| **LED vermelho** | ânodo | 1 kΩ → **GPIO26** | cátodo no GND |
| **LED amarelo** | ânodo | 1 kΩ → **GPIO33** | cátodo no GND |
| **Relé (módulo)** | IN | **GPIO23** | VCC do módulo conforme o modelo (pendência de doc 01) |
| **LED verde** | — | 3V3 → 1 kΩ → LED → contato **NC** do relé, COM → GND | sem GPIO, como antes |

O divisor 10 kΩ / 20 kΩ dá no GPIO34, com AO = 5 V, 5 × 20 / 30 = **3,33 V**. É um pouco acima do
que o ADC do ESP32 mede (~3,1 V com atenuação de 11 dB). Com o MQ-135 em ar limpo, o AO fica
perto de 1 V (0,67 V no pino), então a margem está sobrando. Leituras acima de ~3,06 V no pino
são tratadas como **saturação** (`MQ135_ADC_MAX_VALID = 3800` no ESP32).

## 2. Pinos evitados e por quê

| GPIO | Motivo |
|---|---|
| 6–11 | ligados à memória flash; usá-los trava o chip |
| 1, 3 | UART0 = console USB |
| 0, 2, 5, 12, 15 | pinos de *strapping*: um módulo puxando esses pinos no reset pode impedir o boot. O GPIO12 em HIGH muda a tensão da flash para 1,8 V |
| 14 | gera PWM durante o boot |
| 34–39 | só entrada e sem pull-up interno. Só o MQ-135 (analógico) usa um deles |
| ADC2 (0, 2, 4, 12–15, 25–27) | o ADC2 não funciona com Wi-Fi ligado, por isso o MQ-135 fica no ADC1 (GPIO34). Os pinos 4, 25, 26 e 27 são usados só como digitais, o que não tem problema |

## 3. Diferenças em relação à RP2040-Zero

| Item | RP2040-Zero | ESP32 DevKit |
|---|---|---|
| Core Arduino | Arduino-Pico (Earle Philhower) | Arduino-ESP32 2.0.17 (`espressif32 @ ^6.9.0`) |
| Console | USB nativa | UART0 via conversor USB (CP2102/CH340); abrir o monitor pode reiniciar a placa |
| Watchdog | `rp2040.wdt_*` | Task WDT (`esp_task_wdt`), 8 s, reinicia por pânico |
| UART do LoRa | Serial1 (GP0/GP1) | Serial2 (GPIO17/16), buffer de RX de 512 B |
| ADC | leitura bruta de 12 bits, referência 3V3 | `analogReadMilliVolts` (calibração de fábrica do eFuse), convertida para contagens de 12 bits referidas a 3,3 V. O ADC bruto do ESP32 não é linear |
| `diag`, teste TX/RX invertidos | UART por PIO | a própria Serial2 com os pinos trocados |
| Gravação | arrastar `.uf2` / BOOTSEL | `pio run -t upload` (USB serial, automático) |

A lógica de alertas, o protocolo, a fila de ACK e as telas **não mudaram** (mesmos 84 testes).

## 4. Compilar e gravar

```bash
pio run                      # ESP32 + DHT11 (padrão)
pio run -e esp32dev_dht22    # ou esp32dev_aht20
pio run -t upload            # grava pela USB
pio device monitor           # console 115200 → "help", "diag"
```

No Linux, a porta aparece como `/dev/ttyUSB0` (CP2102/CH340). Se faltar permissão, adicione o
usuário ao grupo `dialout` (`sudo usermod -aG dialout $USER`) e entre de novo na sessão. Se o
upload ficar em `Connecting....`, segure o botão **BOOT** até começar a gravar.

## 5. Não verificado ainda

- Nada foi testado em um ESP32 físico; só a compilação (RAM 8,8 %, flash 27,3 %).
- O `diag`, o watchdog e a leitura do ADC precisam ser conferidos na placa.
- Observado na bancada: com o AO solto e com o pino sem sinal, o ADC devolveu sempre 176 contagens (~0,14 V), não 0. Isso bate com o piso de leitura do ADC do ESP32 com atenuação de 11 dB. Por isso `MQ135_ADC_MIN_VALID = 200` (~0,16 V) no ESP32: abaixo disso o firmware acusa falha do MQ-135 (AO solto ou em curto).
- A precisão do ADC do ESP32 varia de chip para chip. A calibração R0 (`cal start`) absorve um erro
  fixo, mas não a não linearidade perto dos extremos da faixa.
