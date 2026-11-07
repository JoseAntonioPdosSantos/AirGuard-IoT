#include "ack_manager.h"

#include <string.h>

static bool dueAt(uint32_t now, uint32_t notBefore) {
  return (int32_t)(now - notBefore) >= 0;  // wrap-safe
}

bool AckManager::enqueue(uint32_t id, uint8_t priority, int8_t key, bool needsAck,
                         const uint8_t* payload, size_t len, uint32_t now) {
  if (!payload || len == 0 || len > cfg::LORA_MAX_PAYLOAD) return false;

  Entry* slot = nullptr;
  // 1) Supersede an older message with the same key.
  if (key >= 0) {
    for (auto& e : q_) {
      if (e.state == State::Free || e.key != key || e.superseded) continue;
      ++stats_.superseded;
      if (e.state == State::Sending) {
        e.superseded = true;  // dropped when its TX completes
      } else {
        slot = &e;            // reuse in place
      }
      break;
    }
  }
  // 2) Free slot.
  if (!slot)
    for (auto& e : q_)
      if (e.state == State::Free) { slot = &e; break; }
  // 3) Evict the lowest-priority (oldest) entry with lower priority.
  if (!slot) {
    Entry* victim = nullptr;
    for (auto& e : q_) {
      if (e.state == State::Sending || e.priority >= priority) continue;
      if (!victim || e.priority < victim->priority ||
          (e.priority == victim->priority && e.seq < victim->seq))
        victim = &e;
    }
    if (!victim) { ++stats_.dropped; return false; }
    ++stats_.dropped;
    slot = victim;
  }

  slot->state = State::Ready;
  slot->id = id;
  slot->priority = priority;
  slot->key = key;
  slot->needsAck = needsAck;
  slot->superseded = false;
  slot->attempts = 0;
  slot->seq = ++seq_;
  slot->notBeforeMs = now;
  slot->sentAtMs = 0;
  slot->len = static_cast<uint8_t>(len);
  memcpy(slot->payload, payload, len);
  ++stats_.enqueued;
  return true;
}

const AckManager::Entry* AckManager::nextToTransmit(uint32_t now) {
  for (auto& e : q_)
    if (e.state == State::Sending) return nullptr;  // radio busy with one of ours
  if (everSent_ && (uint32_t)(now - lastTxEndMs_) < cfg_.minTxGapMs) return nullptr;

  Entry* best = nullptr;
  for (auto& e : q_) {
    if (e.state != State::Ready || !dueAt(now, e.notBeforeMs)) continue;
    if (!best || e.priority > best->priority ||
        (e.priority == best->priority && e.seq < best->seq))
      best = &e;
  }
  if (best) best->state = State::Sending;
  return best;
}

void AckManager::onTransmitted(uint32_t id, uint32_t now) {
  everSent_ = true;
  lastTxEndMs_ = now;
  Entry* e = findMutable(id);
  if (!e || e->state != State::Sending) return;
  ++stats_.transmissions;
  if (e->attempts > 0) ++stats_.retransmissions;
  ++e->attempts;
  if (e->superseded || !e->needsAck) { release(*e); return; }
  e->state = State::WaitAck;
  e->sentAtMs = now;
}

void AckManager::onTransmitFailed(uint32_t id, uint32_t now, uint32_t backoffMs) {
  Entry* e = findMutable(id);
  if (!e || e->state != State::Sending) return;
  ++stats_.busyBackoffs;
  if (e->superseded) { release(*e); return; }
  e->state = State::Ready;
  e->notBeforeMs = now + backoffMs;
}

bool AckManager::onAck(uint32_t id, uint32_t) {
  Entry* e = findMutable(id);
  if (!e || !e->needsAck || (e->state != State::WaitAck && e->state != State::Ready)) {
    ++stats_.duplicateAcks;  // late/duplicate ACK: harmless
    return false;
  }
  // An ACK can legitimately arrive while the entry is Ready for a retry.
  if (e->attempts == 0) { ++stats_.duplicateAcks; return false; }
  ++stats_.delivered;
  consecutiveFailures_ = 0;
  release(*e);
  return true;
}

void AckManager::poll(uint32_t now) {
  for (auto& e : q_) {
    if (e.state != State::WaitAck) continue;
    if ((uint32_t)(now - e.sentAtMs) < cfg_.ackTimeoutMs) continue;
    if (e.attempts > cfg_.maxRetries) {
      ++stats_.failed;
      ++consecutiveFailures_;
      if (failCount_ < 4) failures_[failCount_++] = e.id;
      release(e);
    } else {
      e.state = State::Ready;  // retransmit same payload / same event_id
      e.notBeforeMs = now;
    }
  }
}

bool AckManager::popFailure(uint32_t& id) {
  if (failCount_ == 0) return false;
  id = failures_[0];
  for (uint8_t i = 1; i < failCount_; ++i) failures_[i - 1] = failures_[i];
  --failCount_;
  return true;
}

uint8_t AckManager::pending() const {
  uint8_t n = 0;
  for (const auto& e : q_) if (e.state != State::Free) ++n;
  return n;
}

uint8_t AckManager::waitingAck() const {
  uint8_t n = 0;
  for (const auto& e : q_) if (e.state == State::WaitAck) ++n;
  return n;
}

const AckManager::Entry* AckManager::find(uint32_t id) const {
  for (const auto& e : q_) if (e.state != State::Free && e.id == id) return &e;
  return nullptr;
}

AckManager::Entry* AckManager::findMutable(uint32_t id) {
  for (auto& e : q_) if (e.state != State::Free && e.id == id) return &e;
  return nullptr;
}

// ---------------------------------------------------------------------------
uint32_t RecentIdCache::hash(const char* s) {  // FNV-1a
  uint32_t h = 2166136261u;
  while (s && *s) { h ^= static_cast<uint8_t>(*s++); h *= 16777619u; }
  return h;
}

bool RecentIdCache::checkAndInsert(const char* sender, uint32_t id) {
  const uint32_t h = hash(sender);
  for (uint8_t i = 0; i < N; ++i)
    if (used_[i] && senderHash_[i] == h && id_[i] == id) return true;
  senderHash_[next_] = h;
  id_[next_] = id;
  used_[next_] = true;
  next_ = (next_ + 1) % N;
  return false;
}
