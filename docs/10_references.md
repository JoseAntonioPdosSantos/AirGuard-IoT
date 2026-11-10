# 10 — Referências técnicas e científicas

Cada referência está marcada assim:
- **[C]** consultada durante o desenvolvimento (busca ou leitura registrada);
- **[R]** referência padrão citada pelo título e conteúdo conhecido, **não reaberta nesta
  etapa**. Confira a versão vigente antes de usar o valor em um relatório.

## Placa e microcontrolador

O nó usa o ESP32 DevKit; as referências da RP2040 valem para o **receptor** (`receiver/`).

- [R] Raspberry Pi Ltd. *RP2040 Datasheet* (tabela de funções GPIO §1.4.3, ADC, watchdog). https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf
- [R] Waveshare. *RP2040-Zero wiki* (pinout, WS2812 em GP16). https://www.waveshare.com/wiki/RP2040-Zero
- [C] Mischianti, R. *Waveshare RP2040-Zero: high-resolution pinout and specs* (2022). https://mischianti.org/waveshare-rp2040-zero-high-resolution-pinout-and-specs/
- [C] Zephyr Project. *Waveshare RP2040-Zero board documentation* (ADC0–3 = GP26–29). https://docs.zephyrproject.org/latest/boards/waveshare/rp2040_zero/doc/index.html

## Sensores e módulos

- [C] Zhengzhou Winsen Electronics. *MQ135 Air Quality Gas Sensor Manual* (aquecedor 30 ± 3 Ω, pré-aquecimento > 48 h, RL ajustável). https://www.winsen-sensor.com/manual/mq135.html
- [C] Hanwei Electronics. *MQ-135 Gas Sensor datasheet* (aquecedor 29 ± 3 Ω, pré-aquecimento > 24 h, curvas com RL = 20 kΩ, dependência de T/UR). Cópias: https://datasheetq.com/pdf-view/MQ-135-HANWEI.pdf e https://oldshop.soldered.com/productdata/2022/03/Soldered_MQ-135_datasheet.pdf
- [C] Universal-Solder. *26166 MQ-135 Air Quality Gas Sensor* (tempos de pré-aquecimento conforme a idade de armazenamento). https://www.universal-solder.ca/datasheets/26166_MQ-135_Air_Quality_Gas_Sensor.pdf
- [C] REYAX. *LoRa AT Command Guide RYLR998/RYLR498* (20-MAY-2021; comandos, payload ≤ 240 B, `+RCV`, códigos `+ERR`, regra do preâmbulo, recomendação `8,7,1,12`). Cópia: https://www.botnroll.com/img/cms/LoRa_AT_Command_RYLR998_RYLR498_EN.pdf
- [R] REYAX. *RYLR998 datasheet* (consumo, faixa de alimentação).
- [C] DHT11 datasheet (20–90 %UR ±5 %, 0–50 °C ±2 °C, resolução de 1). Cópia: https://ece-classes.usc.edu/ee459/library/datasheets/DHT11.pdf
- [R] Semtech. *AN1200.13 — LoRa Modem Designer's Guide* (cálculo do tempo no ar).
- [R] Solomon Systech. *SSD1306 datasheet*.

## Regulamentação

- [C] IB-Lenhardt. *ANATEL: 6 dB bandwidth exemption to end on October 6, 2026* (Ato nº 14.158, item 10.3.4.4; faixas 902–907,5 / 915–928 MHz; banda mínima de 500 kHz para novas certificações). https://ib-lenhardt.com/news/anatel-6-db-bandwidth-exemption-to-end-on-october-6-2026
- [C] IB-Lenhardt. *Technical requirements of restricted radio communication devices* (Ato nº 14.448). https://ib-lenhardt.com/news/technical-requirements-of-restricted-radio-communication-devices
- [R] ANATEL. *Ato nº 14.448/2017 e alterações — Requisitos técnicos de equipamentos de radiação restrita*. https://www.gov.br/anatel/pt-br/regulado/espectro — **os limites de potência/EIRP não foram confirmados nesta documentação.**

## Ambiente, conforto e saúde

- [R] World Health Organization. *WHO Housing and Health Guidelines* (2018) — 18 °C como temperatura interna mínima recomendada na estação fria. https://www.who.int/publications/i/item/9789241550376
- [R] U.S. EPA. *A Brief Guide to Mold, Moisture and Your Home* — manter UR < 60 %, idealmente 30–50 %. https://www.epa.gov/mold/brief-guide-mold-moisture-and-your-home
- [R] NOAA / National Weather Service. *Heat Index* (categorias Caution 80 °F, Extreme Caution 90 °F, Danger 103 °F). https://www.weather.gov/safety/heat-index
- [R] NOAA / Weather Prediction Center. *The Heat Index Equation* (Rothfusz e ajustes). https://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml
- [R] Public Health England. *Cold Weather Plan for England* — efeitos à saúde abaixo de 18 °C, 16 °C e 9–12 °C.
- [R] ASHRAE. *Standard 55 — Thermal Environmental Conditions for Human Occupancy*.

## Software

- [R] Espressif. *Arduino core for the ESP32* (nó). https://github.com/espressif/arduino-esp32
- [R] Earle F. Philhower III. *Arduino-Pico core* (receptor). https://github.com/earlephilhower/arduino-pico
- [R] M. Gerhardt. *platform-raspberrypi (PlatformIO)*. https://github.com/maxgerhardt/platform-raspberrypi
- [R] Adafruit. *Adafruit_SSD1306*, *Adafruit-GFX-Library*. https://github.com/adafruit
