#include "airguard_app.h"

#include <Arduino.h>
#include <string.h>

#include "../communication/telemetry_codec.h"
#include "../config/config.h"
#include "../net/uplink.h"
#include "../platform/board.h"
#include "../utils/logger.h"
#include "../utils/persistent_store.h"
#include "../utils/telemetry.h"

namespace {

constexpr uint8_t PRIO_ACK = 6;
constexpr uint32_t ACK_ID_TAG = 0xF0000000UL;  // ids of our ACK replies (never events)
constexpr uint32_t HB_ID_TAG  = 0xE0000000UL;  // ids of heartbeats (never events)
constexpr uint8_t  PRIO_HEARTBEAT = 1;           // lowest: after every event
constexpr int8_t   HB_SUPERSEDE_KEY = 8;         // only the newest heartbeat waits

uint8_t priorityFor(const AlertEvent& e) {
  if (e.kind == EventKind::Recovery) return 2;
  switch (e.severity) {
    case Severity::High: return 5;
    case Severity::Medium: return 4;
    default: return 3;
  }
}

// Telemetry name of a queued frame, from the id tags above.
const char* frameKind(uint32_t id) {
  if ((id & 0xF0000000UL) == ACK_ID_TAG) return "ack";
  if ((id & 0xF0000000UL) == HB_ID_TAG) return "hb";
  return "event";
}

int8_t categoryOfEvent(const uint32_t* lastIds, uint32_t id) {
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i)
    if (lastIds[i] == id && id != 0) return static_cast<int8_t>(i);
  return -1;
}

}  // namespace

// ===========================================================================
// setup
// ===========================================================================
void AirGuardApp::setup() {
  // 1) Outputs to their SAFE state before anything else can fail.
  relay_.begin();
  buzzer_.begin();
  led_.begin();

  logger::begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (uint32_t)(millis() - t0) < 1500) {}  // give a USB host a chance
  bootMs_ = millis();

  LOGI("BOOT", "AirGuard IoT v%s - SMART ENVIRONMENTAL MONITORING", AIRGUARD_FW_VERSION);
  LOGI("BOOT", "Intelligent monitoring. Safer environments.");
  LOGI("BOOT", "board: %s", board::NAME);
  LOGI("BOOT", "device %s -> gateway %s, env sensor %s", cfg::DEVICE_ID, cfg::GATEWAY_ID,
       envSensor_.model());
  if (board::watchdogCausedReboot()) LOGW("BOOT", "previous reset was caused by the WATCHDOG");
  LOGW("BOOT", "prototype: NOT a certified gas/safety detector");

  // 2) Persistent state -> unique event ids across reboots.
  store::begin();
  const uint16_t boot = store::incrementBoot();
  alerts_.setBootCount(boot);
  const PersistedState& st = store::state();
  LOGI("BOOT", "boot #%u, HIGH mask 0x%02X", boot, st.highMask);
  uplink::begin();  // Wi-Fi + AirGuard Web telemetry (own task on core 0)

  // 3) Buses and peripherals (each may fail independently).
  board::i2cBegin(cfg::I2C_CLOCK_HZ);
  oled_.begin();
  oled_.render(DisplayModel{});  // boot screen

  mq_.begin();
  air_.begin(bootMs_);
  if (cfg::DEV_SKIP_MQ135_WARMUP) {
    air_.skipWarmup();
    LOGW("BOOT", "DEV: MQ-135 warm-up DISABLED (cfg::DEV_SKIP_MQ135_WARMUP) - not for deployment");
  }
  LOGI("BOOT", "MQ-135 alarm by AO voltage: MEDIUM %.2f V, HIGH %.2f V",
       thr::GAS_VOLTS.above.medium, thr::GAS_VOLTS.above.high);
  envSensor_.begin();
  lora_.begin();  // RYLR998 init runs in the background (reset + AT configuration)

  // 4) Reboot during a critical condition: re-arm HIGH until proven normal.
  savedHighMask_ = st.highMask;
  if (cfg::RESTORE_HIGH_AFTER_REBOOT && st.highMask) {
    alerts_.restoreHigh(st.highMask, bootMs_);
    LOGW("BOOT", "restored HIGH state (mask 0x%02X) from before the reboot", st.highMask);
  }

  randomSeed(board::hwRandom());
  buzzer_.chirp(millis());
  board::watchdogBegin(cfg::WATCHDOG_TIMEOUT_MS);
  LOGI("BOOT", "MQ-135 warm-up %lu s. Type 'help' for console commands.",
       static_cast<unsigned long>(cfg::DEV_SKIP_MQ135_WARMUP ? 0 : cfg::MQ135_WARMUP_MS / 1000));

  char t[tlm::SMALL_CAP];
  tlm::Line l(t, sizeof t, "node", "boot", millis());
  l.w().str("fw", AIRGUARD_FW_VERSION);
  l.w().str("device", cfg::DEVICE_ID);
  l.w().str("gateway", cfg::GATEWAY_ID);
  l.w().unum("boot", boot);
  l.w().flt("gas_med", thr::GAS_VOLTS.above.medium, 2);
  l.w().flt("gas_high", thr::GAS_VOLTS.above.high, 2);
  telemetry::write(t, l.finish());
}

