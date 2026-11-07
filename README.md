# AirGuard IoT

**SMART ENVIRONMENTAL MONITORING** — *Intelligent monitoring. Safer environments.*

Firmware de monitoramento ambiental de cozinha para a **Raspberry Pi RP2040-Zero**. O
dispositivo funciona continuamente, **sem Wi-Fi nem internet**:

- **qualidade do ar relativa** com MQ-135 (calibração R0, filtragem, histerese);
- **temperatura, umidade e índice de calor** (DHT11 instalado; DHT22 ou AHT20 opcionais);
- alertas **NORMAL / LOW / MEDIUM / HIGH** com persistência temporal e recuperação estável;
- **LoRa REYAX RYLR998** (comandos AT por UART): envio de alertas com **ACK, timeout de 60 s, retransmissão**, fila com
  prioridade, deduplicação e controle de tempestade de mensagens;
- **recebimento de texto** de um segundo dispositivo (ex.: gateway WhatsApp), exibido no OLED
  por **3 s**, com ACK ao remetente;
- **OLED SSD1306** amarelo/azul com prioridade visual, **buzzer** por padrões, **relé** em HIGH,
  **LED verde** pelo contato NC do relé e LEDs **amarelo/vermelho** de severidade;
- watchdog, persistência em flash, log serial e **console de simulação** para testar todos os
  níveis sem gases reais.

> ⚠️ Protótipo de estudo. **Não é um detector de gás ou de segurança certificado.** O MQ-135 não
> identifica gases nem mede ppm: o `air_index` é uma resposta relativa ao ar de referência.

## Início rápido

```bash
make -C tests                 # 84 testes unitários no PC (ASan/UBSan)
pio run                       # compilar (DHT11)   |  -e rp2040zero_dht22 / rp2040zero_aht20
pio run -t upload             # gravar
pio device monitor            # console 115200 → digite "help"
```

## Estrutura

```
AirGuard-IoT/
├── platformio.ini
├── src/
│   ├── main.cpp
│   ├── app/            orquestração + console serial
│   ├── config/         config.h, thresholds.h, hardware_pins.h
│   ├── sensors/        MQ-135 (ADC), DHT11/DHT22/AHT20
│   ├── monitoring/     cadeia do MQ-135, ambiente, severidade, alertas
│   ├── communication/  protocolo JSON, fila de ACK, driver RYLR998 (AT)
│   ├── display/        lógica de telas, driver OLED
│   ├── actuators/      buzzer, relé, LED
│   └── utils/          log, flash, texto
├── tests/              testes unitários (Makefile, sem hardware)
└── docs/
```

## Documentação

| # | Documento | Conteúdo (itens da especificação, seção 14) |
|---|---|---|
| 01 | [Hardware](docs/01_hardware.md) | lista de componentes, análise elétrica, **tabela GPIO**, **diagrama**, consumo, pendências |
| 02 | [Arquitetura](docs/02_architecture.md) | módulos, fluxo, máquinas de estado, **bibliotecas e justificativa** |
| 03 | [LoRa e protocolo](docs/03_lora_protocol.md) | **configuração do rádio**, ANATEL, **protocolo e mensagens**, ACK/retransmissão, guia do gateway |
| 04 | [Calibração MQ-135](docs/04_calibration_mq135.md) | cadeia de sinal, aquecimento, **procedimento de calibração**, limites experimentais |
| 05 | [Limites ambientais](docs/05_environmental_thresholds.md) | **tabela de limites** com referências |
| 06 | [Alertas](docs/06_alerts.md) | **alertas sonoros e visuais**, telas, relé, LED, recuperação |
| 07 | [Plano de testes](docs/07_test_plan.md) | testes unitários + **23 cenários experimentais** |
| 08 | [Limitações](docs/08_limitations.md) | **limitações conhecidas** |
| 09 | [Compilação e gravação](docs/09_build_flash.md) | **compilação**, **gravação**, validação física |
| 10 | [Referências](docs/10_references.md) | **referências técnicas e científicas** |

## Estado atual

| Item | Situação |
|---|---|
| Lógica central (classificação, alertas, protocolo, ACK, rádio AT, telas, texto) | ✅ implementada, 84 testes unitários passando |
| Drivers (LoRa, OLED, buzzer, relé, LED, sensores, flash) e integração | ✅ implementados |
| Compilação com o toolchain RP2040 (`pio run`) | ✅ DHT11, DHT22 e AHT20 compilam sem avisos (RAM 6,3 %, flash 4,9 %) |
| Testes em hardware (23 cenários) | ⏳ pendentes (requer montagem) |
| Pendências de hardware | ver [docs/01_hardware.md §6](docs/01_hardware.md) |
