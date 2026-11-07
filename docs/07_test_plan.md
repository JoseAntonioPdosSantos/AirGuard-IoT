# 07 — Plano de testes

O plano tem dois níveis:

1. **Testes unitários automatizados** no PC, sem hardware: `make -C tests`. São 84 testes
   compilados com AddressSanitizer e UndefinedBehaviorSanitizer.
2. **Testes experimentais** na placa, com os 23 cenários da especificação. Eles usam o **console
   serial** (`pio device monitor`, 115200) para simular sensores e rádio sem depender de gases
   reais.

## 1. Testes unitários (máquina de estados, classificação e protocolo)

| Área | Arquivo | O que cobre |
|---|---|---|
| Classificação e histerese | `test_severity.cpp` | níveis do ar, histerese de valor, temperatura nos dois lados, umidade, NaN não classificado, persistência (pico ignorado, oscilação, recuperação interrompida, recuperação parcial), **wrap-around de `millis()`**, índice de calor vs. NWS |
| Motor de alertas | `test_alert_manager.cpp` | normal sem eventos, HIGH aciona o relé, **MEDIUM→HIGH gera escalonamento imediato**, RECOVERY após estabilidade, **oscilação não gera tempestade**, leitura inválida mantém o estado, flags SYSTEM, lembretes, **restauração após reboot**, unicidade de `event_id` |
| Entrega / ACK | `test_ack_manager.cpp` | ACK entregue, **timeout de 60 s retransmite o mesmo id**, desiste após o máximo, intervalo mínimo entre TX, prioridade, substituição por categoria, canal ocupado, fila cheia, payload grande demais, deduplicação |
| Protocolo | `test_protocol.cpp` | campos da especificação, pior caso ≤ 240 B, overflow, ACK, parse do TEXT da especificação, escapes Unicode, rejeição de entradas malformadas, aninhadas, longas e com tipo errado, lista de remetentes |
| Rádio RYLR998 | `test_rylr998.cpp` | `+RCV` com JSON (vírgulas no dado), CR/LF dentro do dado, exemplo do guia com espaços, lixo e limites, `AT+SEND` (≤ 240 B, sem CR/LF), `+ERR`, tempo no ar |
| Sensores e display | `test_sensors_display.cpp` | cadeia de tensão/Rs, índice, aquecimento, falha (circuito aberto/saturação), mediana, calibração (sucesso/instável/recusada), simulação, falha do ambiente, padrões do buzzer, transliteração/quebra de texto, **mensagem exatamente 3 s**, paginação, **mensagem não esconde HIGH**, prioridade de tela |

## 2. Preparação dos testes experimentais

- Monte o hardware conforme `docs/01_hardware.md`, com o gateway (segundo dispositivo) na mesma
  configuração de rádio. Sem gateway, use `sim ack <id>` e `sim text <msg>`.
- Para acelerar a bancada: `warmup skip` (só em teste) e `r0 <valor>` (ou `cal start` em ar limpo).
- `log debug` mostra os payloads enviados. `status` e `stats` mostram o estado e os contadores.
- Registre para cada teste: data, firmware (`v1.0.0`), condições (T/UR), resultado e trecho do log.

## 3. Cenários experimentais

