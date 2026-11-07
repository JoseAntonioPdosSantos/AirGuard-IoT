# 06 — Alertas sonoros, visuais, relé e LED

## 1. Ações por severidade

A severidade **global** é a maior entre as categorias AIR_QUALITY, TEMPERATURE, HUMIDITY e
SYSTEM.

| | NORMAL | LOW | MEDIUM | HIGH |
|---|---|---|---|---|
| **Buzzer** (ativo) | desligado | 2 bipes de 60 ms a cada 30 s | 3 bipes de 150 ms a cada 5 s | 400 ms ligado / 200 ms desligado, **contínuo enquanto durar** |
| **OLED** | tela principal | tela de alerta fixa | tela de alerta, **severidade piscando** (500 ms) | tela de alerta, **tela inteira piscando (inversão)** |
| **Mensagem LoRa** | RECOVERY ao voltar | ALERT | ALERT | ALERT (prioridade máxima) |
| **Relé** | desligado | desligado | desligado (opcional: `RELAY_ON_MEDIUM`) | **energizado** (AIR/TEMP/HUM; SYSTEM não aciona) |
| **LED verde** (pelo NC do relé) | **aceso** | aceso | aceso | **apagado** (relé energizado) |
| **LED amarelo** | apagado | **aceso fixo** | **piscando 2 Hz** | apagado |
| **LED vermelho** | apagado | apagado | apagado | **piscando 2 Hz** |

- Os padrões ficam em `cfg::BUZZ_PATTERN_*` como pares de duração ligado/desligado em ms, e
  podem ser ajustados ao buzzer real. Com um buzzer passivo, os tons são 2,0 / 2,5 / 3,1 kHz.
- No boot há um bipe curto de autoteste (150 ms).
- Durante o boot e o aquecimento do MQ-135, sem alerta ativo, o LED amarelo pisca devagar (1 Hz).
- O LED verde é ligado **por hardware** ao contato NC do relé (doc 01 §4.5). Ele apaga sempre que
  o relé energiza. Com `RELAY_ON_MEDIUM = true`, isso também acontece em MEDIUM.
- O buzzer é uma máquina de estados baseada em tempo, sem `delay()`. Uma nova severidade
  reinicia o padrão imediatamente.
- Problemas de **SYSTEM** também acionam o buzzer conforme a severidade deles. Por exemplo, um
  MQ-135 **sem calibração** após o aquecimento, ou rádio/display em falha, gera LOW (2 bipes a
  cada 30 s) até ser resolvido. Falha de sensor gera MEDIUM. Isso é intencional, para que um
  nó que não está medindo de fato não pareça "normal".

## 2. Telas do OLED (0,96" SSD1306 128×64 amarelo/azul, fonte 5×7, 21 colunas)

O módulo usado tem duas faixas de cor fixas: **pixels 0–15 em amarelo** (2 linhas de texto) e
**16–63 em azul** (6 linhas de texto), com um pequeno espaço físico entre elas. Os títulos e
destaques ficam na faixa amarela e os dados na azul. O layout também funciona em telas de uma
cor só.

**Boot (3 s):**
```
┌ amarelo ─────────────────┐
│       AirGuard IoT       │
├ azul ────────────────────┤
│   SMART ENVIRONMENTAL    │
│        MONITORING        │
│                          │
│ Intelligent monitoring.  │
│   Safer environments.    │
└──────────────────────────┘
```

**Principal:**
```
┌ amarelo ─────────────────┐
│ KITCHEN MONITOR      SIM │
│ Status: MONITORING       │
├ azul ────────────────────┤
│ Temp: 26.5 C             │
│ Humidity: 55%            │
│ Air Quality: NORMAL      │
│ Air index: 7             │
│                          │
│ LoRa: OK                 │
└──────────────────────────┘
```
- A linha "Air" mostra `WARMUP mm:ss`, `NO CAL` (sem calibração) ou `FAIL` (falha do sensor)
  quando for o caso.
