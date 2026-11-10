# 03 — LoRa: configuração do rádio e protocolo de comunicação

## 1. Configuração do rádio: REYAX RYLR998 (`src/config/config.h`)

O RYLR998 tem um SX1262 e um microcontrolador próprio, e é controlado por **comandos AT pela
UART** (115200 baud, `Serial2` do ESP32 em GPIO17/GPIO16). A cada boot e a cada recuperação de falha, o
firmware faz:

1. **Reset por hardware:** NRST (GPIO4) em nível baixo por 100 ms, depois espera `+READY` (até 3 s).
2. **Configuração**, um comando por vez, cada um aguardando `+OK`:

| Comando enviado | Valor | Justificativa |
|---|---|---|
| `AT` | — | Testa a comunicação |
| `AT+MODE=0` | transceptor | Recepção contínua |
| `AT+ADDRESS=101` | `LORA_ADDRESS` | Endereço deste nó |
| `AT+NETWORKID=6` | `LORA_NETWORK_ID` | O padrão de fábrica 18 é evitado, para não conversar com módulos alheios na configuração de fábrica |
| `AT+BAND=915000000` | 915,0 MHz | Subfaixa de 915–928 MHz de radiação restrita no Brasil |
| `AT+PARAMETER=8,7,1,12` | SF8, BW 125 kHz, CR 4/5, preâmbulo 12 | A REYAX recomenda `8,7,1,12` para payloads acima de 100 bytes (os nossos têm ~200). Com NETWORKID diferente de 18, o preâmbulo **precisa** ser 12. |
| `AT+CRFOP=14` | +14 dBm | A potência padrão do módulo é 22 dBm. O próprio guia indica ≤ 14 dBm para conformidade CE; aqui é uma escolha conservadora (ver §2). |
| `AT+CPIN=<8 hex>` | `LORA_CPIN` (opcional) | "Senha de domínio": só módulos com a mesma senha reconhecem os dados. O guia não descreve o mecanismo, então isso não deve ser tratado como criptografia validada. Ela é perdida no reset, por isso é reenviada a cada boot. |

- Os quadros são enviados com `AT+SEND=<LORA_DEST_ADDRESS>,<len>,<json>`. O padrão é o
  endereço **0 (broadcast)** para todos os módulos da mesma NETWORKID. O endereçamento lógico
  fica no campo JSON `dst`.