| # | Cenário | Procedimento | Resultado esperado / critério |
|---|---|---|---|
| 1 | Inicialização | Energizar e observar o log e o OLED | Relé desligado desde o 1º instante; bipe curto; tela de boot de 3 s com nome e slogan; log `BOOT` com versão, boot #, R0; `SSD1306 ok`; após ~1 s, `RYLR998 ok: 915000000 Hz SF8 ...`; LED verde aceso; LED amarelo piscando devagar durante o aquecimento |
| 2 | Aquecimento e estabilização do MQ-135 | Energizar com sensor já com burn-in e registrar `Rs` (STAT, 10 s) por 30 min | OLED `WARMUP mm:ss` por 5 min; nenhum alerta AIR durante o aquecimento. **Medir** o tempo até Rs variar < 2 %/min e ajustar `MQ135_WARMUP_MS` |
| 3 | Ambiente normal | Calibrado e em ar limpo, 22–25 °C, 40–55 %UR, observar 10 min | Tela principal `NORMAL`/`MONITORING`; LED verde aceso, amarelo e vermelho apagados; buzzer e relé desligados; nenhum TX |
| 4 | Alteração inicial do ar | `sim air 20` | Após 5 s: ALERT AIR_QUALITY LOW; 2 bipes/30 s; tela de alerta fixa; LED amarelo aceso; relé desligado |
| 5 | Alteração média do ar | `sim air 35` | Após 5 s: ALERT MEDIUM (`prev` LOW); 3 bipes/5 s; severidade piscando; LED amarelo piscando; relé desligado |
| 6 | Alteração crítica do ar | `sim air 60` | Após 5 s: ALERT HIGH, **transmitido antes de qualquer outra mensagem pendente** (o MEDIUM sem ACK é substituído); buzzer contínuo/urgente; tela inteira piscando; **relé energizado; LED verde apagado** (contato NC aberto); LED vermelho piscando |
| 7 | Temperatura acima da atenção | `sim temp 28` e `sim hum 50` | Após 10 s: ALERT TEMPERATURE LOW (aparente ≥ 26,7 °C). `sim temp 33` → MEDIUM |
| 8 | Temperatura acima do crítico | `sim temp 41` | Após 10 s: ALERT TEMPERATURE HIGH; relé energizado; tela mostra `Temp` e `Feels` |
| 9 | Umidade abaixo da faixa | `sim temp 22`, `sim hum 28` | Após 30 s: ALERT HUMIDITY LOW; `sim hum 18` → HIGH. Fisicamente, o DHT11 não mede < 20 %UR (doc 05), então só a simulação cobre MEDIUM/HIGH baixos |
| 10 | Umidade acima da faixa | `sim hum 65` → LOW, `sim hum 75` → MEDIUM, `sim hum 90` → HIGH | Cada nível após 30 s de persistência |
| 11 | Recebimento de mensagem LoRa | Gateway envia o TEXT da especificação (ou `sim text Atenção: verifique a cozinha.`) | Log `RX TEXT #205 from WHATSAPP_GATEWAY`; acentos transliterados; **ACK enviado** (`ack_for`=205) |
| 12 | Exibição por 3 s | Cronometrar com vídeo (≥ 30 fps) | Página visível 3,0 s (± 1 quadro de display, 250 ms) |
| 13 | Restauração da tela principal | Após o teste 12, com estado normal | Volta automaticamente à tela principal (ou ao alerta ativo). Com HIGH ativo: faixa piscante durante a mensagem e ≥ 3 s de tela de alerta entre mensagens |
| 14 | Recebimento de ACK | Gerar alerta (teste 4) com o gateway respondendo | Log `ACK for event #...`; OLED `LoRa:ACK OK`; `stats`: `delivered` +1 |
| 15 | Ausência de ACK por 60 s | Desligar o gateway e gerar alerta | `waitACK=1` por 60 s; **o monitoramento e o display continuam normais** durante a espera |
| 16 | Retransmissão | Continuação do 15 | Aos 60 s: `retransmission 1 of id N`, **mesmo `event_id`**; repete até 5 vezes; depois `NOT delivered`, OLED `NO ACK`, flag SYSTEM 16 |
| 17 | Mensagens duplicadas | Gateway reenvia o mesmo TEXT (`message_id` igual) | 2º recebimento: log `duplicate TEXT`, **não exibido de novo**, **ACK reenviado**. Lado do nó: o gateway recebe retransmissões com o mesmo `event_id` (deduplicar lá) |
| 18 | Perda temporária do LoRa | `sim lorafail`, ou desconectar o TXD do RYLR998 (o `AT` de saúde falha em ≤ 60 s) | Log `RYLR998 fault`; OLED `LoRa: FAULT`/`NO RADIO`; **sensores, alertas, buzzer e relé continuam**; após 30 s o rádio é reinicializado e a fila retoma |
| 19 | Falha do sensor de temperatura | `sim envfail on` (ou desconectar DATA) | Após 3 leituras: `ENV FAULT`; ALERT SYSTEM MEDIUM (flags 1); OLED `--.-`/`SENSOR FAULT`; o nível de TEMP/HUM **não volta a NORMAL**. `sim envfail off` → recupera |
| 20 | Falha de leitura do MQ-135 | `sim airfail on` (ou desconectar AO → ADC ≈ 0) | Após 5 s: `MQ-135 FAULT`; ALERT SYSTEM MEDIUM (flags 2); nível AIR mantido |
| 21 | Ativação e desativação do relé | Teste 6 e depois `sim air 5` | Relé liga no HIGH (medir na saída do módulo / LED do relé); desliga **60 s** após o índice ficar abaixo de HIGH − histerese; contador em `stats`/log `RELAY` |
| 22 | Recuperação da condição crítica | `sim air 60`, aguardar o HIGH, depois `sim air 3` | Após 60 s estáveis: **RECOVERY** AIR_QUALITY (`prev` HIGH) com ACK; buzzer para; relé liberado e LED verde aceso; LEDs amarelo/vermelho apagados; tela principal |
| 23 | Reinicialização durante alerta | Em HIGH (teste 6), digitar `reboot` (ou cortar a energia) | No boot: log `restored HIGH state`; relé e buzzer reativados; ALERT `note: RESTORED` enviado; com `sim air` desligado e o sensor em ar limpo, a recuperação só ocorre **após o aquecimento + 60 s** |

Para todos os cenários, após o teste, use `sim off` e verifique o retorno ao normal.

## 4. Testes de integração adicionais recomendados

- **Alcance:** RSSI/SNR no log do gateway a 5, 10 e 20 m e através de 1 e 2 paredes.
- **Robustez:** 24 h de operação contínua; `stats` sem `dropped` e sem reboot por watchdog.
- **Tempestade:** `sim air` oscilando entre 14 e 16 a cada 2 s por 10 min deve gerar no máximo
  1 evento (histerese).
- **Consumo:** medir a corrente no 5 V em NORMAL e em HIGH com TX (comparar com doc 01 §5).
