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
| TEMPERATURE_NORMAL | 18,0 °C ≤ T e aparente < 29,0 °C | NORMAL | WHO (mínimo de 18 °C); ASHRAE 55 (conforto) |
| TEMPERATURE_WARNING (quente) | aparente ≥ 29,0 °C | LOW | Ajuste do usuário: a NOAA/NWS começa o "Caution" em 26,7 °C (80 °F), mas o ambiente fica em ~27 °C de sensação térmica e o alerta ficaria aceso o tempo todo |
| TEMPERATURE_WARNING (quente) | aparente ≥ 32,2 °C (90 °F) | MEDIUM | NOAA/NWS "Extreme caution" |
| TEMPERATURE_CRITICAL (quente) | aparente ≥ 39,4 °C (103 °F) | HIGH | NOAA/NWS "Danger" |
| TEMPERATURE_WARNING (frio) | T ≤ 18,0 °C | LOW | WHO Housing and Health Guidelines (2018) |
| TEMPERATURE_WARNING (frio) | T ≤ 16,0 °C | MEDIUM | Cold Weather Plan for England (risco respiratório abaixo de 16 °C) |
| TEMPERATURE_CRITICAL (frio) | T ≤ 12,0 °C | HIGH | Cold Weather Plan for England (sobrecarga cardiovascular entre 9 e 12 °C) |
| Histerese | 1,0 °C | — | escolha de projeto; o DHT11 tem resolução de 1 °C (exatidão de ±2 °C) |

### Umidade relativa

| Constante (conceito) | Faixa | Severidade | Referência |
|---|---|---|---|
| HUMIDITY_NORMAL | 30 % < UR < 75 % | NORMAL | EPA: manter UR abaixo de 60 %, idealmente entre 30 e 50 % |
| HUMIDITY_WARNING (alta) | UR ≥ 75 % | LOW | EPA recomenda < 60 % (mofo); elevado para 75 % a pedido do usuário, porque a UR ambiente local fica em 65–70 % |
| HUMIDITY_WARNING (alta) | UR ≥ 80 % | MEDIUM | UR sustentada > 70–80 % favorece mofo em superfícies. Era 70 %; elevado a pedido do usuário porque a UR ambiente local fica perto de 70 % |
| HUMIDITY_CRITICAL (alta) | UR ≥ 85 % | HIGH | **protótipo:** condensação provável (vapor intenso, vazamento) |
| HUMIDITY_WARNING (baixa) | UR ≤ 30 % | LOW | EPA (limite inferior recomendado) |
| HUMIDITY_WARNING (baixa) | UR ≤ 25 % | MEDIUM | **protótipo:** ressecamento e irritação |
| HUMIDITY_CRITICAL (baixa) | UR ≤ 20 % | HIGH | **protótipo** |
| Histerese | 3 %UR | — | escolha de projeto; o DHT11 tem resolução de 1 % e exatidão de ±5 % |

### Gás: tensão do AO do MQ-135 (`thr::GAS_VOLTS`)

O alarme usa a **tensão na saída AO do MQ-135** (0–5 V, com o divisor desfeito; filtro no doc 04).
No rádio ela vai como `air_index = 100 × V(AO) / 5 V` (a tensão em porcentagem de 5 V).

| Tensão no AO | `air_index` | Severidade |
|---|---|---|
| < 0,90 V | < 18 | NORMAL |
| ≥ 0,90 V | ≥ 18 | **MEDIUM** (não há LOW para gás, a pedido do usuário) |
| ≥ 1,5 V | ≥ 30 | HIGH |
| histerese | 0,05 V (1 ponto) | |

Valores definidos pela bancada: ar limpo ~0,2–0,6 V (com 0,30 V o MEDIUM disparava sozinho, por
isso subiu para 0,90 V); o HIGH subiu de 1,0 V para 1,5 V para que a faixa MEDIUM (0,90–1,5 V)
sirva de aviso antecipado; isqueiro perto do sensor leva o AO a quase 5 V. Ajuste
`thr::GAS_VOLTS` em `thresholds.h` após observar a cozinha real (doc 04 §4). A tensão do AO
depende do sensor, do resistor de carga do módulo e da temperatura/umidade; por isso os limites
valem para **este** módulo e precisam ser revistos se ele for trocado. O aquecimento de 5 min
continua obrigatório em uso real: o sensor frio dá tensões altas.

### Persistência temporal (histerese no tempo)

| Categoria | Para subir de nível | Para descer de nível |
|---|---|---|
| Qualidade do ar | **imediato** (era 5 s; leitura a 5 Hz, subida sem filtro) | **imediato** na bancada (`AIR_RECOVER_MS = 0`; use ~60 s em uso real) (era 60 s; reduzido a pedido do usuário) |
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
- Para limites mais finos seria preciso um sensor melhor (ex.: DHT22 ou AHT20). O suporte a eles
  foi retirado do firmware para simplificar; a versão anterior está na branch `esp32-devkit`.

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