// ===========================================================================
// loop: each task returns quickly; the slowest step is one sensor read.
// ===========================================================================
void AirGuardApp::loop() {
  const uint32_t now = millis();
  board::watchdogFeed();
  taskConsole(now);
  taskSensors(now);
  taskAlerts(now);
  taskRadio(now);
  taskOutputs(now);
  taskDisplay(now);
  taskStatusLog(now);
}

// ---------------------------------------------------------------------------
void AirGuardApp::taskSensors(uint32_t now) {
  if ((uint32_t)(now - lastEnvMs_) >= cfg::ENV_SAMPLE_PERIOD_MS || lastEnvMs_ == 0) {
    lastEnvMs_ = now == 0 ? 1 : now;
    const EnvReading r = envSensor_.read();
    const bool wasFault = env_.faulted();
    env_.addReading(r.ok, r.temperature, r.humidity);
    envSampleNew_ = true;
    if (!r.ok && !env_.simulating()) LOGD("ENV", "%s read failed", envSensor_.model());
    if (env_.faulted() && !wasFault) LOGE("ENV", "%s FAULT (consecutive failures)", envSensor_.model());
    if (!env_.faulted() && wasFault) LOGI("ENV", "%s readings recovered", envSensor_.model());
  }

  if ((uint32_t)(now - lastAirMs_) >= cfg::MQ135_SAMPLE_PERIOD_MS) {
    lastAirMs_ = now;
    const bool wasFault = air_.faulted();
    air_.addSample(mq_.readRaw());
    airSampleNew_ = true;
    if (air_.faulted() && !wasFault)
      LOGE("AIR", "MQ-135 FAULT: ADC %u out of valid range", air_.last().raw);
    if (!air_.faulted() && wasFault) LOGI("AIR", "MQ-135 readings recovered");
  }
}

SensorSnapshot AirGuardApp::snapshot(uint32_t now) const {
  SensorSnapshot s;
  s.airValid = air_.usable(now);
  s.rawAdc = air_.last().raw;
  s.gasVolts = air_.last().vAlarm;
  s.envValid = env_.valid();
  s.temperature = env_.temperature();
  s.humidity = env_.humidity();
  s.heatIndex = env_.heatIndex();
  return s;
}

uint16_t AirGuardApp::computeSystemFlags() const {
  uint16_t f = 0;
  if (env_.faulted()) f |= SYS_ENV_SENSOR_FAULT;
  if (air_.faulted()) f |= SYS_AIR_SENSOR_FAULT;
  if (loraFaultLatched_) f |= SYS_LORA_RADIO_FAULT;
  if (!ack_.linkHealthy()) f |= SYS_LORA_NO_ACK;
  if (!oled_.ok()) f |= SYS_DISPLAY_FAULT;
  return f;
}

