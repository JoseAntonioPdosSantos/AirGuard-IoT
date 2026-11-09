# 01 — Hardware: componentes, análise elétrica e conexões

> **Status:** este documento descreve a **montagem real** informada pelo usuário e valida
> cada ligação. Itens marcados **[MEDIR]** ou **[PENDENTE]** precisam ser conferidos na bancada
> antes de energizar. Os números de pino são os números **GP** impressos na RP2040-Zero.

## 1. Lista de componentes

| # | Componente | Modelo | Alimentação | Lógica | Observações |
|---|---|---|---|---|---|
| 1 | MCU | Waveshare **RP2040-Zero** | 5 V (USB-C ou pino 5V) → regulador 3,3 V | **3,3 V, GPIOs NÃO tolerantes a 5 V** | GP0–GP15 e GP26–GP29 nas bordas; GP16 = WS2812 interno |
| 2 | Sensor de gás | Módulo **MQ-135** (placa com LM393) | **5 V** (aquecedor) | AO analógica 0…~5 V | Aquecedor ≈ 29–31 Ω → **~160 mA / ~0,8 W** |
| 3 | Temperatura/umidade | Módulo **DHT11** (3 pinos: S, VCC, GND) | 3,3 V | 3,3 V | 0–50 °C ±2 °C; 20–90 %UR ±5 %; resolução de 1 °C / 1 % |
| 4 | Rádio LoRa | **REYAX RYLR998** (SX1262 interno, comandos AT por UART) | 3,3 V | 3,3 V (UART 115200) | Payload máx. **240 bytes**. **Nunca transmitir sem antena** |
| 5 | Display | OLED **0,96" SSD1306 128×64 I2C, amarelo/azul** | 2,2–5,5 V → usar **3,3 V** | 3,3 V | Endereço 0x3C (impresso "0x78") |
| 6 | Buzzer | **ativo** (oscilador interno) | via GPIO + 1 kΩ | 3,3 V | Ver §4.3 |
| 7 | Relé | Módulo relé 1 canal **[PENDENTE: tensão da bobina e tipo de entrada]** | **ver §4.4** | IN | Contato NC usado para o LED verde |
| 8 | LED vermelho | LED + 1 kΩ | GPIO | 3,3 V | ~1,3 mA |
| 9 | LED amarelo | LED + 1 kΩ | GPIO | 3,3 V | ~1,2 mA |
| 10 | LED verde | LED + 1 kΩ | 3V3 via contato NC do relé | — | Aceso enquanto o relé está desligado |
| 11 | Resistores | 3× 10 kΩ (divisor MQ-135); 4× 1 kΩ | — | — | 1 % recomendado no divisor |
| 12 | Fonte | 5 V / ≥ 2 A regulada | — | — | Ver §5 |

## 2. RP2040-Zero: GPIOs e restrições

Fontes: datasheet do RP2040 (tabela de funções GPIO, §1.4.3) e pinout da RP2040-Zero (ver
`docs/10_references.md`).

- Pinos acessíveis nas bordas: **GP0–GP15** e **GP26–GP29**. **GP16** aciona o WS2812 da placa.
- ADC de 12 bits **somente em GP26–GP29** (ADC0–ADC3).
- Todos os GPIOs operam em **3,3 V** e **não são tolerantes a 5 V**.

## 3. Tabela de conexões (montagem atual) e validação

| Função | Componente → RP2040-Zero | Periférico | Validação |
|---|---|---|---|
| LoRa TX do RP2040 | RYLR998 **RXD** → **GP0** | UART0 TX (`Serial1`) | ✅ GP0 é TX válido do UART0; TX→RX cruzado corretamente |
| LoRa RX do RP2040 | RYLR998 **TXD** → **GP1** | UART0 RX | ✅ GP1 é RX válido do UART0 |
| LoRa reset | RYLR998 **RST** → **GP2** | GPIO | ✅ o firmware pulsa NRST em nível baixo por 100 ms |
| LoRa alimentação | VDD → 3V3, GND → GND | — | ✅ módulo de 3,3 V. **[MEDIR]** consumo em TX (§5) |
| OLED SDA | SDA → **GP4** | I2C0 SDA | ✅ mux válido |
| OLED SCL | SCK/SCL → **GP5** | I2C0 SCL | ✅ mux válido |
| OLED alimentação | VDD → 3V3 | — | ✅ pull-ups do módulo ficam em 3,3 V |
| DHT11 dados | S → **GP28** | GPIO | ✅ GP28 funciona como GPIO digital; pull-up do módulo em 3,3 V |
| DHT11 alimentação | VCC → 3V3 | — | ✅ DHT11 aceita 3–5,5 V |
| Buzzer | (+) → 1 kΩ → **GP6**, (−) → GND | GPIO / PWM | ⚠️ funciona, mas o volume pode ser **muito baixo** (§4.3) |
| LED vermelho | (+) → 1 kΩ → **GP7** | GPIO | ✅ |
| LED amarelo | (+) → 1 kΩ → **GP12** | GPIO | ✅ |
| LED verde | 3V3 → 1 kΩ → LED → **NC** do relé; **COM** → GND | — | ✅ apaga quando o relé energiza. Ver §4.5 |
| Relé | IN → **GP15**, VCC → **3V3**, GND → GND | GPIO | ❌ **provável problema** se o módulo for de 5 V (§4.4) |
| MQ-135 AO | AO → 10 kΩ → **GP29**; GP29 → 2× 10 kΩ → GND | ADC3 | ✅ se os 2× 10 kΩ estiverem **em série** (20 kΩ). **[PENDENTE]** confirmar (§4.2) |
| MQ-135 alimentação | VCC → 5 V, GND → GND | — | ✅ aquecedor fora do 3,3 V |
| Livres | GP3, GP8–GP11, GP13, GP14, GP26, GP27 | — | — |

