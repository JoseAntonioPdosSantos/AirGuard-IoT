# 05 — Limites ambientais

> Os limites são **configuráveis** (`src/config/thresholds.h`) e foram escolhidos para um
> **protótipo**. Eles separam *desconforto* (LOW), *atenção* (MEDIUM) e *crítico para este
> protótipo* (HIGH). **Não são limites médicos nem toxicológicos.** Não existe uma temperatura
> universal que represente perigo para todas as pessoas e ambientes: idosos, crianças, gestantes
> e pessoas com doenças cardiovasculares ou respiratórias são mais sensíveis.

## 1. Tabela de limites

### Temperatura

O lado **quente** usa a **temperatura aparente** `max(T, índice de calor)`, de modo que a
umidade entra no cálculo. O lado **frio** usa a temperatura de bulbo seco.

| Constante (conceito) | Faixa | Severidade | Referência |
|---|---|---|---|
| TEMPERATURE_NORMAL | 18,0 °C ≤ T e aparente < 26,7 °C | NORMAL | WHO (mínimo de 18 °C); ASHRAE 55 (conforto) |
| TEMPERATURE_WARNING (quente) | aparente ≥ 26,7 °C (80 °F) | LOW | NOAA/NWS, índice de calor "Caution" |
| TEMPERATURE_WARNING (quente) | aparente ≥ 32,2 °C (90 °F) | MEDIUM | NOAA/NWS "Extreme caution" |
| TEMPERATURE_CRITICAL (quente) | aparente ≥ 39,4 °C (103 °F) | HIGH | NOAA/NWS "Danger" |
| TEMPERATURE_WARNING (frio) | T ≤ 18,0 °C | LOW | WHO Housing and Health Guidelines (2018) |
| TEMPERATURE_WARNING (frio) | T ≤ 16,0 °C | MEDIUM | Cold Weather Plan for England (risco respiratório abaixo de 16 °C) |
| TEMPERATURE_CRITICAL (frio) | T ≤ 12,0 °C | HIGH | Cold Weather Plan for England (sobrecarga cardiovascular entre 9 e 12 °C) |
| Histerese | 1,0 °C | — | escolha de projeto; o DHT11 tem resolução de 1 °C (exatidão de ±2 °C) |

### Umidade relativa

| Constante (conceito) | Faixa | Severidade | Referência |
|---|---|---|---|
| HUMIDITY_NORMAL | 30 % < UR < 60 % | NORMAL | EPA: manter UR abaixo de 60 %, idealmente entre 30 e 50 % |
| HUMIDITY_WARNING (alta) | UR ≥ 60 % | LOW | EPA: crescimento de mofo |
| HUMIDITY_WARNING (alta) | UR ≥ 80 % | MEDIUM | UR sustentada > 70–80 % favorece mofo em superfícies. Era 70 %; elevado a pedido do usuário porque a UR ambiente local fica perto de 70 % |
| HUMIDITY_CRITICAL (alta) | UR ≥ 85 % | HIGH | **protótipo:** condensação provável (vapor intenso, vazamento) |
| HUMIDITY_WARNING (baixa) | UR ≤ 30 % | LOW | EPA (limite inferior recomendado) |
| HUMIDITY_WARNING (baixa) | UR ≤ 25 % | MEDIUM | **protótipo:** ressecamento e irritação |
| HUMIDITY_CRITICAL (baixa) | UR ≤ 20 % | HIGH | **protótipo** |
| Histerese | 3 %UR | — | escolha de projeto; o DHT11 tem resolução de 1 % e exatidão de ±5 % |

### Qualidade do ar (índice relativo)

| Faixa de `air_index` | Severidade | Observação |
|---|---|---|
| < 15 | NORMAL | |
| ≥ 15 | LOW | **valor inicial**, a calibrar (doc 04 §5) |
| ≥ 30 | MEDIUM | **valor inicial**, a calibrar |
| ≥ 50 | HIGH | **valor inicial**, a calibrar |
| histerese | 5 pontos | |

### Qualidade do ar pela tensão do AO (modo ativo no ESP32)

Com `cfg::AIR_ALERT_BY_VOLTAGE = true` (padrão atual), o alarme usa a **tensão na saída AO do
MQ-135** (0–5 V, filtrada por mediana + média móvel), sem precisar do R0. O índice passa a ser
`air_index = 100 × V(AO) / 5 V`, ou seja, a tensão do AO em porcentagem de 5 V.

| Tensão no AO | `air_index` | Severidade |
|---|---|---|
| < 1,5 V | < 30 | NORMAL |
| ≥ 1,5 V | ≥ 30 | LOW |
| ≥ 2,5 V | ≥ 50 | MEDIUM |
| ≥ 3,5 V | ≥ 70 | HIGH |
| histerese | 0,3 V (6 pontos) | |

Valores **iniciais**, definidos pela bancada: ar limpo ~0,5 V; isqueiro perto do sensor leva o AO a quase
5 V. Ajuste `AO_LOW_V`, `AO_MEDIUM_V` e `AO_HIGH_V` em `thresholds.h` após observar a cozinha real.
A tensão do AO depende do sensor, do resistor de carga do módulo e da temperatura/umidade; por isso
os limites valem para **este** módulo e precisam ser revistos se ele for trocado. O aquecimento de
5 min continua obrigatório: o sensor frio dá tensões altas. Com `false`, volta o modo por Rs/R0
(tabela acima), que exige `cal start`.

### Persistência temporal (histerese no tempo)

| Categoria | Para subir de nível | Para descer de nível |
|---|---|---|
| Qualidade do ar | 5 s | 60 s estável |
| Temperatura | 10 s | 60 s estável |
| Umidade | 30 s | 120 s estável |

O vapor do cozimento eleva a UR por alguns segundos, por isso a umidade exige mais tempo para
subir de nível.

### Limitações do DHT11 (sensor instalado)

- Mede apenas **20–90 %UR** (±5 %) e **0–50 °C** (±2 °C). Por isso os níveis de umidade baixa
  **MEDIUM (≤ 25 %) e HIGH (≤ 20 %) ficam no limite da faixa do sensor e não são confiáveis**.
  Abaixo de 20 %, o DHT11 não informa um valor válido.
- Com exatidão de ±2 °C, uma leitura de 26 °C pode ser 24–28 °C. A classificação é de
  **atenção**, não um diagnóstico.
- Para limites mais finos, use DHT22 ou AHT20 (`pio run -e rp2040zero_dht22` / `rp2040zero_aht20`).

## 2. Índice de calor

O índice de calor segue o procedimento do NWS. Primeiro calcula-se a fórmula simples
(consistente com Steadman). Se a média dela com T ficar ≥ 80 °F, usa-se a **regressão de
Rothfusz**, com os ajustes para UR < 13 % e UR > 85 %. A implementação é conferida contra
valores da tabela do NWS em `tests/unit/test_severity.cpp`. O índice não considera ventilação
nem radiação solar.

## 3. Plausibilidade dos sensores

Leituras fora de −40…80 °C ou 0…100 %UR são consideradas **inválidas**, assim como NaN,
timeout ou checksum errado. Elas **nunca** são tratadas como normais: a categoria mantém o
último nível confirmado. Três leituras inválidas seguidas (~7,5 s) colocam o sensor em
**falha** (SYSTEM, MEDIUM).