// ---------------------------------------------------------------------------
void AirGuardApp::taskAlerts(uint32_t now) {
  AlertManager& al = alerts_;
  al.setSnapshot(snapshot(now));
  if (airSampleNew_) {
    airSampleNew_ = false;
    al.updateAir(air_.usable(now), air_.last().vAlarm, now);
  }
  if (envSampleNew_) {
    envSampleNew_ = false;
    al.updateEnvironment(env_.valid(), env_.temperature(), env_.apparent(), env_.humidity(), now);
  }
  al.setSystemFlags(computeSystemFlags(), now);
  al.tick(now);

  if (!restoreAnnounced_ && (uint32_t)(now - bootMs_) >= cfg::ENV_SAMPLE_PERIOD_MS) {
    restoreAnnounced_ = true;
    al.announceRestored(now);
  }

  AlertEvent e;
  while (al.popEvent(e)) queueEvent(e, now);

  const uint8_t mask = al.highMask();
  if (mask != savedHighMask_) {
    savedHighMask_ = mask;
    store::setHighMask(mask);  // flash write only when entering/leaving HIGH
  }
}

void AirGuardApp::queueEvent(const AlertEvent& e, uint32_t now) {
  char buf[cfg::LORA_MAX_PAYLOAD + 1];
  const size_t n = proto::encodeEvent(e, cfg::DEVICE_ID, cfg::GATEWAY_ID, buf, sizeof buf);
  const uint8_t ci = static_cast<uint8_t>(e.category);
  const bool important = e.severity >= Severity::Medium || e.kind == EventKind::Recovery;
  if (important) {
    LOGW("EVENT", "#%lu %s %s %s (prev %s)%s%s", static_cast<unsigned long>(e.eventId),
         eventKindName(e.kind), categoryName(e.category), severityName(e.severity),
         severityName(e.previous), e.reminder ? " reminder" : "", e.restored ? " restored" : "");
  } else {
    LOGI("EVENT", "#%lu %s %s %s (prev %s)%s", static_cast<unsigned long>(e.eventId),
         eventKindName(e.kind), categoryName(e.category), severityName(e.severity),
         severityName(e.previous), e.reminder ? " reminder" : "");
  }
  {
    char t[tlm::SMALL_CAP];
    tlm::Line l(t, sizeof t, "node", "evt", now);
    l.w().unum("id", e.eventId);
    l.w().str("type", eventKindName(e.kind));
    l.w().str("cat", categoryName(e.category));
    l.w().str("sev", severityName(e.severity));
    l.w().str("prev", severityName(e.previous));
    if (e.reminder) l.w().boolean("reminder", true);
    if (e.restored) l.w().boolean("restored", true);
    telemetry::write(t, l.finish());
  }
  if (n == 0) {
    LOGE("EVENT", "could not encode event #%lu", static_cast<unsigned long>(e.eventId));
    return;
  }
  LOGD("EVENT", "payload (%u B): %s", static_cast<unsigned>(n), buf);
  if (!ack_.enqueue(e.eventId, priorityFor(e), static_cast<int8_t>(ci), true,
                    reinterpret_cast<const uint8_t*>(buf), n, now)) {
    LOGE("EVENT", "TX queue full: event #%lu dropped", static_cast<unsigned long>(e.eventId));
    link_[ci] = LinkState::Failed;
    return;
  }
  lastEventId_[ci] = e.eventId;
  link_[ci] = LinkState::Sending;
}