## 4. Análise elétrica

### 4.1 MQ-135: aquecedor

- Resistência do aquecedor: 29 ± 3 Ω (Hanwei) / 30 ± 3 Ω (Winsen). A 5 V: **I ≈ 167 mA, P ≈ 0,83 W**.
- Correto: VCC no barramento **5 V**.
- O sensor esquenta. Mantenha o DHT11 a pelo menos 5 cm dele.

### 4.2 MQ-135: divisor de tensão

```
AO ──[ 10 kΩ ]──┬──► GP29 (ADC3)
                │
             [ 10 kΩ ]
                │            (2× 10 kΩ em SÉRIE = 20 kΩ)
             [ 10 kΩ ]
                │
GND ────────────┘
```

| Ligação dos 2× 10 kΩ | R_BOTTOM | Fator | AO = 5,00 V → GP29 | Avaliação |
|---|---|---|---|---|
| **Em série** (configurado no firmware) | 20 kΩ | 0,667 | **3,33 V** | ✅ Aceitável. No topo da escala o ADC satura (> 4085 é tratado como falha), mas na prática AO raramente chega perto de 5 V, porque Rs nunca é zero. Se o seu 5 V passar de 5,0 V, a tensão no pino passa de 3,3 V, ainda dentro do máximo absoluto do RP2040 (IOVDD + 0,5 V). |
| Em paralelo | 5 kΩ | 0,333 | 1,67 V | ✅ Seguro, mas usa só metade da escala do ADC. Exige `DIVIDER_R_BOTTOM = 5000.0f` em `thresholds.h`. |

- **O valor configurado precisa corresponder à montagem.** Com o valor errado, Rs e o
  `air_index` saem distorcidos. A conversão não é linear, então o erro não se cancela na
  razão Rs/R0.
- Um capacitor de 100 nF entre GP29 e GND (opcional) estabiliza a leitura.
- **Efeito de carga [MEDIR]:** o divisor (30 kΩ) fica em paralelo com RL do módulo
  (frequentemente 1 kΩ, código "102"). Configure `mq135::LOAD_RESISTANCE` com
  `RL_eff = RL × 30k / (RL + 30k)`: 1 kΩ → 0,968 kΩ; 10 kΩ → 7,5 kΩ.
- O pino **DO** (comparador, 5 V) **não deve ser ligado** a nenhum GPIO.

### 4.3 Buzzer direto no GPIO com 1 kΩ

- A corrente fica limitada a **(3,3 V − queda no buzzer) / 1 kΩ ≈ 1–3 mA**. É seguro para o GPIO.
- Um buzzer **ativo** típico (com oscilador interno) é especificado para 3–5 V e ~20–30 mA.
  Com 1 kΩ em série, ele pode soar **muito fraco ou nem soar**.
- Um buzzer **passivo** (precisa de PWM) também fica fraco com 1 kΩ.
- **Recomendação:** teste com `sim air 60` (padrão HIGH). Se o volume for insuficiente, use um
  transistor NPN (BC547/2N2222): GP6 → 1 kΩ → base, emissor no GND, buzzer entre 5 V e o
  coletor, mais um diodo 1N4148 em antiparalelo se o buzzer for magnético.
  **Não remova o resistor ligando o buzzer direto no GPIO.**
- O buzzer instalado é **ativo** (`cfg::BUZZER_IS_PASSIVE = false`): o firmware só coloca o pino
  em HIGH/LOW. Se um dia voltar a usar um passivo, defina `true` (usa `tone()`).

### 4.4 Relé alimentado por 3,3 V ❌

- A maioria dos módulos de relé de 1 canal usa bobina de **5 V** (ex.: SRD-05VDC). Esse tipo de
  relé precisa de cerca de 75 % da tensão nominal (~3,75 V) para fechar com segurança. Com
  **3,3 V ele pode não acionar ou ficar "batendo"**. A bobina ainda puxa ~70–90 mA do regulador
  3,3 V da placa.
