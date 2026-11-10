# 08 — Limitações conhecidas

## Segurança e uso

- **Não é um detector certificado** de gás, fumaça ou incêndio, nem um equipamento de segurança.
  Não substitui detectores homologados (por exemplo, detector de GLP/gás natural com válvula de
  corte, detector de fumaça, detector de CO). É um protótipo de estudo de monitoramento
  ambiental.
- O MQ-135 **não identifica gases** e **não mede concentração**. O alarme usa a tensão do AO,
  uma resposta relativa do sensor. O MQ-135 também **não é adequado para detectar CO ou GLP** com
  confiabilidade de segurança, porque sensores específicos e certificados existem para isso.
- Os limites de temperatura e umidade são de conforto e atenção para um protótipo, **não**
  limites médicos.
- O **DHT11** tem ±2 °C / ±5 %UR e não mede abaixo de 20 %UR. Os níveis de umidade baixa
  MEDIUM/HIGH não são confiáveis com ele.

## Sensor de gás

- Exige burn-in de 24–48 h e alguns minutos de aquecimento a cada energização. O valor de
  5 min no firmware precisa ser medido.
- Sofre deriva e envelhecimento: a tensão em ar limpo muda com o tempo, e os limites em volts
  precisam ser revistos periodicamente (doc 04).
- Não há compensação de temperatura e umidade.
- Os limites de tensão foram definidos na bancada e precisam de ajuste experimental no local.
- Com `MQ135_FLOOR_IS_FAULT = false` (bancada), um fio do AO solto **não é detectado** (doc 01 §4.2).
- O aquecedor consome ~0,8 W de forma contínua. Não é adequado para alimentação por bateria.

## Hardware ainda não confirmado

- Relé ainda não ligado nem testado, volume do buzzer ativo ligado direto no GPIO, homologação
  ANATEL do RYLR998, consumo total (ver `docs/01_hardware.md` §6).
- O LED verde depende do contato NC do relé: ele indica "relé desligado", não "firmware
  rodando".
- O nó roda na bancada (OLED, DHT11, MQ-135, LEDs, buzzer e RYLR998 com o receptor), mas os 23
  testes experimentais formais ainda estão pendentes.

## Comunicação

- **Sem autenticação na aplicação.** O `AT+CPIN` do RYLR998 filtra módulos com outra senha, mas
  o mecanismo não é documentado e não foi validado. Qualquer rádio na mesma configuração pode enviar um
  `TEXT` com `source` forjado ou um `ACK` falso. Isso é mitigado apenas por lista de remetentes,
  endereçamento, validação estrita e pelo fato de o conteúdo nunca ser executado. Para produção:
  HMAC (ex.: SHA-256 truncado, com chave pré-compartilhada e contador anti-replay) ou AES-CCM.
- O ACK confirma que o **gateway** recebeu, não que uma pessoa leu (WhatsApp).
- `timestamp` é o tempo desde o boot (não há RTC). O gateway deve registrar a hora de recepção.
- Um quadro LoRa de ~240 bytes ocupa ~0,7 s de canal com SF8/BW125. Com muitos nós, o canal
  satura (não há TDMA nem LoRaWAN).
- Regras da ANATEL para novas certificações (banda mínima de 6 dB de 500 kHz a partir de
  06/10/2026) podem exigir BW 500 kHz em um produto (ver doc 03 §2).
- Uma mensagem só é entregue com até 6 transmissões em ~6 min. Depois disso, a falha é
  registrada, mas não há outro canal de comunicação.

## Firmware

- O envio de um quadro ao OLED leva ~23 ms bloqueantes (biblioteca Adafruit) e a leitura do
  DHT11 ~25 ms. Ambos são compatíveis com o watchdog de 8 s e com os tempos dos alertas, mas não
  são "zero bloqueio".
- Não há listen-before-talk (o RYLR998 não o expõe por AT). Escrever um `AT+SEND` de 240 B na
  UART leva ~22 ms.
- O driver do DHT11 desliga as interrupções por até ~8,5 ms durante a leitura (a cada 2,5 s). Se um
  `+RCV` chegar nesse instante, bytes podem se perder na UART. O quadro é descartado como
  malformado e o remetente retransmite por falta de ACK.
- A falha do display só é detectada por ausência de resposta I2C (verificação a cada 5 s).
- O console serial é local e não autenticado. Quem tem acesso físico à USB controla as
  simulações. Desabilite ou proteja em produção.
- Gravações em flash (entrada/saída de HIGH, boot) pausam brevemente o processador.
  A frequência é baixa, o que limita o desgaste.
