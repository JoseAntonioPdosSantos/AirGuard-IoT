# 04 — MQ-135: medição, calibração e interpretação

## 1. O que o MQ-135 mede (e o que não mede)

O MQ-135 é um sensor semicondutor de óxido metálico (SnO₂) de **sensibilidade ampla**. A
resistência Rs cai na presença de vários gases e vapores (amônia, álcool, benzeno, fumaça, CO₂
em alta concentração, vapores de cozinha etc.), mas **ele não identifica a substância**.

Por isso, o firmware:

- usa a categoria **`AIR_QUALITY`** e o **`air_index`**, uma **resposta relativa** do sensor
  em relação ao ar limpo de referência;
- **não informa ppm** de nenhum gás e **não diz** "GLP", "CO₂" ou "amônia detectados";
- registra no log, a cada 10 s, a leitura ADC, a tensão no pino, a tensão em AO, Rs, Rs/R0 e
  o índice.

## 2. Cadeia de sinal (`src/monitoring/air_quality_monitor.cpp`)

```
raw (média de 16 conversões, 12 bits)
  → V_pino  = raw / 4095 × 3,30
  → V_AO    = V_pino × (R_TOP + R_BOTTOM) / R_BOTTOM        (desfaz o divisor)
  → Rs      = RL × (Vc − V_AO) / V_AO                        (Vc = 5,0 V)
  → mediana de 5 amostras (rejeita picos) → EMA α = 0,25 (suaviza)
  → razão  = Rs_filtrado / (R0 × f(T,UR))                    (f = 1 se a compensação estiver desligada)
  → air_index = 100 × (1 − razão), limitado a [0, 100]
```

- `air_index = 0` significa resposta igual (ou menor) à do ar de referência.
- `air_index = 30` significa Rs 30 % abaixo de R0.
- **Leitura inválida:** ADC < 8 (sensor desconectado), ADC > 4085 (saturação) ou Rs ≤ 0.
  Cinco amostras inválidas seguidas (~5 s) colocam o sensor em **falha** (SYSTEM, MEDIUM).
  Durante a falha, o nível de AIR_QUALITY **não volta a NORMAL**: ele mantém o último estado
  confirmado.

## 3. Pré-aquecimento

| Situação | Exigência | Fonte |
|---|---|---|
| Sensor **novo** (burn-in) | > 24 h (Hanwei) / > 48 h (Winsen) energizado | datasheets |
| Sensor armazenado por meses | 72 h (1–6 meses) a 168 h (> 6 meses), segundo um resumo de terceiros | ver referências |
| A cada energização | estabilização de minutos | prática; **medir** |

- O firmware ignora o MQ-135 para fins de alerta durante **`MQ135_WARMUP_MS` = 5 min** após o
  boot. O OLED mostra `Air: WARMUP mm:ss` e o LED verde pisca rápido.
- **Meça o tempo de estabilização do seu sensor** (teste 2 do plano) e ajuste o valor em
  `config.h`. O critério sugerido é Rs variar menos de 2 % em 1 minuto.

## 4. Procedimento de calibração (R0 em ar limpo)

**Pré-requisitos:** sensor com burn-in concluído (≥ 24–48 h na primeira vez), aquecimento de
pelo menos 5 min, ambiente ventilado **sem** fontes de gás (fogão desligado, sem álcool, sem
produtos de limpeza), temperatura de 18 a 26 °C e umidade de 30 a 60 %. Anote as condições.

1. Conecte a USB e abra o monitor serial (`pio device monitor`, 115200).
2. Espere o fim do aquecimento (`status` → `air=UNCAL`, sem `WARMUP`).
3. Digite `cal start`. Durante **60 s**, o firmware acumula Rs (normalizado pela compensação,
   se ela estiver ativa).
4. O resultado é **aceito** se houver ≥ 30 amostras, o **coeficiente de variação for ≤ 5 %** (ar
   estável) e R0 estiver entre 500 Ω e 2 MΩ. Nesse caso, R0 é salvo em flash
   (`CAL done: R0 = ... ohm`).
5. Ele é **rejeitado** se o CV passar de 5 % (`Unstable`), se ocorrer falha do sensor ou se R0
   for implausível. Nesses casos, verifique o ambiente e o valor de RL e repita.
6. `cal status` mostra o progresso. `cal abort` cancela.
7. Para usar um R0 medido anteriormente, digite `r0 <ohms>`.

**Recalibre** a cada 1 a 3 meses, depois de trocar o sensor e quando a leitura em ar limpo
mostrar `air_index` > 5 de forma persistente, o que indica deriva ou envelhecimento.

## 5. Definição experimental dos limites (`thr::AIR_QUALITY`)

Os valores iniciais (LOW 15, MEDIUM 30, HIGH 50, histerese 5) são **apenas pontos de partida**
e **não representam limites toxicológicos**. Eles devem ser definidos no local de instalação:

1. Com o sensor calibrado, registre o `air_index` (log STAT) durante uma semana de uso normal da
   cozinha: cozimento, fritura, fervura, limpeza.
2. **LOW**: logo acima da faixa de variação do ar "normal" da cozinha sem atividade (ex.:
   percentil 99 do ar parado + 5).
3. **MEDIUM**: resposta típica a uma atividade intensa, mas esperada (fritura com exaustor
   ligado). O objetivo é avisar sem alarmar.
4. **HIGH**: resposta a eventos que de fato exigem ação (fumaça densa de queimado, vapor de
   produto químico concentrado). Use testes **controlados e seguros**, por exemplo um algodão
   com álcool isopropílico a uma distância fixa. **Nunca libere GLP ou outro gás inflamável
   para testar.**
5. Escolha uma histerese maior que o ruído do índice em regime (desvio padrão × 3).
6. Edite `src/config/thresholds.h`, recompile e documente os valores e as condições no
   relatório.

## 6. Temperatura, umidade, envelhecimento e deriva

- Rs depende de T e UR: os datasheets mostram curvas de Rs/R0 entre −10 e 50 °C para 33 % e
  85 %UR. O firmware inclui a **compensação polinomial** da biblioteca MQ135 da comunidade
  (GeorgK), ajustada a essas curvas, mas ela fica **desligada** por padrão
  (`mq135::COMPENSATION_ENABLED = false`) porque **não foi validada** com o seu sensor. Para
  ativá-la, compare o `air_index` em ar limpo a, por exemplo, 20 °C/40 % e 30 °C/70 %. Se a
  compensação reduzir a variação, mantenha-a.
- **Envelhecimento e deriva:** R0 muda ao longo de meses, por isso a recalibração é periódica.
  Uma subida lenta do índice em ar limpo indica deriva, não contaminação.
- **Aquecimento local:** o próprio aquecedor do MQ-135 aquece o entorno. Mantenha o sensor de
  T/UR afastado.
