# 11. Telemetria para o AirGuard Web

O **ESP32** é a única placa na rede. Ele registra tudo o que **envia e recebe pelo LoRa** e manda
os registros ao AirGuard Web pelo **Wi-Fi**, sem notebook nem cabo USB. O **RP2040** (receptor) não
grava log nenhum: ele informa o ESP32 pelo próprio rádio, e o ESP32 repassa ao servidor.

```
RP2040 ──LoRa: ACK (+rssi/snr), STAT a cada 5 s──> ESP32 ──Wi-Fi + MQTT──> broker :1883 ──> AirGuard Web :8787 ──> página Ambiente
       <──LoRa: HB a cada 5 s, ALERT/RECOVERY─────       publica em          (mosquitto     (assina airguard/+/    (tempo real, SSE)
                                                         airguard/KITCHEN_01/ mosquitto)   telemetry, grava em
                                                         telemetry a cada 2 s              data/telemetry/)
```

## Envio (ESP32, `src/net/uplink.*`)

- Protocolo **MQTT 3.1.1** (biblioteca PubSubClient), tópico `airguard/<DEVICE_ID>/telemetry`
  (`airguard/KITCHEN_01/telemetry`), **QoS 0**, sem retain, client id `airguard-KITCHEN_01`,
  keep-alive de 30 s. Login próprio no broker (usuário `airguard`).
- Task própria no **core 0** (o loop da aplicação roda no core 1): Wi-Fi, DNS e MQTT nunca atrasam
  os sensores, o alarme ou o LoRa.
- Os registros esperam numa fila na RAM (`cfg::UPLINK_QUEUE_BYTES` = 32 kB, cerca de 2 a 3 min).
  Com a fila cheia, os mais antigos são descartados. Um lote sai da fila quando o pacote MQTT
  inteiro foi escrito na conexão TCP. **Com QoS 0 não há confirmação do broker:** se a conexão cair
  logo depois da escrita, esse lote (até 2 s de registros) pode se perder. Se a escrita falhar, o
  lote fica na fila e a conexão é refeita.
- Mensagem: `{"now": <millis() no envio>, "records": [...]}`. O servidor data cada registro como
  `chegada − (now − ms)`, então a placa **não precisa de relógio** (funciona também na virada do
  `millis()`).
- Sem TLS (rede local): o login e os dados trafegam em texto puro. O broker não tem ACL, então
  qualquer login dele pode ler e publicar em qualquer tópico.

Configuração, salva na NVS (sobrevive a reboot e a regravação do firmware):

```
net                                   estado (Wi-Fi, IP, MQTT, fila, enviados)
net ssid <rede>     net pass <senha>  rede Wi-Fi (o valor é o resto da linha)
net broker 192.168.1.10:1883           broker MQTT (porta padrão 1883)
net user airguard   net mqttpass <s>  login MQTT
tlm on|off                            ecoa os registros na USB como "@T {...}" (depuração; padrão off)
```

Os valores padrão podem vir de `src/config/net_secrets.h`. Esse arquivo é ignorado pelo git; o
modelo é `net_secrets.example.h`. O que é configurado no console tem prioridade.

## Registros (`src` = `"node"`)

O ESP32 grava um registro a cada transmissão ou recepção pelo rádio, e também nos eventos e no boot.

| `k` | Quando | Campos |
|---|---|---|
| `boot` | fim do `setup()` | `fw`, `device`, `gateway`, `boot` (contador), `gas_med`, `gas_high` (limites em V) |
| `env` | **junto com cada `tx`**: as leituras que o quadro leva | `temp`, `rh`, `hi`, `env` (`ok`/`invalid`/`FAULT`), `gas` (V), `ao`, `adc`, `air` (`ok`/`WARMUP`/`invalid`/`FAULT`), `sim`, `lv` (4 letras N/L/M/H: AIR, TEMP, HUM, SYS), `ov`, `relay`, `flags`, `lora`, `q` (fila TX), `wack`, `up` (s) |
| `tx` | início de um `AT+SEND` | `id` (da fila), `kind` (`hb`/`event`/`ack`), `try` (1 = primeira), `len`, `frame` |
| `txd` | fim da transmissão | `id`, `kind`, `ok`, `air` (ms no ar) |
| `ack` | ACK do receptor recebido | `id` (event_id), `from`, `rssi`/`snr` (como o ESP32 ouviu o ACK), `grssi`/`gsnr` (como o **receptor** ouviu o alerta), `dup`, `frame` |
| `text` | TEXT recebido e aceito | `id`, `from`, `text`, `rssi`, `snr` |
| `rxbad` | quadro rejeitado pelo parser | `err`, `len`, `rssi`, `snr` |
| `evt` | evento gerado pelo AlertManager | `id`, `type` (`ALERT`/`RECOVERY`), `cat`, `sev`, `prev`, `reminder`, `restored` |
| `fail` | evento não entregue após todas as tentativas | `id`, `tries` |

## Registro do receptor (`src` = `"gw"`, repassado pelo ESP32)

| `k` | Quando | Campos |
|---|---|---|
| `stat` | quadro STAT recebido | `seq` (último heartbeat ouvido), `map` (bit i = heartbeat `seq − i` ouvido, 32 bits), `hbs`, `miss`, `events`, `dups`, `bad` (contadores desde o boot do receptor), `up` (s), `lcd`, `lora`, `lost`, `grssi`/`gsnr` (como o receptor ouve o ESP32), `rssi`/`snr` (como o ESP32 ouviu o STAT), `frame` |

### Quadros LoRa do receptor

- **ACK** (agora com o enlace):
  `{"type":"ACK","device":"GATEWAY_01","dst":"KITCHEN_01","ack_for":458764,"rssi":-31,"snr":9}`.
- **STAT** (sem ACK):
  `{"type":"STAT","device":"GATEWAY_01","dst":"KITCHEN_01","seq":120,"map":4294967295,"hbs":118,"miss":2,"ev":3,"dup":0,"bad":0,"rssi":-31,"snr":9,"lcd":1,"lora":1,"lost":0,"up":610}`.
- Quando o STAT é enviado:
  - 250 ms depois de cada heartbeat, ou seja, a cada 5 s (`STAT_PERIOD_MS`; era 15 s). O ESP32
    acabou de transmitir e fica 2 s sem transmitir de novo (`TX_MIN_GAP_MS`), só ouvindo, o que
    evita colisões.
  - Sem heartbeats, um STAT a cada 30 s com `lost:1`.
  - Os ACKs têm prioridade.
- Firmwares antigos do ESP32 ignoram os campos novos do ACK, e um STAT vira `rxbad` (`UnknownType`).

## O que o servidor calcula

- **Perda de pacotes:** cada heartbeat transmitido (`tx`) é marcado como recebido ou perdido pelo
  `map` do STAT seguinte. A perda é calculada sobre os heartbeats já confirmados, numa janela de 10 min.
- **Sinal no receptor (RSSI/SNR):** `grssi`/`gsnr` dos STATs e ACKs.
- **Latência de confirmação:** do `tx` de um ALERT/RECOVERY até o `ack` recebido.
- **Receptor online:** STAT ou ACK nos últimos 50 s.
- **ESP32 online:** registro nos últimos 20 s.
- **Histórico:** agregados por minuto (média, mínimo e máximo), guardados por 30 dias em
  `AirGuard-Web/data/telemetry/AAAA-MM-DD.ndjson`.
