// ACK manager: delivery, 60 s timeout, retransmission, supersede, priority.
#include <string.h>

#include "../../src/communication/ack_manager.h"
#include "test_framework.h"

static const uint8_t PAYLOAD[] = "{\"type\":\"ALERT\"}";

static uint32_t sendOne(AckManager& a, uint32_t now) {
  const AckManager::Entry* e = a.nextToTransmit(now);
  if (!e) return 0;
  uint32_t id = e->id;
  a.onTransmitted(id, now);
  return id;
}

TEST(ack_delivered) {
  AckManager a;
  CHECK(a.enqueue(10, 3, 0, true, PAYLOAD, sizeof PAYLOAD, 0));
  CHECK_EQ(sendOne(a, 0), 10);
  CHECK_EQ(a.waitingAck(), 1);
  CHECK(a.onAck(10, 1500));
  CHECK_EQ(a.pending(), 0);
  CHECK_EQ(a.stats().delivered, 1);
  CHECK(a.linkHealthy());
}

TEST(ack_timeout_60s_retransmits_same_id) {
  AckManager a;
  a.enqueue(11, 3, 0, true, PAYLOAD, sizeof PAYLOAD, 0);
  sendOne(a, 0);
  a.poll(59999);
  CHECK(a.nextToTransmit(59999) == nullptr);   // still waiting
  a.poll(60000);
  const AckManager::Entry* e = a.nextToTransmit(60000);
  CHECK(e != nullptr);
  CHECK_EQ(e->id, 11);
  CHECK(memcmp(e->payload, PAYLOAD, sizeof PAYLOAD) == 0);
  a.onTransmitted(11, 60000);
  CHECK_EQ(a.stats().retransmissions, 1);
}

TEST(ack_gives_up_after_max_retries) {
  AckManager::Config c;
  c.maxRetries = 2;
  AckManager a(c);
  a.enqueue(12, 3, 0, true, PAYLOAD, sizeof PAYLOAD, 0);
  uint32_t now = 0;
  for (int i = 0; i < 3; ++i) {     // first try + 2 retries
    CHECK_EQ(sendOne(a, now), 12);
    now += 60000;
    a.poll(now);
  }
  CHECK_EQ(a.pending(), 0);
  uint32_t failed = 0;
  CHECK(a.popFailure(failed));
  CHECK_EQ(failed, 12);
  CHECK(!a.linkHealthy());
  CHECK_EQ(a.stats().transmissions, 3);
}

TEST(ack_min_gap_between_transmissions) {
  AckManager a;
  a.enqueue(1, 3, 0, false, PAYLOAD, sizeof PAYLOAD, 0);
  a.enqueue(2, 3, 1, false, PAYLOAD, sizeof PAYLOAD, 0);
  CHECK_EQ(sendOne(a, 0), 1);
  CHECK(a.nextToTransmit(1999) == nullptr);
  CHECK_EQ(sendOne(a, 2000), 2);
}

TEST(ack_priority_high_first) {
  AckManager a;
  a.enqueue(1, 1, 0, true, PAYLOAD, sizeof PAYLOAD, 0);   // LOW
  a.enqueue(2, 5, 1, true, PAYLOAD, sizeof PAYLOAD, 0);   // HIGH
  a.enqueue(3, 3, 2, true, PAYLOAD, sizeof PAYLOAD, 0);   // MEDIUM
  CHECK_EQ(sendOne(a, 0), 2);
  CHECK_EQ(sendOne(a, 2000), 3);
  CHECK_EQ(sendOne(a, 4000), 1);
}

TEST(ack_supersede_same_category) {
  AckManager a;
  a.enqueue(20, 3, 0, true, PAYLOAD, sizeof PAYLOAD, 0);   // MEDIUM air
  sendOne(a, 0);
  a.enqueue(21, 5, 0, true, PAYLOAD, sizeof PAYLOAD, 500); // HIGH air replaces it
  CHECK_EQ(a.pending(), 1);
  CHECK(a.find(20) == nullptr);
  CHECK_EQ(sendOne(a, 2000), 21);
  CHECK(!a.onAck(20, 2500));   // late ACK of the old event is harmless
  CHECK(a.onAck(21, 3000));
}

TEST(ack_supersede_while_sending) {
  AckManager a;
  a.enqueue(30, 3, 0, true, PAYLOAD, sizeof PAYLOAD, 0);
  const AckManager::Entry* e = a.nextToTransmit(0);   // radio busy with 30
  CHECK(e && e->id == 30);
  a.enqueue(31, 5, 0, true, PAYLOAD, sizeof PAYLOAD, 10);
  a.onTransmitted(30, 300);
  CHECK(a.find(30) == nullptr);   // dropped after TX: no ACK wait
  CHECK_EQ(sendOne(a, 2300), 31);
}

TEST(ack_channel_busy_backoff_not_counted) {
  AckManager a;
  a.enqueue(40, 3, 0, true, PAYLOAD, sizeof PAYLOAD, 0);
  const AckManager::Entry* e = a.nextToTransmit(0);
  a.onTransmitFailed(e->id, 0, 500);
  CHECK(a.nextToTransmit(499) == nullptr);
  e = a.nextToTransmit(500);
  CHECK(e != nullptr);
  CHECK_EQ(e->attempts, 0);
}

TEST(ack_queue_full_evicts_lower_priority) {
  AckManager a;
  for (uint32_t i = 0; i < cfg::TX_QUEUE_SIZE; ++i)
    CHECK(a.enqueue(100 + i, 1, -1, true, PAYLOAD, sizeof PAYLOAD, 0));
  CHECK(a.enqueue(999, 5, -1, true, PAYLOAD, sizeof PAYLOAD, 0));
  CHECK(a.find(100) == nullptr);           // oldest low-priority evicted
  CHECK(!a.enqueue(1000, 1, -1, true, PAYLOAD, sizeof PAYLOAD, 0));  // nothing lower
}

TEST(ack_rejects_oversize_payload) {
  AckManager a;
  uint8_t big[300] = {};
  CHECK(!a.enqueue(1, 1, -1, true, big, sizeof big, 0));
}

TEST(ack_fire_and_forget_released_after_tx) {
  AckManager a;
  a.enqueue(50, 4, -1, false, PAYLOAD, sizeof PAYLOAD, 0);
  sendOne(a, 0);
  CHECK_EQ(a.pending(), 0);
}

TEST(dedup_cache) {
  RecentIdCache c;
  CHECK(!c.checkAndInsert("WHATSAPP_GATEWAY", 205));
  CHECK(c.checkAndInsert("WHATSAPP_GATEWAY", 205));
  CHECK(!c.checkAndInsert("GATEWAY_01", 205));  // different sender
  for (uint32_t i = 0; i < cfg::RX_DEDUP_CACHE; ++i) c.checkAndInsert("X", 1000 + i);
  CHECK(!c.checkAndInsert("WHATSAPP_GATEWAY", 205));  // evicted after N new ids
}