// ---------------------------------------------------------------------------
void AirGuardApp::taskRadio(uint32_t now) {
  lora_.loop(now);
  if (lora_.state() == LoraManager::State::Fault) loraFaultLatched_ = true;
  else if (lora_.state() == LoraManager::State::Rx || lora_.state() == LoraManager::State::Tx)
    loraFaultLatched_ = false;

  bool ok;
  if (lora_.takeTxDone(ok) && inFlight_) {
    inFlight_ = false;
    const int8_t ci = categoryOfEvent(lastEventId_, inFlightId_);
    tlmTxDone(inFlightId_, ok, now);
    if (ok) {
      ack_.onTransmitted(inFlightId_, now);
      const AckManager::Entry* en = ack_.find(inFlightId_);
      const bool hb = (inFlightId_ & 0xF0000000UL) == HB_ID_TAG;
      logger::log(hb ? LogLevel::Debug : LogLevel::Info, "LORA", "TX done %s %lu (airtime %lu ms)%s",
                  hb ? "heartbeat" : "id", static_cast<unsigned long>(inFlightId_ & (hb ? 0x0FFFFFFFUL : 0xFFFFFFFFUL)),
                  static_cast<unsigned long>(lora_.lastAirtimeMs()),
                  en && en->state == AckManager::State::WaitAck ? ", waiting ACK" : "");
      if (ci >= 0 && link_[ci] == LinkState::Sending) link_[ci] = LinkState::WaitAck;
    } else {
      ack_.onTransmitFailed(inFlightId_, now, 1000);
      LOGW("LORA", "TX failed id %lu, will retry", static_cast<unsigned long>(inFlightId_));
    }
  }

  // Received frames.
  uint8_t rx[cfg::LORA_MAX_PAYLOAD + 1];
  size_t len;
  float rssi, snr;
  while (lora_.takeRx(rx, sizeof rx, len, rssi, snr)) handleFrame(rx, len, rssi, snr, now);

  queueHeartbeat(now);

  // Next transmission (only one in flight; never blocks).
  ack_.poll(now);
  if (!inFlight_ && lora_.ready()) {
    const AckManager::Entry* e = ack_.nextToTransmit(now);
    if (e) {
      const uint32_t id = e->id;
      switch (lora_.startTransmit(e->payload, e->len, now)) {
        case LoraManager::TxResult::Started:
          inFlight_ = true;
          inFlightId_ = id;
          tlmTx(id, e->attempts + 1, e->payload, e->len, now);
          if (e->attempts > 0)
            LOGW("LORA", "retransmission %u of id %lu (no ACK in %lu s)", e->attempts,
                 static_cast<unsigned long>(id), static_cast<unsigned long>(cfg::ACK_TIMEOUT_MS / 1000));
          break;
        case LoraManager::TxResult::Busy:
          ack_.onTransmitFailed(id, now, cfg::LORA_BUSY_BACKOFF_MS + random(cfg::LORA_BUSY_BACKOFF_MS));
          break;
        case LoraManager::TxResult::Error:
          ack_.onTransmitFailed(id, now, 5000);
          break;
      }
    }
  }

  uint32_t failedId;
  while (ack_.popFailure(failedId)) {
    LOGE("LORA", "event #%lu NOT delivered after %u attempts (communication failure)",
         static_cast<unsigned long>(failedId), cfg::ACK_MAX_RETRIES + 1);
    const int8_t ci = categoryOfEvent(lastEventId_, failedId);
    if (ci >= 0) link_[ci] = LinkState::Failed;
    char t[tlm::SMALL_CAP];
    tlm::Line l(t, sizeof t, "node", "fail", now);
    l.w().unum("id", failedId);
    l.w().unum("tries", cfg::ACK_MAX_RETRIES + 1);
    telemetry::write(t, l.finish());
  }
}

// Telemetry is recorded when the node transmits or receives on the radio:
// every transmission also records the readings it carries ("env").
void AirGuardApp::tlmTx(uint32_t id, uint8_t attempt, const uint8_t* frame, size_t len, uint32_t now) {
  tlmEnv(now);
  char t[tlm::LINE_CAP];
  tlm::Line l(t, sizeof t, "node", "tx", now);
  l.w().unum("id", id);
  l.w().str("kind", frameKind(id));
  l.w().unum("try", attempt);
  l.w().unum("len", len);
  l.w().strn("frame", reinterpret_cast<const char*>(frame), len);
  telemetry::write(t, l.finish());
}

