# 11. Telemetria para o AirGuard Web

O nó (ESP32) e o receptor (RP2040-Zero) escrevem na **USB serial**, no meio do log normal, linhas
de telemetria lidas pelo painel **Ambiente** do AirGuard Web:

```
@T {"src":"node","k":"env","ms":123456,"temp":25.4,"rh":61,...}\r\n
```

- Cada linha começa com `@T ` e traz **um objeto JSON plano**. `src` é `node` (ESP32) ou `gw`
  (RP2040), `k` é o tipo do registro e `ms` é o `millis()` da placa.
- A escrita **nunca bloqueia o loop**: se o buffer de TX da serial não tiver espaço (nenhum
  programa lendo a porta), a linha é descartada.
- No ESP32, `tlm off` / `tlm on` no console desliga e liga as linhas (o padrão é ligado).
- O quadro LoRa (`frame`) vai como string JSON escapada, com no máximo 240 B no rádio. Por isso a
  linha tem até 720 B (`tlm::LINE_CAP`).

O RP2040 não tem Wi-Fi, então o caminho é sempre o mesmo para as duas placas:

```
ESP32 ──USB──┐
             ├─ AirGuard-Web/bridge/serial_bridge.py ──HTTP POST──> servidor :8787 ──> página Ambiente
RP2040 ──USB─┘        (adiciona o relógio do PC)        /api/telemetry/ingest      (tempo real, SSE)
```

Só o receptor ligado ao notebook já basta: as leituras chegam pelo **heartbeat LoRa**, a cada 5 s,
com resolução de 0,05 V no gás. Com o ESP32 também na USB, o painel recebe as leituras completas a
cada 2 s, vê o lado do envio (tentativas, tempo no ar, ACK recebido) e casa cada pacote enviado
com o recebido.

## Registros do nó (`src":"node"`)

| `k` | Quando | Campos |
|---|---|---|
| `boot` | fim do `setup()` | `fw`, `device`, `gateway`, `boot` (contador), `gas_med`, `gas_high` (limites em V) |
| `env` | a cada `cfg::TELEMETRY_PERIOD_MS` (2 s) | `temp`, `rh`, `hi` (só com leitura válida), `env` (`ok`/`invalid`/`FAULT`), `gas` (V comparado com os limites; só quando utilizável), `ao`, `adc`, `air` (`ok`/`WARMUP`/`invalid`/`FAULT`), `sim`, `lv` (4 letras N/L/M/H: AIR, TEMP, HUM, SYS), `ov` (nível geral), `relay`, `flags`, `lora` (`ok`/`INIT`/`FAULT`), `q` (fila TX), `wack` (aguardando ACK), `up` (s) |
| `evt` | evento gerado pelo AlertManager | `id`, `type` (`ALERT`/`RECOVERY`), `cat`, `sev`, `prev`, `reminder`, `restored` |
| `tx` | início de um `AT+SEND` | `id` (da fila), `kind` (`hb`/`event`/`ack`), `try` (1 = primeira), `len`, `frame` |
| `txd` | fim da transmissão | `id`, `kind`, `ok`, `air` (ms no ar) |
| `ack` | ACK do gateway recebido | `id` (event_id), `from`, `rssi`, `snr`, `dup` |
| `text` | TEXT recebido e aceito | `id`, `from`, `text`, `rssi`, `snr` |
| `rxbad` | quadro rejeitado pelo parser | `err`, `len`, `rssi`, `snr` |
| `fail` | evento não entregue após todas as tentativas | `id`, `tries` |

## Registros do receptor (`"src":"gw"`)

| `k` | Quando | Campos |
|---|---|---|
| `boot` | fim do `setup()` | `gateway`, `node`, `lora`, `lcd` |
| `rx` | **todo** quadro recebido | `from` (endereço LoRa), `rssi`, `snr`, `len`, `res`, `frame`. `res`: `hb`, `event`, `dup` (retransmissão, ACK de novo), `foreign` (outro nó/destino), `invalid`, `other` |
| `ack` | ACK transmitido | `id`, `to`, `frame` |
| `ackdrop` | fila de ACK cheia | `id` |
| `stat` | a cada 10 s | `lcd`, `lora`, `events`, `hbs`, `ov`, `lost`, `rssi` |

## O que o servidor calcula

- **Perda de pacotes**: lacunas no `seq` dos heartbeats recebidos pelo RP2040 (janela de 10 min).
- **Latência de confirmação**: do `tx` de um ALERT/RECOVERY no ESP32 até o `ack` recebido nele.
- **Pacote perdido no ar**: `tx` do ESP32 sem `rx` correspondente no RP2040 em 8 s (só com as
  duas USB conectadas).
- **Histórico**: agregados por minuto (média, mínimo e máximo) guardados por 30 dias em
  `AirGuard-Web/data/telemetry/AAAA-MM-DD.ndjson`.
