// Telemetry uplink queue (src/net/uplink_queue.h).
#include <string.h>

#include <string>

#include "../../src/net/uplink_queue.h"
#include "test_framework.h"

static std::string batch(const UplinkQueue& q, size_t cap, size_t maxRec, uint32_t& last, size_t& n) {
  char out[256];
  const size_t w = q.peekBatch(out, cap < sizeof out ? cap : sizeof out, maxRec, last, n);
  return std::string(out, w);
}

TEST(uplink_queue_batches_in_order_and_confirms) {
  uint8_t st[64];
  UplinkQueue q(st, sizeof st);
  CHECK(q.push("{\"a\":1}", 7));
  CHECK(q.push("{\"b\":2}", 7));
  CHECK(q.push("{\"c\":3}", 7));
  uint32_t last;
  size_t n;
  CHECK_STR(batch(q, 256, 2, last, n).c_str(), "{\"a\":1},{\"b\":2}");
  CHECK_EQ(n, 2u);
  q.confirm(last);
  CHECK_EQ(q.count(), 1u);
  CHECK_STR(batch(q, 256, 10, last, n).c_str(), "{\"c\":3}");
  q.confirm(last);
  CHECK_EQ(q.count(), 0u);
  CHECK_EQ(batch(q, 256, 10, last, n).size(), 0u);
  CHECK(!q.push("", 0));
  CHECK(!q.push("0123456789012345678", 17));  // > cap/4
}

TEST(uplink_queue_drops_oldest_and_wraps) {
  uint8_t st[40];  // 4 records of 8 B + 2 B header
  UplinkQueue q(st, sizeof st);
  char r[9];
  for (int i = 0; i < 4; ++i) { snprintf(r, sizeof r, "rec%05d", i); CHECK(q.push(r, 8)); }
  CHECK_EQ(q.count(), 4u);
  uint32_t last;
  size_t n;
  batch(q, 256, 1, last, n);  // copy out rec0 ...
  CHECK(q.push("rec00004", 8));  // ... but it is dropped meanwhile
  CHECK_EQ(q.dropped(), 1u);
  q.confirm(last);               // nothing newer than rec0 removed
  CHECK_EQ(q.count(), 4u);
  CHECK_STR(batch(q, 256, 10, last, n).c_str(), "rec00001,rec00002,rec00003,rec00004");
  for (int i = 5; i < 23; ++i) { snprintf(r, sizeof r, "rec%05d", i); CHECK(q.push(r, 8)); }
  CHECK_STR(batch(q, 256, 10, last, n).c_str(), "rec00019,rec00020,rec00021,rec00022");
  CHECK_STR(batch(q, 18, 10, last, n).c_str(), "rec00019,rec00020");  // only what fits
  q.confirm(last);
  CHECK_STR(batch(q, 256, 10, last, n).c_str(), "rec00021,rec00022");
}
