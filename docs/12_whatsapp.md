# 12. Ligação com o WhatsApp

O AirGuard mostra no visor as mensagens que chegam no WhatsApp do usuário e avisa esse mesmo
WhatsApp quando há um alerta. Quem fala com o WhatsApp é o **AirGuard Web** (no notebook); o
**ESP32 só troca mensagens MQTT** com ele e repassa o texto ao receptor pelo LoRa.

```
WhatsApp ◄──internet──► AirGuard Web ◄──MQTT (broker :1883)──► ESP32 ──► OLED (faixa amarela / código de conexão)
 (celular)              (biblioteca Baileys,                    │
                         sessão em data/whatsapp/)              └──LoRa: TEXT──► RP2040 ──► LCD (2× até 5 s)
```

| O que acontece | Caminho |
|---|---|
| Chega uma mensagem no WhatsApp | AirGuard Web → MQTT `airguard/whatsapp/inbox` → ESP32 → OLED e, por LoRa, LCD |
| Um alerta começa, muda de gravidade ou normaliza; o relé desliga/religa a energia | ESP32 → MQTT `airguard/KITCHEN_01/state` → AirGuard Web → WhatsApp (mensagem para o próprio número) |
| O ESP32 é ligado e não há WhatsApp conectado | ESP32 → MQTT `…/event` (`hello`) → AirGuard Web pede ao WhatsApp um **código de conexão** (8 caracteres) → MQTT `airguard/whatsapp/link` → OLED |

Biblioteca do WhatsApp: **Baileys 7** (`baileys`, não oficial, fala o protocolo do WhatsApp Web por
WebSocket, sem navegador). Ela roda só no servidor: ver `AirGuard-Web/README.md`.

## Tópicos MQTT

Todos no mesmo broker e com o mesmo login da telemetria (doc 11). QoS 0, exceto onde indicado.

| Tópico | Sentido | Retido | Conteúdo |
|---|---|---|---|
| `airguard/whatsapp/link` | servidor → nó | **sim** (QoS 1) | estado da sessão do WhatsApp e, no pareamento, o código de conexão |
| `airguard/whatsapp/inbox` | servidor → nó | não | uma mensagem recebida |
| `airguard/<DEVICE_ID>/state` | nó → servidor | **sim** | níveis de alerta, relé e leituras do nó |
| `airguard/<DEVICE_ID>/event` | nó → servidor | não | `hello` (acabou de ligar) e o que o nó fez com cada mensagem |

### `airguard/whatsapp/link`

```json
{"state":"code","code":"ABCD1234","ttl":165}
{"state":"open","me":"5511988887777"}
{"state":"connecting"}   {"state":"qr"}   {"state":"idle"}   {"state":"offline"}
```

- `code`: o código de conexão que o WhatsApp deu ao servidor, sempre 8 caracteres (dígitos e
  letras maiúsculas; qualquer outra coisa o nó recusa). `ttl`: por quantos segundos o nó pode
  mostrá-lo. O usuário digita esse código no celular.
- `qr`: há um pareamento por QR code em andamento **na página web**. O QR não vem para o nó: ver
  "Por que não o QR code no OLED", abaixo.
- `idle`: sem conta conectada e sem pareamento em andamento. `offline`: o servidor caiu. É o
  **testamento MQTT** do servidor, publicado pelo broker, para o nó nunca ficar mostrando um código velho.
- Como o tópico é retido, o nó recebe o estado atual assim que assina, logo depois de ligar.

### `airguard/whatsapp/inbox`

```json
{"id":17,"from":"Maria","text":"Chego em 10 min"}
```

O servidor já tira emojis e quebras de linha e encurta: remetente até 20 caracteres, texto até 96
(com `...` no fim quando cortou). `from` é, nesta ordem: o nome do contato cadastrado no AirGuard
Web com esse número; o nome salvo na **agenda do celular**; o nome que o remetente escolheu no
perfil do WhatsApp; e, só quando não há nome nenhum, o número. A agenda vem do próprio WhatsApp (ele
a compartilha com os aparelhos conectados): o servidor a lê uma vez depois de conectar, acompanha as
mudanças e a guarda em `AirGuard-Web/data/whatsapp/names.json`.

### `airguard/<DEVICE_ID>/state`

```json
{"boot":12,"up":345,"lv":"NNHN","ov":"HIGH","worst":"HUMIDITY","relay":true,"flags":0,
 "temp":27.5,"rh":61,"gas":1.82}
```