void AirGuardApp::tlmTxDone(uint32_t id, bool ok, uint32_t now) {
  char t[tlm::SMALL_CAP];
  tlm::Line l(t, sizeof t, "node", "txd", now);
  l.w().unum("id", id);
  l.w().str("kind", frameKind(id));
  l.w().boolean("ok", ok);
  if (ok) l.w().unum("air", lora_.lastAirtimeMs());
  telemetry::write(t, l.finish());
}

void AirGuardApp::queueHeartbeat(uint32_t now) {
  if (cfg::HEARTBEAT_PERIOD_MS == 0 || !lora_.ready()) return;
  if ((uint32_t)(now - lastHeartbeatMs_) < cfg::HEARTBEAT_PERIOD_MS) return;
  lastHeartbeatMs_ = now;
  Severity lv[CATEGORY_COUNT];
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) lv[i] = alerts_.category(static_cast<Category>(i));
  char buf[cfg::LORA_MAX_PAYLOAD + 1];
  const size_t n = proto::encodeHeartbeat(cfg::DEVICE_ID, cfg::GATEWAY_ID, ++heartbeatSeq_, lv,
                                          snapshot(now), now / 1000, buf, sizeof buf);
  if (n == 0) return;
  const uint32_t qid = HB_ID_TAG | (heartbeatSeq_ & 0x0FFFFFFFUL);
  if (!ack_.enqueue(qid, PRIO_HEARTBEAT, HB_SUPERSEDE_KEY, false,
                    reinterpret_cast<const uint8_t*>(buf), n, now))
    LOGD("LORA", "heartbeat #%lu not queued (queue busy)", static_cast<unsigned long>(heartbeatSeq_));
}

void AirGuardApp::sendAck(const char* to, uint32_t id, uint32_t now) {
  char buf[96];
  const size_t n = proto::encodeAck(cfg::DEVICE_ID, to, id, buf, sizeof buf);
  if (n == 0) return;
  const uint32_t qid = ACK_ID_TAG | (++ackSeq_ & 0x0FFFFFFFUL);
  if (!ack_.enqueue(qid, PRIO_ACK, -1, false, reinterpret_cast<const uint8_t*>(buf), n, now))
    LOGW("RX", "could not queue ACK for %s #%lu", to, static_cast<unsigned long>(id));
}

