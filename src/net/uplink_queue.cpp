#include "uplink_queue.h"

bool UplinkQueue::push(const char* rec, size_t len) {
  if (!rec || len == 0 || len > cap_ / 4 || len > 0xFFFF) return false;
  while (used_ + len + 2 > cap_) {
    popOldest();
    ++dropped_;
  }
  size_t pos = (head_ + used_) % cap_;
  buf_[pos] = static_cast<uint8_t>(len & 0xFF);
  pos = (pos + 1) % cap_;
  buf_[pos] = static_cast<uint8_t>(len >> 8);
  pos = (pos + 1) % cap_;
  for (size_t i = 0; i < len; ++i) {
    buf_[pos] = static_cast<uint8_t>(rec[i]);
    pos = (pos + 1) % cap_;
  }
  used_ += len + 2;
  ++count_;
  return true;
}

void UplinkQueue::popOldest() {
  if (!count_) return;
  const size_t n = lenAt(0) + 2;
  head_ = (head_ + n) % cap_;
  used_ -= n;
  --count_;
  ++headSeq_;
  if (!count_) head_ = 0;
}

size_t UplinkQueue::peekBatch(char* out, size_t cap, size_t maxRecords, uint32_t& lastSeq,
                              size_t& count) const {
  size_t w = 0, off = 0;
  count = 0;
  lastSeq = headSeq_ - 1;
  while (count < count_ && count < maxRecords) {
    const size_t len = lenAt(off);
    const size_t need = len + (count ? 1 : 0);
    if (w + need > cap) break;
    if (count) out[w++] = ',';
    for (size_t i = 0; i < len; ++i) out[w++] = static_cast<char>(at(off + 2 + i));
    off += len + 2;
    ++count;
    lastSeq = headSeq_ + static_cast<uint32_t>(count) - 1;
  }
  return w;
}

void UplinkQueue::confirm(uint32_t lastSeq) {
  while (count_ && static_cast<int32_t>(lastSeq - headSeq_) >= 0) popOldest();
}
