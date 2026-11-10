# AirGuard IoT

**SMART ENVIRONMENTAL MONITORING** — *Intelligent monitoring. Safer environments.*

Firmware de monitoramento ambiental de cozinha para o **ESP32 DevKit**. O dispositivo funciona
continuamente, **sem Wi-Fi nem internet**:

- **gás** com MQ-135, alarme pela **tensão do AO** (aquecimento, filtro, histerese);
- **temperatura, umidade e índice de calor** com DHT11;
- alertas **NORMAL / LOW / MEDIUM / HIGH** com persistência temporal e recuperação estável;
- **LoRa REYAX RYLR998** (comandos AT por UART): envio de alertas com **ACK, timeout de 60 s,
  retransmissão**, fila com prioridade, deduplicação e heartbeat a cada 5 s;
- **recebimento de texto** de um segundo dispositivo (ex.: gateway WhatsApp), exibido no OLED
  por **3 s**, com ACK ao remetente;
- **OLED SSD1306** amarelo/azul com prioridade visual, **buzzer** por padrões, **relé** em HIGH,
  **LED verde** pelo contato NC do relé e LEDs **amarelo/vermelho** de severidade;
- watchdog, persistência em flash, log serial e **console de simulação** para testar todos os
  níveis sem gases reais.

> ⚠️ Protótipo de estudo. **Não é um detector de gás ou de segurança certificado.** O MQ-135 não
> identifica gases nem mede ppm: a tensão do AO é só uma resposta relativa do sensor.

## Início rápido

```bash
make -C tests                 # testes unitários do nó no PC (ASan/UBSan)
make -C receiver/tests        # testes do receptor
pio run                       # compilar (ESP32 DevKit + DHT11)
pio run -t upload             # gravar
pio device monitor            # console 115200 → digite "help"
```

Branches: `main` = versão original do nó na RP2040-Zero; `esp32-devkit` = porte para o ESP32
antes desta simplificação; `backup/*` = cópias de segurança.

## Receptor (segundo dispositivo)

A pasta [`receiver/`](receiver/README.md) contém o **AirGuard Receiver** (RP2040-Zero + RYLR998 +
LCD 16x2 I2C + LEDs verde/amarelo/vermelho). Ele recebe os alertas pelo LoRa, responde com ACK e
mostra o estado. É um projeto PlatformIO próprio (`cd receiver && pio run`).

## Estrutura

```
AirGuard-IoT/
├── platformio.ini
├── src/
│   ├── main.cpp
│   ├── app/            orquestração + console serial
│   ├── config/         config.h (tempos, rádio, políticas), thresholds.h (limites), hardware_pins.h
│   ├── platform/       board.h: única camada com chamadas do ESP-IDF
│   ├── sensors/        MQ-135 (ADC), DHT11
│   ├── monitoring/     cadeia do MQ-135, ambiente, severidade, alertas
│   ├── communication/  protocolo JSON, fila de ACK, driver RYLR998 (AT)
│   ├── display/        lógica de telas, driver OLED
│   ├── actuators/      buzzer, relé, LED
│   └── utils/          log, flash, texto
├── receiver/           AirGuard Receiver (projeto PlatformIO próprio, RP2040-Zero)
├── tests/              testes unitários do nó (Makefile, sem hardware)
├── tools/              console.py (comandos sem reset), trace_air.py (registro do gás)
└── docs/
```

Onde mexer no dia a dia: **limites** em `src/config/thresholds.h`, **tempos e políticas** em
`src/config/config.h`, **pinos** em `src/config/hardware_pins.h`.

## Documentação

| # | Documento | Conteúdo |
|---|---|---|
| 01 | [Hardware](docs/01_hardware.md) | componentes, **ligações no ESP32**, pinos evitados, análise elétrica, consumo, pendências |
| 02 | [Arquitetura](docs/02_architecture.md) | módulos, fluxo, máquinas de estado, **bibliotecas e justificativa** |
| 03 | [LoRa e protocolo](docs/03_lora_protocol.md) | **configuração do rádio**, ANATEL, **protocolo e mensagens**, ACK/retransmissão, guia do gateway |
| 04 | [MQ-135](docs/04_mq135.md) | cadeia de sinal, aquecimento, **ajuste dos limites de tensão** |
| 05 | [Limites ambientais](docs/05_environmental_thresholds.md) | **tabela de limites** com referências |
| 06 | [Alertas](docs/06_alerts.md) | **alertas sonoros e visuais**, telas, relé, LED, recuperação |
| 07 | [Plano de testes](docs/07_test_plan.md) | testes unitários + **23 cenários experimentais** |
| 08 | [Limitações](docs/08_limitations.md) | **limitações conhecidas** |
| 09 | [Compilação e gravação](docs/09_build_flash.md) | **compilação**, **gravação**, validação física |
| 10 | [Referências](docs/10_references.md) | **referências técnicas e científicas** |
| 11 | [Telemetria](docs/11_telemetry.md) | ESP32 envia pelo Wi-Fi o que transmite e recebe no LoRa (e o STAT do receptor) para o **painel Ambiente** do AirGuard Web |

## Estado atual

| Item | Situação |
|---|---|
| Lógica central (classificação, alertas, protocolo, ACK, rádio AT, telas, texto) | ✅ 84 testes unitários do nó + 12 do receptor passando |
| Nó ESP32 na bancada | ✅ OLED, DHT11, MQ-135 (alarme por tensão), LEDs, buzzer ativo e RYLR998 funcionando |
| Receptor RP2040-Zero | ✅ recebe alertas e heartbeats, envia ACK, LCD funcionando; LEDs ainda não ligados |
| Relé | ⏳ ainda não ligado/testado |
| Testes em hardware (23 cenários) | ⏳ pendentes |
| Pendências de hardware | ver [docs/01_hardware.md §6](docs/01_hardware.md) |