void AirGuardApp::handleFrame(const uint8_t* data, size_t len, float rssi, float snr,
                              uint32_t now) {
  proto::IncomingMessage m;
  const proto::ParseError err = proto::parse(data, len, m);
  if (err != proto::ParseError::Ok) {
    char t[tlm::SMALL_CAP];
    tlm::Line l(t, sizeof t, "node", "rxbad", now);
    l.w().str("err", proto::parseErrorName(err));
    l.w().unum("len", len);
    l.w().flt("rssi", rssi, 0);
    l.w().flt("snr", snr, 1);
    telemetry::write(t, l.finish());
    ++rxRejected_;
    LOGW("RX", "frame from LoRa addr %u rejected (%s), %u B, RSSI %.0f",
         lora_.lastRxAddress(), proto::parseErrorName(err), static_cast<unsigned>(len), rssi);
    return;
  }
  if (!proto::addressedToUs(m, cfg::DEVICE_ID)) {
    LOGD("RX", "frame for '%s' ignored", m.dst);
    return;
  }

  if (m.type == proto::MsgType::Ack) {
    const bool fresh = ack_.onAck(m.id, now);
    {
      char t[tlm::SMALL_CAP];
      tlm::Line l(t, sizeof t, "node", "ack", now);
      l.w().unum("id", m.id);
      l.w().str("from", m.sender);
      l.w().flt("rssi", rssi, 0);
      l.w().flt("snr", snr, 1);
      if (m.hasLink) {  // how the receiver heard the event it confirms
        l.w().num("grssi", m.rssi);
        l.w().num("gsnr", m.snr);
      }
      if (!fresh) l.w().boolean("dup", true);
      l.w().strn("frame", reinterpret_cast<const char*>(data), len);
      telemetry::write(t, l.finish());
    }
    if (fresh) {
      LOGI("RX", "ACK for event #%lu from %s (RSSI %.0f dBm, SNR %.1f dB)",
           static_cast<unsigned long>(m.id), m.sender, rssi, snr);
      const int8_t ci = categoryOfEvent(lastEventId_, m.id);
      if (ci >= 0) link_[ci] = LinkState::Delivered;
    } else {
      LOGD("RX", "ACK #%lu ignored (unknown or duplicate)", static_cast<unsigned long>(m.id));
    }
    return;
  }

  if (m.type == proto::MsgType::Stat) {
    // Receiver status: forwarded to AirGuard Web as a "gw" record (the
    // receiver has no network and logs nothing itself).
    if (strcmp(m.sender, cfg::GATEWAY_ID) != 0) {
      LOGD("RX", "STAT from '%s' ignored", m.sender);
      return;
    }
    const proto::GatewayStat& g = m.stat;
    LOGD("RX", "STAT from %s: hb #%lu heard %lu missed %lu (RSSI there %d)", m.sender,
         static_cast<unsigned long>(g.seq), static_cast<unsigned long>(g.hbs),
         static_cast<unsigned long>(g.miss), m.rssi);
    char t[tlm::LINE_CAP];
    tlm::Line l(t, sizeof t, "gw", "stat", now);
    l.w().unum("seq", g.seq);
    l.w().unum("map", g.map);
    l.w().unum("hbs", g.hbs);
    l.w().unum("miss", g.miss);
    l.w().unum("events", g.events);
    l.w().unum("dups", g.dups);
    l.w().unum("bad", g.bad);
    l.w().unum("up", g.upS);
    l.w().boolean("lcd", g.lcd);
    l.w().boolean("lora", g.lora);
    l.w().boolean("lost", g.lost);
    if (m.hasLink) {
      l.w().num("grssi", m.rssi);
      l.w().num("gsnr", m.snr);
    }
    l.w().flt("rssi", rssi, 0);
    l.w().flt("snr", snr, 1);
    l.w().strn("frame", reinterpret_cast<const char*>(data), len);
    telemetry::write(t, l.finish());
    return;
  }

  // TEXT
  if (!proto::sourceAllowed(m.sender)) {
    ++rxRejected_;
    LOGW("RX", "TEXT from non-whitelisted source '%s' rejected", m.sender);
    return;
  }
  if (rxDedup_.checkAndInsert(m.sender, m.id)) {
    ++rxDuplicates_;
    LOGI("RX", "duplicate TEXT #%lu from %s: re-ACK, not shown again",
         static_cast<unsigned long>(m.id), m.sender);
    sendAck(m.sender, m.id, now);
    return;
  }
  LOGI("RX", "TEXT #%lu from %s (RSSI %.0f dBm): \"%s\"", static_cast<unsigned long>(m.id),
       m.sender, rssi, m.text);
  {
    char t[tlm::SMALL_CAP];
    tlm::Line l(t, sizeof t, "node", "text", now);
    l.w().unum("id", m.id);
    l.w().str("from", m.sender);
    l.w().str("text", m.text);
    l.w().flt("rssi", rssi, 0);
    l.w().flt("snr", snr, 1);
    telemetry::write(t, l.finish());
  }
  if (msgs_.push(m.sender, m.text)) ++textShown_;
  else LOGW("RX", "message queue full or empty text: not displayed");
  sendAck(m.sender, m.id, now);
}

