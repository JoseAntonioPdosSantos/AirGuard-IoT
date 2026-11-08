# AirGuard Receiver

Segundo dispositivo do AirGuard IoT. Ele recebe pelo LoRa os alertas do sensor da cozinha
(`KITCHEN_01`), **responde com ACK** e mostra o estado num **LCD 16x2 I2C** e em **três LEDs**:

| LED | Quando acende |
|---|---|
| **Verde** | tudo normal. **Pisca** enquanto nenhuma mensagem foi recebida desde que ligou |
| **Amarelo** | alerta LOW ou MEDIUM em alguma categoria |
| **Vermelho** | alerta HIGH em alguma categoria |
| **Amarelo piscando** | **sem sinal**: nenhuma mensagem do sensor há mais de 15 s |

Na partida, os três LEDs acendem juntos por ~1 s (teste das lâmpadas).

O receptor responde como `GATEWAY_01`, o destino configurado no sensor. Com ele ligado, o
sensor recebe o ACK e o `NO ACK` some.

## Placa e ligações (Waveshare RP2040-Zero)

> **Solde os pinos da RP2040-Zero.** Pinos só encaixados não fazem contato confiável (foi o
> problema da primeira montagem).

| Componente | Pino do módulo | RP2040-Zero |
|---|---|---|
| **RYLR998** (2º módulo) | VDD | **3V3** (só 3,3 V) |
| | GND | GND |
| | TXD | **GP1** |
| | RXD | **GP0** |
| | NRST | **GP2** |
| **LCD 16x2 + módulo I2C (PCF8574)** | VCC | **3V3**, veja abaixo |
| | GND | GND |
| | SDA | **GP4** |
| | SCL | **GP5** |
| **LED verde** | ânodo (perna longa) | 220 Ω–1 kΩ → **GP6** (cátodo no GND) |
| **LED amarelo** | ânodo | 220 Ω–1 kΩ → **GP7** |
| **LED vermelho** | ânodo | 220 Ω–1 kΩ → **GP8** |

Rosqueie a antena do RYLR998 antes de ligar.

### Alimentação do LCD: atenção

O LCD 16x2 é feito para **5 V**, e o módulo I2C tem resistores pull-up ligados ao **VCC do LCD**.
Os GPIO da RP2040 **não toleram 5 V**.

1. **Primeiro tente com o VCC do LCD no 3V3** e gire o **potenciômetro azul** atrás do módulo
   (contraste) até os caracteres aparecerem. Muitos LCDs funcionam assim, com contraste mais fraco.
2. Se não aparecer nada com o contraste no máximo: alimente o LCD com **5 V** e use um
   **conversor de nível lógico bidirecional** (ex.: módulo BSS138) entre GP4/GP5 e SDA/SCL.
   **Não** ligue SDA/SCL de um LCD alimentado com 5 V direto na RP2040.

## Telas do LCD

```
AirGuard RX          (antes da 1ª mensagem)
Aguardando LoRa

SEM SINAL            nenhuma mensagem há mais de 15 s
ultimo ha 25s

HIGH: GAS            nível e categoria mais grave (GAS, TEMP, UMIDADE, SISTEMA)
25.4C 61% 3.10V      temperatura, umidade, gás (tensão do AO do MQ-135)
RSSI -45dBm #12      alterna a cada 3 s: sinal do último quadro e eventos recebidos
```

O sensor manda um **heartbeat a cada 5 s** com o nível de cada categoria e as leituras atuais. Os
valores do LCD são atualizados a cada 5 s, e o estado dos LEDs é ressincronizado mesmo que um
evento tenha se perdido. Alertas e recuperações continuam chegando na hora, com ACK.

## Compilar, testar e gravar

```bash
make -C receiver/tests                       # 12 testes no PC (inclui compatibilidade com o código do sensor)
cd receiver
~/.platformio/penv/bin/pio run -t upload     # grava a RP2040-Zero (copie o .uf2 se o picotool falhar)
~/.platformio/penv/bin/pio device monitor    # log 115200: [RX], [ACK], [LORA], [LCD]
```

Se o upload não funcionar (picotool sem permissão), segure **BOOT**, aperte **RESET**, solte
**BOOT** e copie `receiver/.pio/build/rp2040zero/firmware.uf2` para a unidade `RPI-RP2`.

## Configuração (`receiver/src/rx_config.h`)

- Rádio: **igual ao sensor** (915 MHz, SF8, BW 125 kHz, CR 4/5, preâmbulo 12, rede 6, 14 dBm).
  O endereço do receptor é **1**, e o do sensor é 101.
- `AIR_INDEX_IS_VOLTAGE = true`: o sensor no modo de tensão manda `air_index = 100 × V / 5`, e o
  LCD mostra `V = air_index / 20`. Com resolução de 0,05 V, porque o índice é inteiro.

## Limitações

- O heartbeat não tem ACK: o sensor não sabe se o receptor está ligado; só o receptor detecta a
  perda de sinal. Os alertas, com ACK, continuam indicando `NO ACK` no sensor.
- Sem buzzer no receptor (não foi pedido).
- Não testado em hardware ainda: só compilado e testado no PC.