`lv`: nível de cada categoria (gás, temperatura, umidade, sistema), `N/L/M/H`. Publicado quando um
nível, o relé ou as `flags` mudam, **e de novo a cada 30 s**. O nó manda o **estado**, não eventos:
o servidor compara com o último estado que já avisou. Assim uma publicação perdida (QoS 0) ou o
servidor reiniciando só atrasa o aviso, e um estado repetido nunca gera aviso em dobro.

### `airguard/<DEVICE_ID>/event`

```json
{"k":"hello","boot":12,"fw":"1.0.0"}
{"k":"wa","id":17,"res":"shown"}
```

`res`: `shown` (está passando no visor), `queued` (na fila do visor), `ignored` (havia alerta),
`full` (fila cheia), `empty` (nada imprimível). Os eventos esperam na RAM (1 kB) enquanto o nó
está sem broker.

## No ESP32

### Código de conexão no OLED (`Screen::Code`)

O pareamento usa o **código de 8 caracteres** do WhatsApp ("Conectar com número de telefone"), não
a câmera. Para pedir o código o servidor precisa do **número da conta**: ele é digitado uma vez na
aba WhatsApp do AirGuard Web (com a senha de edição) e fica guardado em
`AirGuard-Web/data/whatsapp/pairing.json`.

Quando o nó liga, publica `hello`. Se não houver WhatsApp conectado e o número já for conhecido, o
servidor pede o código e o OLED mostra só isto:

```
     WhatsApp          faixa amarela, fonte 2x
 Codigo de conexao
    ABCD-1234          fonte 2x (16 px de altura)
```

No celular: WhatsApp → Dispositivos conectados → Conectar dispositivo → **Conectar com número de
telefone** → digitar o código. O mesmo código aparece na página. A tentativa dura cerca de 160 s
(medido aqui duas vezes: o WhatsApp encerra depois de seis períodos, 60 + 5 × 20 s); se ninguém
digitar, o servidor volta para `idle` e a tela normal retorna. Para tentar de novo: religar o ESP32
ou pedir um novo código na página.

Sem número guardado, ao ligar o nó o servidor abre um pareamento por QR code na página, e a faixa
amarela avisa uma vez `WhatsApp: QR code na pagina web`.

Prioridade das telas: boot > mensagem remota (TEXT) > **alerta** > código de conexão > tela
principal. Um alerta sempre tira o código da tela.

#### Por que não o QR code no OLED

Foi a primeira versão e **não funcionou**: o QR de pareamento do WhatsApp tem 61 × 61 módulos (já
no nível de correção L, o menor possível) e o painel tem 64 linhas, o que dá 1 pixel por módulo,
~10 mm de lado. Testado no painel (2026-11-12), sozinho na tela e centralizado: a câmera do celular
não reconhece o código. O QR continua disponível, em tamanho grande, na página web.

### Mensagem na faixa amarela (`display/wa_ticker`)

- `remetente: texto` passa da direita para a esquerda na fonte 2x (16 px, a faixa amarela inteira),
  a **25 px/s** (`cfg::WA_TICKER_PX_PER_S`). As medições na faixa azul continuam a 40 px/s.
- Atravessa a tela **2 vezes** (`cfg::WA_TICKER_PASSES`) e o cabeçalho `AirGuard IoT` volta.
- Até 4 mensagens esperam na fila (`cfg::WA_MSG_QUEUE`); com a fila cheia, a mais nova é recusada.
- O texto é convertido para ASCII (acentos viram a letra sem acento) e limitado a 120 caracteres
  (`cfg::WA_TEXT_MAX`).
- **Com qualquer alerta ativo, a mensagem é ignorada**: não vai para o visor nem para o LoRa, e o
  nó informa `ignored`. Se um alerta começa com uma mensagem passando, ela sai da tela e a fila é
  esvaziada. Mensagens ignoradas não voltam depois.

### LED azul de mensagem (`actuators/led_manager`)

Um **LED azul** só para isto, no **GPIO32** (GPIO32 → 230 Ω → LED → GND; doc 01). Quando uma mensagem é
aceita para o visor (`shown` ou `queued`), ele pisca a **4 Hz por 5 s** (`cfg::WA_LED_HALF_MS`,
`cfg::WA_LED_BLINK_MS`); uma mensagem nova recomeça a contagem. Fora disso fica apagado. Mensagem
ignorada por causa de alerta não pisca, e um alerta que começa apaga o LED na hora. Os LEDs
vermelho e amarelo e o verde (relé) não mudaram.