// ---------------------------------------------------------------------------
void AirGuardApp::taskOutputs(uint32_t now) {
  const AlertManager& al = alerts_;
  const Severity overall = al.overall();
  buzzer_.setSeverity(overall, now);
  relay_.set(al.relayDemand(), now);

  if (outTest_) {
    if ((int32_t)(now - outTestUntilMs_) < 0) {
      // 3 s cycle: yellow ON, red ON, buzzer 3 short beeps (150 ms on / 200 ms off).
      const uint32_t t = (now - outTestStartMs_) % 3000;
      const uint32_t b = t - 2000;
      led_.force(t >= 1000 && t < 2000, t < 1000);
      buzzer_.force(t >= 2000 && b < 1050 && (b % 350) < 150);
      return;
    }
    outTest_ = false;
    buzzer_.force(false);
    LOGI("CON", "output test finished");
  }

  buzzer_.loop(now);

  // Green LED follows the relay in hardware (NC contact); red/yellow here.
  led_.setState(overall, air_.warmingUp(now) && !air_.usable(now));
  led_.loop(now);
}

void AirGuardApp::taskDisplay(uint32_t now) {
  const AlertManager& al = alerts_;
  msgs_.update(now, al.overall() == Severity::High);
  oled_.checkBus(now);
  oled_.retry(now);
  if ((uint32_t)(now - lastDisplayMs_) < cfg::DISPLAY_REFRESH_MS) return;
  lastDisplayMs_ = now;

  DisplayModel d;
  d.nowMs = now;
  const bool booting = (uint32_t)(now - bootMs_) < cfg::BOOT_SCREEN_MS;
  d.overall = al.overall();
  d.screen = selectScreen(booting, d.overall, msgs_.active());
  d.envValid = env_.valid();
  d.temperature = env_.temperature();
  d.humidity = env_.humidity();
  d.heatIndex = env_.heatIndex();
  d.envFault = env_.faulted();
  d.airWarmingUp = !air_.usable(now) && air_.warmingUp(now);
  d.airFault = air_.faulted();
  d.simulation = air_.simulating() || env_.simulating();
  d.airAoVolts = air_.last().vAo;
  d.airAoValid = air_.last().raw > 0 || air_.last().simulated;
  d.worst = al.worstCategory();
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) d.perCategory[i] = al.category(static_cast<Category>(i));
  d.systemFlags = al.systemFlags();
  d.relayOn = relay_.energised();
  const uint8_t wi = static_cast<uint8_t>(d.worst);
  d.loraOk = lora_.state() == LoraManager::State::Rx || lora_.state() == LoraManager::State::Tx;
  d.loraInit = lora_.state() == LoraManager::State::Init;
  d.alertLink = d.loraOk ? link_[wi] : LinkState::NoRadio;
  d.txPending = ack_.pending();
  d.msg = &msgs_;
  oled_.render(d);
}

void AirGuardApp::taskStatusLog(uint32_t now) {
  if ((uint32_t)(now - lastStatusMs_) < cfg::STATUS_LOG_PERIOD_MS) return;
  lastStatusMs_ = now;
  printStatus(now);
}

void AirGuardApp::tlmEnv(uint32_t now) {
  const AlertManager& al = alerts_;
  tlm::NodeEnv e;
  e.envValid = env_.valid();
  e.temperature = env_.temperature();
  e.humidity = env_.humidity();
  e.heatIndex = env_.heatIndex();
  e.envState = env_.faulted() ? "FAULT" : (env_.valid() ? "ok" : "invalid");
  e.airValid = air_.usable(now);
  e.gasVolts = air_.last().vAlarm;
  e.aoVolts = air_.last().vAo;
  e.rawAdc = air_.last().raw;
  e.airState = air_.faulted() ? "FAULT"
               : (air_.warmingUp(now) && !air_.usable(now) ? "WARMUP" : (air_.usable(now) ? "ok" : "invalid"));
  e.simulated = air_.simulating() || env_.simulating();
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) e.levels[i] = al.category(static_cast<Category>(i));
  e.overall = al.overall();
  e.relayOn = relay_.energised();
  e.systemFlags = al.systemFlags();
  e.lora = lora_.state() == LoraManager::State::Fault ? "FAULT"
           : (lora_.state() == LoraManager::State::Init ? "INIT" : "ok");
  e.txQueue = ack_.pending();
  e.waitAck = ack_.waitingAck();
  e.uptimeS = now / 1000;
  char t[tlm::SMALL_CAP];
  telemetry::write(t, tlm::encodeNodeEnv(e, now, t, sizeof t));
}

