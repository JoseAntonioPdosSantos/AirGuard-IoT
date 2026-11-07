// AirGuard IoT - outgoing message queue with ACK tracking (platform independent).
//
// Policy (docs/03_lora_protocol.md, section "Entrega confiável"):
//  * fixed-size priority queue, no dynamic allocation;
//  * one in-flight event per "supersede key" (category): a newer state of
//    the same category replaces the older unacknowledged one -> no storms;
//  * TX_MIN_GAP between transmissions (channel courtesy);
//  * ACK timeout 60 s -> retransmit the SAME payload/event_id, up to
//    maxRetries; then the message is reported as failed;
//  * channel busy / radio error -> short backoff, not counted as attempt;
//  * never blocks: the caller polls it from the main loop.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../config/config.h"

class AckManager {
 public:
  struct Config {
    uint32_t ackTimeoutMs = cfg::ACK_TIMEOUT_MS;
    uint8_t  maxRetries = cfg::ACK_MAX_RETRIES;
    uint32_t minTxGapMs = cfg::TX_MIN_GAP_MS;
  };

  enum class State : uint8_t { Free, Ready, Sending, WaitAck };

  struct Entry {
    State    state = State::Free;
    uint32_t id = 0;
    uint8_t  priority = 0;     // higher = sent first
    int8_t   key = -1;         // supersede key (-1 = none)
    bool     needsAck = false;
    bool     superseded = false;
    uint8_t  attempts = 0;     // transmissions completed
    uint32_t seq = 0;          // FIFO tie-break
    uint32_t notBeforeMs = 0;  // earliest next attempt
    uint32_t sentAtMs = 0;
    uint8_t  len = 0;
    uint8_t  payload[cfg::LORA_MAX_PAYLOAD];
  };

  struct Stats {
    uint32_t enqueued = 0, transmissions = 0, retransmissions = 0, delivered = 0,
             failed = 0, superseded = 0, dropped = 0, duplicateAcks = 0, busyBackoffs = 0;
  };

  AckManager() : AckManager(Config()) {}
  explicit AckManager(const Config& c) : cfg_(c) {}

  // Returns false if the message was rejected (queue full of higher priority
  // traffic or invalid length).
  bool enqueue(uint32_t id, uint8_t priority, int8_t supersedeKey, bool needsAck,
               const uint8_t* payload, size_t len, uint32_t nowMs);

  // Next entry to hand to the radio (marked Sending), or nullptr.
  const Entry* nextToTransmit(uint32_t nowMs);
  void onTransmitted(uint32_t id, uint32_t nowMs);           // TxDone
  void onTransmitFailed(uint32_t id, uint32_t nowMs, uint32_t backoffMs);  // busy/error
  // Returns true if the ACK matched a pending message.
  bool onAck(uint32_t id, uint32_t nowMs);
  void poll(uint32_t nowMs);  // timeouts/retries

  // One failed event id per call (after retries exhausted).
  bool popFailure(uint32_t& id);
  uint8_t pending() const;
  uint8_t waitingAck() const;
  bool    linkHealthy() const { return consecutiveFailures_ == 0; }
  uint32_t consecutiveFailures() const { return consecutiveFailures_; }
  const Stats& stats() const { return stats_; }
  const Entry* find(uint32_t id) const;

 private:
  Entry* findMutable(uint32_t id);
  void   release(Entry& e) { e.state = State::Free; }

  static constexpr uint8_t N = cfg::TX_QUEUE_SIZE;
  Config cfg_;
  Entry  q_[N];
  uint32_t seq_ = 0;
  bool     everSent_ = false;
  uint32_t lastTxEndMs_ = 0;
  uint32_t consecutiveFailures_ = 0;
  uint32_t failures_[4] = {};
  uint8_t  failCount_ = 0;
  Stats    stats_;
};

// Remembers the last N (sender, id) pairs to drop duplicate deliveries.
class RecentIdCache {
 public:
  // Returns true if (sender,id) was already seen; otherwise records it.
  bool checkAndInsert(const char* sender, uint32_t id);
  static uint32_t hash(const char* s);

 private:
  static constexpr uint8_t N = cfg::RX_DEDUP_CACHE;
  uint32_t senderHash_[N] = {};
  uint32_t id_[N] = {};
  bool     used_[N] = {};
  uint8_t  next_ = 0;
};
