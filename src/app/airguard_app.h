// AirGuard IoT - application orchestrator (cooperative, non-blocking).
//
// Data flow (one direction, no module talks to a module "above" it):
//   sensors -> monitors -> AlertManager -> events -> protocol -> AckManager -> LoRa
//   LoRa RX -> protocol parser -> (ACK -> AckManager | TEXT -> message display + ACK)
//   AlertManager state -> buzzer / relay / LED / OLED
//
// Every task is time-sliced in loop(); nothing waits for the radio, the ACK
// or the display. See docs/02_architecture.md.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../actuators/buzzer_manager.h"
#include "../actuators/led_manager.h"
#include "../actuators/relay_manager.h"
#include "../communication/ack_manager.h"
#include "../communication/lora_manager.h"
#include "../communication/message_protocol.h"
#include "../display/oled_manager.h"
#include "../display/screen_logic.h"
#include "../monitoring/air_quality_monitor.h"
#include "../monitoring/alert_manager.h"
#include "../monitoring/environment_monitor.h"
#include "../sensors/mq135_sensor.h"
#include "../sensors/temperature_sensor.h"

class AirGuardApp {
 public:
  void setup();
  void loop();

 private:
  // tasks
  void taskSensors(uint32_t now);
  void taskAlerts(uint32_t now);
  void taskRadio(uint32_t now);
  void taskOutputs(uint32_t now);
  void taskDisplay(uint32_t now);
  void taskStatusLog(uint32_t now);
  void taskConsole(uint32_t now);

  // helpers
  void handleFrame(const uint8_t* data, size_t len, float rssi, float snr, uint32_t now);
  void sendAck(const char* to, uint32_t id, uint32_t now);
  void queueEvent(const AlertEvent& e, uint32_t now);
  uint16_t computeSystemFlags(uint32_t now) const;
  SensorSnapshot snapshot(uint32_t now) const;
  void handleCommand(char* line, uint32_t now);
  void runDiagnostics(uint32_t now);
  void printStatus(uint32_t now);
  void printStats();

  // hardware
  Mq135Sensor       mq_;
  TemperatureSensor envSensor_;
  LoraManager       lora_;
  OledManager       oled_;
  BuzzerManager     buzzer_;
  RelayManager      relay_;
  LedManager        led_;

  // logic
  AirQualityMonitor    air_;
  EnvironmentMonitor   env_;
  AlertManager*        alerts_ = nullptr;  // constructed in setup() (needs boot count)
  AckManager           ack_;
  RecentIdCache        rxDedup_;
  RemoteMessageDisplay msgs_;

  // scheduling
  uint32_t bootMs_ = 0;
  uint32_t lastAirMs_ = 0, lastEnvMs_ = 0, lastDisplayMs_ = 0, lastStatusMs_ = 0;
  bool     airSampleNew_ = false, envSampleNew_ = false;
  bool     restoreAnnounced_ = false;
  CalState lastCalState_ = CalState::Idle;
  uint8_t  savedHighMask_ = 0;

  // radio bookkeeping
  bool      inFlight_ = false;
  uint32_t  inFlightId_ = 0;
  uint32_t  ackSeq_ = 0;
  uint32_t  lastEventId_[CATEGORY_COUNT] = {};
  LinkState link_[CATEGORY_COUNT] = {};
  uint32_t  rxRejected_ = 0, rxDuplicates_ = 0, textShown_ = 0;

  // bench output test ('out test'): yellow -> red -> buzzer, 1 s each
  uint32_t outTestStartMs_ = 0, outTestUntilMs_ = 0;
  bool     outTest_ = false;

  // console
  char   cmd_[320] = {};
  size_t cmdLen_ = 0;
  bool   cmdOverflow_ = false;
};
