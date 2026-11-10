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

### Alimentação do LCD: montagem usada na bancada

O LCD 16x2 é feito para **5 V**. Em 3,3 V ele funciona, mas fica **muito apagado**, porque o
contraste não alcança o nível necessário. O módulo I2C (PCF8574) tem **pull-ups de ~4,6 kΩ para o
VCC**. Isso foi medido na bancada com a própria RP2040: SDA em 4,14 V com carga de 20 kΩ e em
3,01 V com carga de 6,7 kΩ, o que dá VCC ≈ 5,1 V e pull-up ≈ 4,6 kΩ. Com o LCD em 5 V, o SDA e o
SCL ficariam em ~5 V, e a RP2040 **não tolera 5 V**.

Montagem atual: **VCC do LCD no 5V** e **um resistor de 10 kΩ de cada linha (SDA e SCL) para o
GND**. Isso forma um divisor com os pull-ups do módulo, e as linhas ficam em ~3,5 V.

```
LCD VCC ── 5V (jumper direto, NÃO no trilho do 3V3 do rádio)
LCD GND ── GND
LCD SDA ──┬── GP4          LCD SCL ──┬── GP5
          └─[10 kΩ]─ GND             └─[10 kΩ]─ GND
```

É um **meio-termo**: 3,5 V fica acima dos 3,3 V recomendados para a RP2040 (mas abaixo do máximo
absoluto, ~3,8 V) e um pouco abaixo do "1" especificado para o PCF8574 em 5 V (0,7 × VCC ≈ 3,6 V).
Funcionou na bancada. **A solução recomendada é um conversor de nível bidirecional (BSS138)**:
LV → 3V3, HV → 5V, LV1/HV1 → GP4/SDA, LV2/HV2 → GP5/SCL, sem os resistores de 10 kΩ.

Depois de passar o LCD para 5 V, **reajuste o potenciômetro azul** (contraste) atrás do módulo.

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

## Telemetria para o AirGuard Web

Cada quadro recebido (com RSSI, SNR e o que o receptor fez com ele), cada ACK enviado e um resumo a
cada 10 s saem na USB como linhas `@T {...}`, no meio do log. A ponte
`AirGuard-Web/bridge/serial_bridge.py` as envia ao painel **Ambiente**. Formato: [docs/11_telemetry.md](../docs/11_telemetry.md).

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
