// AirGuard IoT - queue of telemetry records waiting to be sent to AirGuard Web
// (pure, unit tested; the locking is done by net/uplink.cpp).
//
// Records are JSON objects stored back to back in a byte ring as
// [len lo][len hi][bytes]. When the ring is full the OLDEST records are
// dropped (the newest data matters most). Every record gets a sequence
// number, so a batch that was copied out, sent and confirmed can be removed
// even if older records were dropped meanwhile.
#pragma once
#include <stddef.h>
#include <stdint.h>

class UplinkQueue {
 public:
  UplinkQueue(uint8_t* storage, size_t cap) : buf_(storage), cap_(cap) {}

  // Adds one record. False (and nothing stored) if empty or larger than cap/4.
  bool push(const char* rec, size_t len);

  // Copies the oldest records, separated by ',', into `out` while they fit
  // (at most `maxRecords`). Returns the bytes written (no terminator);
  // `lastSeq` = sequence number of the last record copied, `count` = records.
  size_t peekBatch(char* out, size_t cap, size_t maxRecords, uint32_t& lastSeq,
                   size_t& count) const;

  // Removes every record with sequence number <= lastSeq (a confirmed batch).
  void confirm(uint32_t lastSeq);

  size_t   count() const { return count_; }
  size_t   bytes() const { return used_; }
  uint32_t dropped() const { return dropped_; }
  void     clear() { while (count_) popOldest(); }

 private:
  uint8_t at(size_t i) const { return buf_[(head_ + i) % cap_]; }
  size_t  lenAt(size_t off) const { return at(off) | (static_cast<size_t>(at(off + 1)) << 8); }
  void    popOldest();

  uint8_t* buf_;
  size_t   cap_;
  size_t   head_ = 0, used_ = 0, count_ = 0;
  uint32_t headSeq_ = 1;  // sequence number of the oldest record
  uint32_t dropped_ = 0;
};