- "Status" pode ser `MONITORING`, `WARMING UP`, `CAL nn%` ou `SENSOR FAULT`.
- `SIM` aparece quando há uma simulação ativa.
- Leituras inválidas aparecem como `--.-`, nunca como um valor antigo apresentado como atual.

**Alerta:**
```
┌ amarelo ─────────────────┐
│          HIGH            │  ← fonte 2× (16 px, ocupa toda a faixa)
├ azul ────────────────────┤
│ ALERT AIR_QUALITY        │
│ Air index: 62 (rel.)     │
│                          │
│ +TMP:LOW                 │
│ Relay:ON  LoRa:WAIT ACK  │
└──────────────────────────┘
```
- A tela mostra a severidade (em destaque na faixa amarela), a categoria mais grave, os valores
  relacionados (índice; temperatura e sensação térmica; umidade e temperatura; ou o problema de
  sistema), as demais categorias ativas e o estado do relé e da entrega LoRa (`SENDING`,
  `WAIT ACK`, `ACK OK`, `NO ACK`, `NO RADIO`).

**Mensagem remota:**
```
┌ amarelo ─────────────────┐
│█MSG WHATSAPP_GAT 1/2█████│  ← cabeçalho invertido
│█!  HIGH AIR  !███████████│  ← faixa piscando, só se houver alerta ativo
├ azul ────────────────────┤
│ Atencao: verifique a     │
│ cozinha.                 │  ← até 6 linhas por página
└──────────────────────────┘
```

## 3. Prioridade visual e mensagens remotas

Ordem: **HIGH > MEDIUM > LOW > mensagem remota > tela principal**, com estas regras:

1. Cada página de mensagem fica **exatamente 3 s** na tela. Mensagens longas são quebradas
   por palavra em até 2 páginas de 6 linhas. Depois disso, a tela volta **automaticamente**
   ao alerta ativo ou à tela principal.
2. Enquanto a mensagem é exibida durante um alerta, a **segunda linha (faixa amarela) mostra
   uma faixa piscante do alerta**. A mensagem nunca esconde o alerta.
3. Com **HIGH** ativo e várias mensagens na fila, a tela de alerta aparece por pelo menos 3 s
   entre duas mensagens. Uma sequência de mensagens não oculta indefinidamente um alerta
   crítico.
4. A fila guarda até 4 mensagens. Quando cheia, as novas são descartadas, mas ainda recebem ACK
   e o descarte é registrado no log.
5. O display só é atualizado via I2C quando o conteúdo muda, sem bloquear o firmware.

## 4. Relé: política de segurança

| Situação | Relé |
|---|---|
| Boot / inicialização | **desligado** (nível inativo gravado antes de o pino virar saída) |
| HIGH confirmado em AIR, TEMP ou HUM (`RELAY_ON_HIGH`) | **energizado** |
| Leitura inválida ou falha de sensor durante HIGH | **mantém energizado**: a falha não é tratada como "normal" |
| Recuperação | desliga só depois que a categoria fica abaixo de HIGH pelo tempo de recuperação (60 a 120 s) |
| Reboot durante HIGH (`RESTORE_HIGH_AFTER_REBOOT`) | volta a **energizar** já no boot e permanece até a recuperação ser comprovada |
| Travamento do firmware | o watchdog (8 s) reinicia, o relé passa pelo estado seguro (desligado) e o HIGH é restaurado da flash |

> O papel do relé (ex.: ligar um exaustor ou cortar uma válvula) depende da instalação e não é
> presumido pelo firmware. Avalie se o "estado seguro" da sua carga é energizado ou
> desenergizado e ajuste `RELAY_ACTIVE_HIGH`/fiação (contatos NA ou NF).

## 5. Recuperação

A recuperação exige histerese de valor e estabilidade no tempo. Quando uma categoria volta a
NORMAL:

1. o buzzer para, se nenhuma outra categoria estiver ativa;
2. o relé é liberado e o LED verde volta a acender (pelo contato NC); os LEDs amarelo e vermelho apagam;
3. o relé só continua acionado se outra categoria ainda exigir;
4. o OLED volta à tela principal;
5. um **RECOVERY** é enviado via LoRa (com ACK).
