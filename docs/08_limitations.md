# 08 — Limitações conhecidas

## Segurança e uso

- **Não é um detector certificado** de gás, fumaça ou incêndio, nem um equipamento de segurança.
  Não substitui detectores homologados (por exemplo, detector de GLP/gás natural com válvula de
  corte, detector de fumaça, detector de CO). É um protótipo de estudo de monitoramento
  ambiental.
- O MQ-135 **não identifica gases** e **não mede concentração**. O `air_index` é relativo ao ar
  de referência da calibração. O MQ-135 também **não é adequado para detectar CO ou GLP** com
  confiabilidade de segurança, porque sensores específicos e certificados existem para isso.
- Os limites de temperatura e umidade são de conforto e atenção para um protótipo, **não**
  limites médicos.
- O **DHT11** tem ±2 °C / ±5 %UR e não mede abaixo de 20 %UR. Os níveis de umidade baixa
  MEDIUM/HIGH não são confiáveis com ele.

## Sensor de gás

- Exige burn-in de 24–48 h e alguns minutos de aquecimento a cada energização. O valor de
  5 min no firmware precisa ser medido.
- Sofre deriva e envelhecimento, por isso precisa de recalibração periódica.
- A compensação de temperatura e umidade está desligada por não ter sido validada.
- Os limites do índice são valores iniciais e precisam de calibração experimental no local.
- O aquecedor consome ~0,8 W de forma contínua. Não é adequado para alimentação por bateria.

## Hardware ainda não confirmado

- Relé alimentado em 3,3 V (provável problema com um módulo de 5 V), ligação do divisor do
  MQ-135 (série ou paralelo), tipo e volume do buzzer ligado direto no GPIO, RL do módulo
  MQ-135, homologação ANATEL do RYLR998, capacidade do regulador 3,3 V (ver
  `docs/01_hardware.md` §6).
- O LED verde depende do contato NC do relé: ele indica "relé desligado", não "firmware
  rodando".
- O firmware **compila** com o toolchain real (`pio run`, variantes DHT22 e AHT20), mas **ainda
  não foi executado em hardware**. Os 23 testes experimentais estão pendentes.

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
  AHT20 ~80 ms. Ambos são compatíveis com o watchdog de 8 s e com os tempos dos alertas, mas não
  são "zero bloqueio".
- Não há listen-before-talk (o RYLR998 não o expõe por AT). Escrever um `AT+SEND` de 240 B na
  UART leva ~22 ms.
- A biblioteca DHT desliga as interrupções por ~4 ms durante a leitura (a cada 2,5 s). Se um
  `+RCV` chegar nesse instante, bytes podem se perder na UART. O quadro é descartado como
  malformado e o remetente retransmite por falta de ACK.
- A falha do display só é detectada por ausência de resposta I2C (verificação a cada 5 s).
- O console serial é local e não autenticado. Quem tem acesso físico à USB controla as
  simulações. Desabilite ou proteja em produção.
- Gravações em flash (calibração, entrada/saída de HIGH, boot) pausam brevemente os dois núcleos.
  A frequência é baixa, o que limita o desgaste.