- **TX concluída** quando o módulo respondeu `+OK` **e** já passou o tempo no ar calculado
  (fórmula da Semtech com +8 bytes de overhead do módulo). Isso evita `+ERR=17` ("last TX was
  not completed") no envio seguinte.
- **Saúde:** sem tráfego por 60 s, o firmware envia `AT`. Sem resposta em 1,5 s, ou com
  timeout na configuração ou em `AT+SEND`, o rádio entra em **Fault**: o monitoramento continua
  e há um novo reset e uma nova configuração a cada 30 s. Um `+READY` inesperado (o módulo
  reiniciou sozinho) faz o firmware reconfigurar o módulo.
- **O gateway precisa usar exatamente** BAND, PARAMETER, NETWORKID e CPIN iguais. O endereço dele
  deve ser outro (ex.: 102).
- Não há CAD/listen-before-talk: o RYLR998 não expõe essa função por AT. Colisões são tratadas
  pelo ACK, pela retransmissão e pelo intervalo mínimo de 2 s entre transmissões.

### Tempo no ar (SF8, BW125, CR4/5, preâmbulo 12, +8 B de overhead)

| Mensagem | Tamanho típico | Tempo no ar aprox. |
|---|---|---|
| ALERT / RECOVERY | 200–240 bytes | 0,6–0,7 s |
| ACK | ~80 bytes | ~0,25 s |

## 2. Observações regulatórias (ANATEL) — **[PENDENTE de verificação formal]**

- A faixa de 915–928 MHz é de **radiação restrita**: dispensa licença, mas o equipamento precisa
  de **certificação/homologação ANATEL**. **Use um módulo LoRa que já tenha certificado ANATEL.**
- A ANATEL revogou, pelo Ato nº 14.158, a isenção do item 10.3.4.4, que dispensava os sistemas
  de "espalhamento espectral ou outras tecnologias de modulação digital" (como LoRa) da largura
  de banda mínima de 6 dB. **A partir de 06/10/2026**, novas certificações nas faixas
  902–907,5 MHz e 915–928 MHz precisam atender ao item 10.3.1, com banda mínima de 6 dB de
  **500 kHz**, e as emissões espúrias passam a 30 dBc. Equipamentos certificados antes dessa
  data continuam válidos (fonte: IB-Lenhardt, ver referências).
  - **Consequência para este projeto:** o firmware usa BW = 125 kHz, adequado para um protótipo
    de bancada com módulo já homologado. Para um **produto a certificar**, avaliar
    `LORA_BW_KHZ = 500.0f`. Essa configuração também precisa ser trocada no gateway e reduz o
    alcance em cerca de 6 dB, mas o tempo no ar cai a ~1/4. Confirme no texto vigente do Ato
    nº 14.448 e alterações qual é o limite de potência/EIRP para a sua configuração. **Esses
    números não foram confirmados nesta documentação e não devem ser presumidos.**
- O firmware configura +14 dBm (`AT+CRFOP=14`). A EIRP final depende do ganho da antena.
- **[PENDENTE]** Verifique se o seu RYLR998 tem certificado ANATEL.

## 3. Protocolo de aplicação v1

O formato é **JSON compacto, plano** (sem objetos ou arrays aninhados), em UTF-8, com no
máximo **240 bytes** (limite do `AT+SEND` do RYLR998; `+ERR=13` acima disso). O campo `"v"` indica a versão.

### 3.1 Endereçamento

- `device`: identificador do **remetente** (`KITCHEN_01` neste nó).
- `dst`: identificador do destinatário. O nó aceita quadros com `dst` igual ao próprio ID,
  `"*"` (broadcast) ou sem `dst`. Os demais são ignorados.
- Os alertas são enviados para `cfg::GATEWAY_ID` (`GATEWAY_01`).
- Só são aceitas mensagens `TEXT` de remetentes da lista `cfg::ALLOWED_TEXT_SOURCES`
  (`WHATSAPP_GATEWAY`, `GATEWAY_01`).

### 3.2 Mensagens enviadas pelo nó

#### ALERT (exige ACK)

Exemplo real gerado pelo codificador do firmware (239 bytes):

```json
{"v":1,"type":"ALERT","device":"KITCHEN_01","dst":"GATEWAY_01","event_id":458764,
 "category":"AIR_QUALITY","severity":"HIGH","prev":"MEDIUM","sensor":"MQ135","raw_adc":2850,
 "air_index":85,"temperature":32.5,"humidity":68,"timestamp":123456}
```

| Campo | Tipo | Descrição |
|---|---|---|
| `v` | int | Versão do protocolo (1) |
| `type` | string | `ALERT` |
| `device`, `dst` | string | Remetente e destinatário |
| `event_id` | uint32 | Identificador único: `(boot_count << 16) \| sequência`. Uma retransmissão **mantém o mesmo** `event_id`. |
| `category` | string | `AIR_QUALITY`, `TEMPERATURE`, `HUMIDITY`, `SYSTEM` |
| `severity` | string | `LOW`, `MEDIUM`, `HIGH` |
| `prev` | string | Severidade anterior (opcional, omitido se não couber) |
| `sensor` | string | `MQ135`, `DHT11`, `SYSTEM` |
| `raw_adc` | uint | Leitura ADC (0–4095) do MQ-135 |
| `air_index` | uint | Tensão do AO do MQ-135 usada pelo alarme, em **% de 5 V** (0–100; `V = air_index / 20`). Resposta relativa: **não é concentração química.** Omitido durante o aquecimento ou com o sensor em falha. |
| `temperature` | float | °C, 1 casa decimal. Omitido se a leitura for inválida. |
| `humidity` | float | %UR, inteiro. Omitido se a leitura for inválida. |
| `heat_index` | float | °C (só em eventos de TEMPERATURE, se couber) |
| `flags` | uint | Só em SYSTEM: bitmask (ver abaixo) |
| `note` | string | `REMINDER` (lembrete periódico) ou `RESTORED` (estado restaurado após reboot) |
| `timestamp` | uint | **Segundos desde o boot** (o nó não tem relógio de tempo real) |

Se a mensagem completa passar de 240 bytes, o codificador remove primeiro os diagnósticos
opcionais (`prev`, `heat_index`, `raw_adc` de outras categorias) e depois `v`, `sensor` e
`note`. Os campos obrigatórios sempre cabem.

#### RECOVERY (exige ACK)

O formato é o mesmo, com `"type":"RECOVERY"` e `"severity":"NORMAL"`. É enviado quando a
categoria volta ao normal depois da histerese temporal de recuperação.

```json
{"v":1,"type":"RECOVERY","device":"KITCHEN_01","dst":"GATEWAY_01","event_id":458766,
 "category":"AIR_QUALITY","severity":"NORMAL","prev":"HIGH","sensor":"MQ135","raw_adc":2850,
 "air_index":4,"temperature":32.5,"humidity":68,"timestamp":123456}
```

#### SYSTEM: flags

| Bit | Valor | Condição | Severidade |
|---|---|---|---|
| 0 | 1 | Falha do sensor de temperatura/umidade | MEDIUM |
| 1 | 2 | Falha de leitura do MQ-135 | MEDIUM |
| 2 | 4 | *(não usado; era "MQ-135 sem calibração R0", modo removido)* | — |
| 3 | 8 | Falha do rádio LoRa (só pode ser enviada depois que o rádio voltar) | LOW |
| 4 | 16 | Alertas sem ACK (falha de entrega) | LOW |
| 5 | 32 | Falha do display | LOW |

#### HB, heartbeat "estou vivo" (não exige ACK)

Enviado a cada `cfg::HEARTBEAT_PERIOD_MS` (5 s; 0 desliga), com a **menor prioridade**: qualquer
alerta sai antes, e só o heartbeat mais recente fica na fila. Leva o nível atual de cada
categoria em `levels` (4 letras na ordem AIR, TEMP, HUM, SYS: `N`/`L`/`M`/`H`) e as leituras ao vivo.
O receptor usa o heartbeat para atualizar o LCD, ressincronizar o estado se tiver perdido um
evento e detectar **perda de sinal** (sem quadros por 15 s).

```json
{"v":1,"type":"HB","device":"KITCHEN_01","dst":"GATEWAY_01","seq":12,"levels":"NNLN",
 "air_index":5,"temperature":25.4,"humidity":61,"timestamp":60}
```

Tempo no ar: ~0,4 s em SF8, ou seja, ~8 % do tempo transmitindo com período de 5 s. Para uso real,
um período de 30–60 s reduz a ocupação do canal e o consumo.

#### ACK (resposta, não exige ACK)

```json
{"v":1,"type":"ACK","device":"KITCHEN_01","dst":"WHATSAPP_GATEWAY","ack_for":205}
```

### 3.3 Mensagens recebidas pelo nó

#### TEXT (exibida no OLED por 3 s; o nó responde com ACK)

```json
{"type":"TEXT","source":"WHATSAPP_GATEWAY","message_id":205,"text":"Atenção: verifique a cozinha.","dst":"KITCHEN_01"}
```

| Campo | Obrigatório | Regra |
|---|---|---|
| `type` | sim | `TEXT` |
| `source` (ou `device`) | sim | ≤ 23 bytes, presente em `ALLOWED_TEXT_SOURCES` |
| `message_id` | sim | inteiro sem sinal (32 bits) |
| `text` | sim | não vazio, **≤ 120 bytes UTF-8**. Escapes JSON (`\n`, `\uXXXX`) são aceitos e os de controle viram espaço. Caractere de controle literal (byte < 0x20) torna o quadro inválido. |
| `dst` | não | se presente: `KITCHEN_01` ou `*` |
| `v` | não | ignorado |

#### ACK (confirma um evento do nó)

```json
{"type":"ACK","device":"GATEWAY_01","dst":"KITCHEN_01","ack_for":458764}
```

### 3.4 Validação e segurança

O parser rejeita e registra no log (`RX frame rejected (...)`) os seguintes casos:

- quadro vazio ou com mais de 240 bytes (`EMPTY`, `TOO_LONG`);
- qualquer coisa que não seja um único objeto JSON plano: objeto ou array aninhado (`NESTED`),
  vírgula sobrando, chave duplicada, lixo após o `}` (`MALFORMED`);
- campo string acima do limite (`FIELD_TOO_LONG`) ou tipo errado, por exemplo `message_id` como
  string ou negativo (`BAD_VALUE`);
- `type` desconhecido (`UNKNOWN_TYPE`) ou campo obrigatório ausente (`MISSING_FIELD`).

O conteúdo recebido é **apenas dado**: nada é executado e nenhum campo é interpretado como
comando. O texto é convertido para ASCII exibível (acentos do português são transliterados,
como em "Atenção" → "Atencao") antes de ir ao display. A integridade do quadro é verificada
pelo CRC do LoRa: o módulo descarta quadros corrompidos e informa `+ERR=12`/`+ERR=19`, contados como erros de RX.

> **Limitação:** não há autenticação criptográfica na aplicação. Um transmissor com a mesma
> configuração (e o mesmo CPIN, se usado) pode forjar `source`. Ver `docs/08_limitations.md`.

## 4. Entrega confiável (ACK)

1. Uma condição confirmada gera um `AlertEvent` com `event_id` único.
2. O evento é codificado e entra na **fila de prioridade** (8 posições, sem alocação dinâmica):
   ACK = 6, HIGH = 5, MEDIUM = 4, LOW = 3, RECOVERY = 2.
3. **Substituição por categoria:** um novo estado da mesma categoria substitui a mensagem
   anterior ainda não confirmada. Exemplo: MEDIUM ainda sem ACK → HIGH, e só o HIGH segue.
   Isso evita tempestades de mensagens e garante que a mudança de MEDIUM para HIGH seja enviada
   de imediato.
4. Antes do TX, respeita-se um intervalo mínimo de 2 s desde o último TX. Se o módulo estiver
   ocupado (configurando ou respondendo ao `AT` de saúde), aplica-se um backoff aleatório de
   400–800 ms, que não conta como tentativa.
5. Depois do TxDone, a mensagem aguarda o ACK por **60 s**, sem bloquear: o monitoramento
   continua.
6. Sem ACK, a mensagem é **retransmitida** com o mesmo payload e `event_id`, até **5
   retransmissões** (6 transmissões no total).
7. Esgotadas as tentativas, a falha é registrada no log, o OLED mostra `NO ACK` e a flag SYSTEM
   16 é ativada.
8. Um ACK duplicado ou atrasado é inofensivo (ignorado e contado).
9. **Deduplicação na recepção:** o nó guarda os últimos 16 pares (remetente, `message_id`). Um
   TEXT repetido **recebe um novo ACK** (o remetente provavelmente perdeu o primeiro), mas não é
   exibido de novo.
10. **Lembretes:** enquanto uma categoria estiver em MEDIUM ou HIGH, um novo ALERT com
    `"note":"REMINDER"` e novo `event_id` sai a cada 15 min. Esse é o único envio periódico.

## 5. Guia para o segundo dispositivo (gateway / WhatsApp)

O gateway deve:

1. Usar a mesma configuração de rádio (§1).
2. Ao receber `ALERT` ou `RECOVERY` com `dst` igual ao seu ID: **responder imediatamente**
   `{"type":"ACK","device":"GATEWAY_01","dst":"KITCHEN_01","ack_for":<event_id>}`, por
   exemplo com `AT+SEND=101,<len>,<json>` ou para o endereço 0.
3. Deduplicar por `(device, event_id)`. Uma retransmissão traz o mesmo `event_id` e deve ser
   reconfirmada, mas não reencaminhada.
4. Para enviar texto: montar o `TEXT` acima, com `message_id` crescente, e aguardar o `ACK` com
   `ack_for = message_id`. Se o ACK não vier, retransmitir com o **mesmo** `message_id`.
5. Limitar `text` a 120 bytes UTF-8 e escapar aspas e barras (`\"`, `\\`) conforme o JSON.
   O RYLR998 documenta o dado como ASCII, então **envie acentos como `\uXXXX`** (ex.: `Aten\u00e7\u00e3o`).
   Isso garante a passagem pelo módulo. O nó converte para ASCII exibível. Nunca envie CR/LF
   crus dentro do dado.