void AirGuardApp::printStatus(uint32_t now) {
  const AlertManager& al = alerts_;
  const AirSample& a = air_.last();
  LOGI("STAT", "T=%.1fC RH=%.0f%% HI=%.1fC env=%s | ADC=%u Vpin=%.3fV Vao=%.3fV Valarm=%.3fV "
       "idx=%u air=%s%s",
       env_.temperature(), env_.humidity(), env_.heatIndex(),
       env_.faulted() ? "FAULT" : (env_.valid() ? "ok" : "invalid"), a.raw, a.vPin, a.vAo,
       a.vAlarm, static_cast<unsigned>(proto::airIndex(a.vAlarm)),
       air_.faulted() ? "FAULT" : (air_.warmingUp(now) ? "WARMUP" : (air_.usable(now) ? "ok" : "invalid")),
       air_.simulating() ? " SIM" : "");
  LOGI("STAT", "sev AIR=%s TEMP=%s HUM=%s SYS=%s (flags 0x%02X) | relay=%s buzzer=%s | "
       "LoRa=%s queue=%u waitACK=%u",
       severityName(al.category(Category::AirQuality)), severityName(al.category(Category::Temperature)),
       severityName(al.category(Category::Humidity)), severityName(al.category(Category::System)),
       al.systemFlags(), relay_.energised() ? "ON" : "off", severityName(buzzer_.severity()),
       lora_.state() == LoraManager::State::Fault ? "FAULT"
       : (lora_.state() == LoraManager::State::Init ? "INIT" : "ok"),
       ack_.pending(), ack_.waitingAck());
}

void AirGuardApp::printStats() {
  const AckManager::Stats& s = ack_.stats();
  LOGI("STAT", "TX: enq %lu tx %lu retx %lu delivered %lu failed %lu superseded %lu dropped %lu busy %lu",
       (unsigned long)s.enqueued, (unsigned long)s.transmissions, (unsigned long)s.retransmissions,
       (unsigned long)s.delivered, (unsigned long)s.failed, (unsigned long)s.superseded,
       (unsigned long)s.dropped, (unsigned long)s.busyBackoffs);
  LOGI("STAT", "RX: frames %lu crcErr %lu overrun %lu rejected %lu dup %lu shown %lu | events %lu",
       (unsigned long)lora_.rxFrames(), (unsigned long)lora_.crcErrors(),
       (unsigned long)lora_.rxOverruns(), (unsigned long)rxRejected_,
       (unsigned long)rxDuplicates_, (unsigned long)textShown_,
       (unsigned long)alerts_.eventsGenerated());
}

// ---------------------------------------------------------------------------
void AirGuardApp::taskConsole(uint32_t now) {
  // Bounded: at most 64 chars per loop iteration.
  for (int i = 0; i < 64 && Serial.available() > 0; ++i) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      if (cmdOverflow_) LOGW("CON", "line too long, ignored");
      else if (cmdLen_ > 0) {
        cmd_[cmdLen_] = '\0';
        handleCommand(cmd_, now);
      }
      cmdLen_ = 0;
      cmdOverflow_ = false;
    } else if (cmdLen_ + 1 < sizeof cmd_) {
      cmd_[cmdLen_++] = static_cast<char>(c);
    } else {
      cmdOverflow_ = true;
    }
  }
}