- **Se o módulo for de 5 V:** ligue o **VCC do módulo ao 5 V**. A entrada IN pode então ser de
  dois tipos:
  - **Ativo-alto com transistor, sem optoacoplador** (comum em módulos "1 canal" simples):
    3,3 V no IN costuma ser suficiente. Use `RELAY_ACTIVE_HIGH = true`.
  - **Ativo-baixo com optoacoplador e IN puxado para 5 V:** com o GPIO em ALTO (3,3 V) ainda
    sobra ~1,7 V sobre o LED do opto, e o relé pode **não desligar**. Além disso, o pull-up de
    5 V empurra corrente para o GPIO. **Solução:** um NPN entre IN e GND (GP15 → 1 kΩ → base,
    coletor no IN). Assim o GPIO em ALTO aciona o relé e `RELAY_ACTIVE_HIGH = true`.
- **Se o módulo for de 3 V** (bobina 3 V, ex.: SRD-03VDC), a ligação atual serve, mas confira se
  o regulador da placa aguenta a corrente da bobina (§5).
- **[PENDENTE / MEDIR]** Informe o texto impresso no relé (ex.: "SRD-05VDC-SL-C") e se a placa tem
  optoacoplador ou jumper H/L. Depois teste: alimente o módulo, encoste IN em GND e depois em
  3,3 V, observando o LED do relé.
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

- Alimentação de 3,3 V e lógica UART de 3,3 V, ligação direta com a RP2040: ✅.
- O firmware configura o módulo a cada boot (endereço, rede, frequência, parâmetros, potência).
  Ver `docs/03_lora_protocol.md`.
- **[PENDENTE]** Verifique se o seu RYLR998 tem homologação ANATEL (doc 03 §2).

### 4.7 OLED e DHT11

- OLED em 3,3 V: ✅. Os pull-ups de SDA/SCL do módulo vão para o VCC dele, então ficam em 3,3 V.
- DHT11 em 3,3 V: ✅. O firmware lê a cada 2,5 s (o mínimo do DHT11 é 1 s).
- Limitações do DHT11: precisão de ±2 °C / ±5 %UR; **não mede UR abaixo de 20 %**; faixa de
  0 a 50 °C (ver doc 05).

## 5. Orçamento de consumo

| Carga | Trilho | Corrente aproximada |
|---|---|---|
| Aquecedor MQ-135 | 5 V | ~167 mA contínuo |
| Bobina do relé | **5 V (recomendado)** ou 3,3 V (atual) | ~70–90 mA quando acionado |
| RP2040-Zero | 5 V → 3,3 V | ~25–40 mA |
| RYLR998 em TX (+14 dBm) | 3,3 V | **[MEDIR / ver datasheet REYAX]** |
| OLED | 3,3 V | ~10–20 mA |
| DHT11, LEDs, buzzer | 3,3 V | < 10 mA |

- **Fonte externa 5 V ≥ 2 A** recomendada. Uma porta USB (500 mA) fica no limite.
- **[PENDENTE]** Confira a corrente máxima do regulador 3,3 V da sua RP2040-Zero. Com o relé no
  3,3 V, a soma (relé + RYLR998 em TX + OLED + RP2040) pode passar de 200 mA. Mais um motivo
  para levar o relé ao 5 V.
- **GND comum** entre tudo: ✅ (todos os módulos estão ligados ao GND da placa).

## 6. Pendências de hardware

| # | Pendência | O que fazer |
|---|---|---|
| 1 | Relé em 3,3 V | Informar o modelo; provavelmente mover o VCC para 5 V (§4.4) |
| 2 | Divisor do MQ-135: série ou paralelo | Confirmar e ajustar `DIVIDER_R_BOTTOM` (§4.2) |
| 3 | Volume do buzzer (ativo) | Testar; transistor se necessário (§4.3) |
| 4 | RL do módulo MQ-135 | Medir e configurar RL_eff (§4.2) |
| 5 | Homologação ANATEL do RYLR998 | Verificar (doc 03) |
| 6 | Capacidade do regulador 3,3 V | Datasheet/esquema da placa (§5) |

## 7. Diagrama elétrico de referência (montagem atual)

```
                         +5V (fonte 5 V / 2 A)
     ┌───────────────────────┬──────────────────────────────┐
     │                       │                              │
 MQ-135 VCC        (relé VCC: recomendado aqui)       RP2040-Zero 5V
 MQ-135 AO ──[10k]──┬── GP29                         ┌───────────────┐
                 [10k]                               │  RP2040-Zero  │
                 [10k]   (série)                     │               │
                   GND                         GP0 ──┤ TX ──► RXD    │
                                               GP1 ──┤ RX ◄── TXD  RYLR998 (3V3, antena!)
   3V3 ─┬─ OLED VDD     GP4 ── OLED SDA        GP2 ──┤ ──► RST       │
        ├─ DHT11 VCC    GP5 ── OLED SCL              │               │
        ├─ RYLR998 VDD  GP28 ── DHT11 S              │               │
        │               GP6 ──[1k]── Buzzer(+)  Buzzer(−) ── GND     │
        │               GP7 ──[1k]──►|── GND (LED vermelho)          │
        │               GP12 ─[1k]──►|── GND (LED amarelo)           │
        │               GP15 ──────── Relé IN                        │
        └──[1k]──►|── Relé NC      Relé COM ── GND   (LED verde)     │
                                                     └───────────────┘
  GND ═══════════════ GND comum a todos os módulos ═══════════════════
```
