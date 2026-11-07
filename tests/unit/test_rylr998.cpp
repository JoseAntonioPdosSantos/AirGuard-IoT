// RYLR998 AT framing: +RCV parsing (data with commas / CR LF), AT+SEND, airtime.
#include <string.h>

#include "../../src/communication/rylr998_codec.h"
#include "test_framework.h"

using namespace rylr;

static RcvResult P(const char* s, RcvFrame& f) { return parseRcv(s, strlen(s), f); }

TEST(rylr_rcv_basic) {
  RcvFrame f;
  CHECK(P("+RCV=50,5,HELLO,-99,40", f) == RcvResult::Ok);
  CHECK_EQ(f.address, 50);
  CHECK_EQ(f.length, 5);
  CHECK(memcmp(f.data, "HELLO", 5) == 0);
  CHECK_EQ(f.rssi, -99);
  CHECK_EQ(f.snr, 40);
}

TEST(rylr_rcv_guide_example_with_spaces) {
  RcvFrame f;  // exactly as printed in the REYAX guide
  CHECK(P("+RCV=50, 5, HELLO, -99, 40", f) == RcvResult::Ok);
  CHECK(memcmp(f.data, "HELLO", 5) == 0);
  CHECK_EQ(f.snr, 40);
}

TEST(rylr_rcv_json_with_commas_split_by_length) {
  const char* json = "{\"type\":\"ACK\",\"device\":\"GATEWAY_01\",\"ack_for\":458764}";
  char line[200];
  snprintf(line, sizeof line, "+RCV=7,%u,%s,-80,-3", (unsigned)strlen(json), json);
  RcvFrame f;
  CHECK(P(line, f) == RcvResult::Ok);
  CHECK_EQ(f.length, strlen(json));
  CHECK(memcmp(f.data, json, f.length) == 0);
  CHECK_EQ(f.rssi, -80);
  CHECK_EQ(f.snr, -3);
}

TEST(rylr_rcv_newline_inside_data_is_incomplete) {
  RcvFrame f;  // data "AB\r\nCD" (6 bytes): the line was cut at the '\n'
  CHECK(P("+RCV=1,6,AB\r", f) == RcvResult::Incomplete);
  const char full[] = "+RCV=1,6,AB\r\nCD,-50,9";
  CHECK(parseRcv(full, sizeof full - 1, f) == RcvResult::Ok);
  CHECK(memcmp(f.data, "AB\r\nCD", 6) == 0);
}

TEST(rylr_rcv_rejects_garbage) {
  RcvFrame f;
  CHECK(P("+OK", f) == RcvResult::Invalid);
  CHECK(P("+RCV=", f) == RcvResult::Invalid);
  CHECK(P("+RCV=70000,1,A,-1,1", f) == RcvResult::Invalid);   // address range
  CHECK(P("+RCV=1,0,,-1,1", f) == RcvResult::Invalid);        // empty
  CHECK(P("+RCV=1,241,X,-1,1", f) == RcvResult::Invalid);     // > 240
  CHECK(P("+RCV=1,3,ABCDEFGH,-1,1", f) == RcvResult::Invalid); // length mismatch
  CHECK(P("+RCV=1,2,AB,-1,1,junk", f) == RcvResult::Invalid);
}

TEST(rylr_build_send) {
  char out[300];
  const char* d = "{\"a\":1,\"b\":2}";
  size_t n = buildSend(out, sizeof out, 0, (const uint8_t*)d, strlen(d));
  CHECK_STR(out, "AT+SEND=0,13,{\"a\":1,\"b\":2}\r\n");
  CHECK_EQ(n, strlen(out));
  uint8_t big[241];
  memset(big, 'x', sizeof big);
  CHECK_EQ(buildSend(out, sizeof out, 1, big, 241), 0);  // > 240
  CHECK(buildSend(out, sizeof out, 1, big, 240) > 0);
  CHECK_EQ(buildSend(out, 20, 1, big, 240), 0);          // buffer too small
  const uint8_t crlf[] = {'a', '\n', 'b'};
  CHECK_EQ(buildSend(out, sizeof out, 1, crlf, 3), 0);   // would break AT framing
}

TEST(rylr_parse_err) {
  CHECK_EQ(parseErr("+ERR=17", 7), 17);
  CHECK_EQ(parseErr("+ERR=4", 6), 4);
  CHECK_EQ(parseErr("+OK", 3), -1);
}

TEST(rylr_airtime_matches_lora_formula) {
  // SF9/125k/CR4-5/pre 12, 5+8 B: Tsym 4.096 ms; payload 8+ceil((104-36+44)/36)*5 = 28 sym
  // -> (12+4.25+28)*4.096 = 181.2 ms
  CHECK_NEAR(airtimeMs(9, 7, 1, 12, 5), 182, 2);
  // SF8/125k, 232+8 B: Tsym 2.048 ms; 8+ceil((1920-32+44)/32)*5 = 313 sym
  // -> (16.25+313)*2.048 = 674.3 ms
  CHECK_NEAR(airtimeMs(8, 7, 1, 12, 232), 675, 2);
  // SF11/125k enables low-data-rate optimisation (Tsym 16.384 ms > 16 ms)
  CHECK(airtimeMs(11, 7, 1, 12, 50) > airtimeMs(10, 7, 1, 12, 50));
}