No receptor não há LED azul: o **LED verde** dele pisca no mesmo ritmo enquanto a mensagem está
no LCD (nas duas vezes, até 5 s cada) e depois volta a ficar aceso.

### Repasse ao receptor (LoRa)

```json
{"type":"TEXT","device":"KITCHEN_01","dst":"GATEWAY_01","message_id":7,"text":"Maria: Chego em 10 min"}
```

- `proto::encodeText` corta o texto para o quadro nunca passar de 240 bytes (limite do RYLR998):
  sobram ~150 caracteres para o texto. Como o nó já limita a 120, hoje nada é cortado no rádio.
- Sem ACK e prioridade mínima (igual ao heartbeat): todo alerta sai antes. Só o texto mais novo
  espera pelo rádio; um mais antigo ainda na fila é descartado.

## No receptor (`receiver/`)

O `TEXT` aparece **duas vezes** no LCD (`LCD_TEXT_TIMES`), como as duas passadas no OLED do nó:
toma as duas linhas por **no máximo 5 s** (`LCD_TEXT_MS`), a tela normal volta por 2 s
(`LCD_TEXT_GAP_MS`) e o texto aparece de novo por até 5 s. O quadro `TEXT` é transmitido uma vez
só; a repetição é feita no receptor.
O texto é quebrado em 16 colunas; até 2 páginas de 2 linhas dividem os 5 s (uma página fica os 5 s,
duas ficam 2,5 s cada), e a segunda exibição recomeça na primeira página. O que não cabe em 64
caracteres fica de fora e a última linha termina em `...`. Se o receptor estiver em alerta, o texto
é ignorado; se um alerta chega antes do fim (inclusive no intervalo), o LCD volta ao alerta na hora
e a segunda exibição é cancelada. Uma mensagem nova substitui a que ainda estava sendo mostrada.

## No servidor (resumo)

- Mensagem recebida: só conversas diretas (grupos com `WHATSAPP_GROUPS=1`); status, canais e as
  mensagens do próprio usuário nunca. Mensagens entregues com mais de 2 min de atraso são
  registradas na página mas não vão ao visor.
- Aviso de alerta: enviado ao **próprio número** da conta conectada. Diz qual é o alerta, a
  gravidade (e se subiu ou baixou), as leituras, quando o relé desligou ou religou a energia e
  quando normalizou. Piora é avisada na hora; melhora só depois de 3 s estável; no máximo um aviso
  a cada 5 s por nó. Se o WhatsApp estiver reconectando, o aviso espera e sai com "ocorrido há N min".
- Depois de um reboot do nó, por 60 s só piora é considerada (os níveis recomeçam em normal e
  levam alguns segundos para se confirmar).

## Console (bancada)

```
wa                 estado da ligação (sessão, código de conexão, faixa amarela, contadores)
sim wa <texto>     injeta uma mensagem como se tivesse chegado pelo MQTT (remetente "Teste")
net                a linha "WhatsApp link" mostra o que chegou pelo MQTT
```

## Limitações e pendências

- Pareamento pelo código de 8 caracteres: verificado com uma conta real em 2026-11-12 (número com
  o nono dígito, código digitado no celular ~15 s depois de gerado). Por quanto tempo o código
  vale de fato não foi medido.
- O código exige o número da conta, guardado em texto claro em `data/whatsapp/pairing.json`.
- Os nomes da agenda do celular ficam em texto claro em `data/whatsapp/names.json` (apagado ao
  desconectar a conta). Para grupos, o nome da agenda só é achado quando o WhatsApp informa o
  número do participante.
- Se o nó for religado nos últimos segundos de uma tentativa de pareamento, o servidor não abre
  outra: o código some logo e é preciso religar de novo ou pedir pela página.
- Avisos saem em QoS 0: o reenvio a cada 30 s cobre perdas, com atraso de até 30 s.
- O aviso vai para o próprio número. **[PENDENTE de verificação]** se o celular toca para
  mensagens enviadas a si mesmo por um aparelho conectado.
- O servidor guarda as chaves da sessão em `AirGuard-Web/data/whatsapp/auth/`: quem lê essa pasta
  controla a conta do WhatsApp conectada.
- A biblioteca não é oficial: o WhatsApp pode mudar o protocolo ou restringir contas que a usam.
- Sem TLS no MQTT (rede local), como na telemetria: o texto das mensagens trafega em claro na
  rede Wi-Fi entre o notebook e o ESP32, e no LoRa entre o ESP32 e o receptor.
