// WhatsApp link: MQTT payloads, the message in the yellow band, the TEXT frame
// relayed to the receiver, screen priority (docs/12_whatsapp.md).
#include <string.h>

#include "../../src/actuators/led_manager.h"
#include "../../src/communication/message_protocol.h"
#include "../../src/display/screen_logic.h"
#include "../../src/display/wa_ticker.h"
#include "../../src/net/wa_codec.h"
#include "test_framework.h"

static bool link(const char* s, wa::Link& l) { return wa::parseLink(s, strlen(s), l); }
static bool inbox(const char* s, wa::Inbox& m) { return wa::parseInbox(s, strlen(s), m); }

// ---- session state ------------------------------------------------------------
TEST(wa_link_states) {
  wa::Link l;
  CHECK(link("{\"state\":\"open\",\"me\":\"5511988887777\",\"name\":\"Davi\"}", l));
  CHECK(l.state == wa::LinkState::Open);
  CHECK_STR(l.me, "5511988887777");
  CHECK_STR(l.code, "");
  CHECK(link("{\"state\":\"idle\"}", l) && l.state == wa::LinkState::Idle);
  CHECK_STR(l.me, "");
  CHECK(link(" { \"state\" : \"connecting\" } ", l) && l.state == wa::LinkState::Connecting);
  CHECK(link("{\"state\":\"offline\"}", l) && l.state == wa::LinkState::Offline);
  // a pairing by QR code happens on the web page: only the state reaches the node
  CHECK(link("{\"state\":\"qr\"}", l) && l.state == wa::LinkState::Qr);
  CHECK(link("{\"state\":\"qr\",\"n\":61,\"ttl\":65,\"qr\":\"QUJD\"}", l) && l.state == wa::LinkState::Qr);
  CHECK_STR(l.code, "");
  CHECK_STR(wa::linkStateName(wa::LinkState::Qr), "qr");
  CHECK_STR(wa::linkStateName(wa::LinkState::Code), "code");
}

TEST(wa_link_rejects_what_is_not_a_flat_object) {
  wa::Link l;
  CHECK(!link("", l));
  CHECK(!link("open", l));
  CHECK(!link("{}", l));                                   // no state
  CHECK(!link("{\"state\":\"dancing\"}", l));
  CHECK(!link("{\"state\":\"open\",}", l));                // trailing comma
  CHECK(!link("{\"state\":\"open\"} x", l));
  CHECK(!link("{\"state\":{\"a\":1}}", l));                // nested
  CHECK(!link("{\"state\":\"open\",\"state\":\"idle\"}", l));  // duplicate key
  CHECK(!link("{\"state\":7}", l));
  CHECK(!link("{\"state\":\"code\",\"code\":\"ABCD1234\",\"ttl\":\"165\"}", l));  // ttl must be a number
  CHECK(l.state == wa::LinkState::Unknown);
}

// ---- pairing code -------------------------------------------------------------
TEST(wa_message_led_blinks_4hz_for_5_seconds) {
  const uint32_t total = cfg::WA_LED_BLINK_MS, half = cfg::WA_LED_HALF_MS;
  CHECK(LedManager::messageBlink(0, total, half));          // lit the moment the message arrives
  CHECK(LedManager::messageBlink(half - 1, total, half));
  CHECK(!LedManager::messageBlink(half, total, half));
  CHECK(LedManager::messageBlink(2 * half, total, half));
  CHECK(!LedManager::messageBlink(total, total, half));      // and off for good afterwards
  CHECK(!LedManager::messageBlink(total + 2 * half, total, half));
  CHECK(!LedManager::messageBlink(0xFFFFFFF0u, total, half));
  int flashes = 0;
  bool prev = false, last = false;
  for (uint32_t t = 0; t <= total; ++t) {
    last = LedManager::messageBlink(t, total, half);
    if (last && !prev) ++flashes;
    prev = last;
  }
  CHECK_EQ(flashes, 20);
  CHECK(!last);
  CHECK_EQ(half, 125);   // the receiver blinks its green LED at the same rate (rxcfg::MSG_BLINK_MS)
}

TEST(wa_link_pairing_code) {
  wa::Link l;
  CHECK(link("{\"state\":\"code\",\"code\":\"ABCD1234\",\"ttl\":165}", l));
  CHECK(l.state == wa::LinkState::Code);
  CHECK_STR(l.code, "ABCD1234");
  CHECK_EQ(l.ttlS, 165);
  CHECK_STR(l.me, "");
  CHECK(link("{\"ttl\":999999,\"code\":\"7Z9K2M4P\",\"state\":\"code\"}", l));  // any order, ttl capped
  CHECK_STR(l.code, "7Z9K2M4P");
  CHECK_EQ(l.ttlS, 3600);
  CHECK(link("{\"state\":\"code\",\"code\":\"ABCD1234\"}", l));                  // ttl optional
  CHECK_EQ(l.ttlS, 0);
  // the code of an older message never survives a newer state
  CHECK(link("{\"state\":\"idle\"}", l));
  CHECK_STR(l.code, "");
  CHECK(link("{\"state\":\"open\",\"me\":\"5511988887777\",\"code\":\"ABCD1234\"}", l));
  CHECK_STR(l.code, "");
}

TEST(wa_link_pairing_code_must_be_8_digits_or_capitals) {
  wa::Link l;
  CHECK(!link("{\"state\":\"code\"}", l));                             // no code
  CHECK(!link("{\"state\":\"code\",\"code\":\"\"}", l));
  CHECK(!link("{\"state\":\"code\",\"code\":\"ABCD123\"}", l));          // 7
  CHECK(!link("{\"state\":\"code\",\"code\":\"ABCD12345\"}", l));        // 9
  CHECK(!link("{\"state\":\"code\",\"code\":\"ABCD12345678901234\"}", l));
  CHECK(!link("{\"state\":\"code\",\"code\":\"abcd1234\"}", l));         // lower case
  CHECK(!link("{\"state\":\"code\",\"code\":\"ABCD-123\"}", l));
  CHECK(!link("{\"state\":\"code\",\"code\":\"ABC\\u00c91234\"}", l));   // not ASCII
  CHECK(!link("{\"state\":\"code\",\"code\":12345678}", l));           // not a string
  CHECK(l.state == wa::LinkState::Unknown);
  CHECK_STR(l.code, "");
}

// ---- messages ------------------------------------------------------------------
TEST(wa_inbox_message) {
  wa::Inbox m;
  CHECK(inbox("{\"id\":17,\"from\":\"Maria\",\"text\":\"Chego em 10 min\"}", m));
  CHECK_EQ(m.id, 17u);
  CHECK_STR(m.from, "Maria");
  CHECK_STR(m.text, "Chego em 10 min");
  // escapes, UTF-8 kept as is, unknown keys ignored, sender optional
  CHECK(inbox("{\"text\":\"a \\\"b\\\" \\\\ c\\nd \\u00e7 \\ud83d\\ude00\",\"id\":4294967295,\"x\":true,\"y\":-1.5}", m));
  CHECK_EQ(m.id, 4294967295u);
  CHECK_STR(m.from, "");
  CHECK_STR(m.text, "a \"b\" \\ c d \xc3\xa7 \xf0\x9f\x98\x80");
  CHECK(inbox("{\"id\":1,\"from\":\"Jo\xc3\xa3o\",\"text\":\"\xc3\xa9\"}", m));
  CHECK_STR(m.from, "Jo\xc3\xa3o");
}

TEST(wa_inbox_rejects_bad_payloads) {
  wa::Inbox m;
  CHECK(!inbox("{\"id\":1,\"from\":\"a\"}", m));               // no text
  CHECK(!inbox("{\"id\":1,\"text\":\"\"}", m));                // empty text
  CHECK(!inbox("{\"from\":\"a\",\"text\":\"b\"}", m));         // no id
  CHECK(!inbox("{\"id\":-1,\"text\":\"b\"}", m));
  CHECK(!inbox("{\"id\":1,\"text\":\"b", m));                  // cut
  CHECK(!inbox("{\"id\":1,\"text\":\"a\x01\"}", m));           // raw control char
  CHECK(!inbox("{\"id\":1,\"text\":[\"a\"]}", m));
  CHECK_EQ(m.id, 0u);
  CHECK_STR(m.text, "");
}

TEST(wa_inbox_long_text_is_cut_at_a_character_boundary) {
  static char json[1200];
  size_t n = static_cast<size_t>(snprintf(json, sizeof json, "{\"id\":2,\"text\":\""));
  for (int i = 0; i < 300; ++i) { json[n++] = '\xc3'; json[n++] = '\xa9'; }  // 300 x "e acute" = 600 bytes
  json[n++] = '"';
  json[n++] = '}';
  wa::Inbox m;
  CHECK(wa::parseInbox(json, n, m));
  const size_t len = strlen(m.text);
  CHECK(len <= sizeof m.text - 1);
  CHECK_EQ(len % 2, 0u);                                        // no half character
  CHECK_EQ(static_cast<unsigned char>(m.text[len - 1]), 0xA9);
}

// ---- node state ------------------------------------------------------------------
TEST(wa_node_state_json_and_signature) {
  wa::NodeState s;
  s.boot = 12;
  s.uptimeS = 345;
  s.levels[static_cast<uint8_t>(Category::AirQuality)] = Severity::High;
  s.overall = Severity::High;
  s.worst = Category::AirQuality;
  s.relayOn = true;
  s.envValid = true;
  s.temperature = 27.46f;
  s.humidity = 61.2f;
  s.airValid = true;
  s.gasVolts = 1.823f;
  char b[320];
  CHECK(wa::encodeNodeState(s, b, sizeof b) > 0);
  CHECK_STR(b, "{\"boot\":12,\"up\":345,\"lv\":\"HNNN\",\"ov\":\"HIGH\",\"worst\":\"AIR_QUALITY\",\"relay\":true,"
               "\"flags\":0,\"temp\":27.5,\"rh\":61,\"gas\":1.82}");
  CHECK_EQ(wa::encodeNodeState(s, b, 40), 0u);   // does not fit: nothing, never a cut object

  const uint32_t sig = wa::nodeStateSignature(s);
  CHECK(sig != 0);
  wa::NodeState t = s;
  t.uptimeS = 999;
  t.temperature = 30;                           // readings alone do not trigger a publish
  CHECK_EQ(wa::nodeStateSignature(t), sig);
  t.relayOn = false;
  CHECK(wa::nodeStateSignature(t) != sig);
  t = s;
  t.levels[static_cast<uint8_t>(Category::Humidity)] = Severity::Low;
  CHECK(wa::nodeStateSignature(t) != sig);
  t = s;
  t.systemFlags = SYS_LORA_NO_ACK;
  CHECK(wa::nodeStateSignature(t) != sig);
  CHECK(wa::nodeStateSignature(wa::NodeState()) != 0);   // "all normal" is a state too
}

// ---- yellow band ------------------------------------------------------------------
TEST(wa_ticker_scrolls_slower_than_the_measurements_and_twice) {
  CHECK(cfg::WA_TICKER_PX_PER_S < cfg::DISPLAY_TICKER_PX_PER_S);
  CHECK_EQ(cfg::WA_TICKER_PASSES, 2);
  WaTicker t;
  CHECK(!t.active());
  CHECK(t.push("Maria", "Oi", 1000) == WaTicker::Push::Shown);
  CHECK(t.active());
  CHECK_STR(t.text(), "Maria: Oi");
  CHECK_STR(t.last(), "Maria: Oi");
  // 9 chars x 12 px + 128 px = 236 px per pass
  const uint32_t pass = 236u * 1000u / cfg::WA_TICKER_PX_PER_S;
  CHECK_EQ(WaTicker::durationMs(9), 2 * pass);
  CHECK_EQ(t.x(1000), 128);                         // enters at the right edge
  CHECK_EQ(t.x(1000 + 1000), 128 - static_cast<int>(cfg::WA_TICKER_PX_PER_S));
  t.update(1000 + pass - 1, false);
  CHECK(t.active());
  CHECK(t.x(1000 + pass - 40) < -9 * 12 + 2);       // first pass leaving on the left
  CHECK_EQ(t.x(1000 + pass), 128);                  // second pass starts again on the right
  t.update(1000 + 2 * pass - 1, false);
  CHECK(t.active());
  t.update(1000 + 2 * pass, false);
  CHECK(!t.active());                               // header comes back
  CHECK_STR(t.text(), "");
}

TEST(wa_ticker_queue_and_ascii) {
  WaTicker t;
  CHECK(t.push("Jo\xc3\xa3o", "Aten\xc3\xa7\xc3\xa3o \xf0\x9f\x94\xa5", 0) == WaTicker::Push::Shown);
  CHECK_STR(t.text(), "Joao: Atencao ?");
  for (uint8_t i = 0; i < cfg::WA_MSG_QUEUE; ++i) CHECK(t.push("B", "dois", 10) == WaTicker::Push::Queued);
  CHECK(t.push("C", "sobra", 20) == WaTicker::Push::Full);
  CHECK_EQ(t.waiting(), cfg::WA_MSG_QUEUE);
  CHECK(t.push("D", "   ", 30) == WaTicker::Push::Empty);
  CHECK(t.push("", "sem nome", 30) == WaTicker::Push::Full);
  const uint32_t d1 = WaTicker::durationMs(strlen("Joao: Atencao ?"));
  t.update(d1, false);
  CHECK(t.active());
  CHECK_STR(t.text(), "B: dois");                   // next one starts right away
  CHECK_EQ(t.waiting(), cfg::WA_MSG_QUEUE - 1);
  CHECK_EQ(t.x(d1), 128);
}

TEST(wa_ticker_long_text_is_cut_with_dots) {
  WaTicker t;
  char longTxt[400];
  memset(longTxt, 'a', sizeof longTxt - 1);
  longTxt[sizeof longTxt - 1] = '\0';
  CHECK(t.push("Nome Bem Comprido Da Pessoa Que Enviou A Mensagem De Teste", longTxt, 0) == WaTicker::Push::Shown);
  CHECK_EQ(strlen(t.text()), cfg::WA_TEXT_MAX);
  CHECK(strcmp(t.text() + cfg::WA_TEXT_MAX - 3, "...") == 0);
  CHECK(strstr(t.text(), ": aaa") != nullptr);      // sender cut, text still there
  WaTicker u;
  CHECK(u.push(nullptr, "sem remetente", 0) == WaTicker::Push::Shown);
  CHECK_STR(u.text(), "sem remetente");
}

TEST(wa_ticker_is_emptied_by_an_alert) {
  WaTicker t;
  t.push("A", "um", 0);
  t.push("A", "dois", 0);
  t.update(100, true);                              // an alert starts
  CHECK(!t.active());
  CHECK_EQ(t.waiting(), 0);
  t.update(200, false);                             // alert over: nothing comes back
  CHECK(!t.active());
  CHECK(t.push("A", "novo", 300) == WaTicker::Push::Shown);
}

// ---- screen ------------------------------------------------------------------
TEST(wa_code_screen_never_hides_an_alert_or_a_message) {
  CHECK(selectScreen(false, Severity::Normal, false, true) == Screen::Code);
  CHECK(selectScreen(false, Severity::Low, false, true) == Screen::Alert);
  CHECK(selectScreen(false, Severity::High, false, true) == Screen::Alert);
  CHECK(selectScreen(false, Severity::Normal, true, true) == Screen::Message);
  CHECK(selectScreen(true, Severity::Normal, false, true) == Screen::Boot);
  CHECK(selectScreen(false, Severity::Normal, false, false) == Screen::Main);
}

// ---- TEXT frame for the receiver ----------------------------------------------
TEST(wa_text_frame_for_the_receiver) {
  char b[cfg::LORA_MAX_PAYLOAD + 1];
  size_t used = 0;
  const size_t n = proto::encodeText("KITCHEN_01", "GATEWAY_01", 7, "Maria: \"oi\" \\ tudo bem?", b, sizeof b, &used);
  CHECK(n > 0);
  CHECK_STR(b, "{\"type\":\"TEXT\",\"device\":\"KITCHEN_01\",\"dst\":\"GATEWAY_01\",\"message_id\":7,"
               "\"text\":\"Maria: \\\"oi\\\" \\\\ tudo bem?\"}");
  CHECK_EQ(used, strlen("Maria: \"oi\" \\ tudo bem?"));
  CHECK_EQ(proto::encodeText("KITCHEN_01", "GATEWAY_01", 7, "", b, sizeof b), 0u);
  CHECK_EQ(proto::encodeText("KITCHEN_01", "GATEWAY_01", 7, nullptr, b, sizeof b), 0u);
}

TEST(wa_text_frame_is_cut_to_the_lora_limit) {
  char longTxt[400];
  memset(longTxt, 'x', sizeof longTxt - 1);
  longTxt[sizeof longTxt - 1] = '\0';
  char b[512];                                        // larger than a LoRa frame on purpose
  size_t used = 0;
  size_t n = proto::encodeText("KITCHEN_01", "GATEWAY_01", 4294967295u, longTxt, b, sizeof b, &used);
  CHECK_EQ(n, cfg::LORA_MAX_PAYLOAD);                 // exactly the radio limit, never more
  CHECK(used > 140 && used < strlen(longTxt));
  CHECK_EQ(b[n - 1], '}');
  // escaped characters count double: fewer of them fit
  memset(longTxt, '"', sizeof longTxt - 1);
  size_t usedQ = 0;
  n = proto::encodeText("KITCHEN_01", "GATEWAY_01", 4294967295u, longTxt, b, sizeof b, &usedQ);
  CHECK(n > 0 && n <= cfg::LORA_MAX_PAYLOAD);
  CHECK_EQ(usedQ, used / 2);
  // a 2-byte character is never split
  for (size_t i = 0; i + 1 < sizeof longTxt - 1; i += 2) { longTxt[i] = '\xc3'; longTxt[i + 1] = '\xa9'; }
  longTxt[0] = 'a';
  longTxt[1] = 'b';
  longTxt[2] = 'c';                                   // odd offset before the first pair
  for (size_t i = 3; i + 1 < sizeof longTxt - 1; i += 2) { longTxt[i] = '\xc3'; longTxt[i + 1] = '\xa9'; }
  size_t usedU = 0;
  n = proto::encodeText("KITCHEN_01", "GATEWAY_01", 4294967295u, longTxt, b, sizeof b, &usedU);
  CHECK(n > 0 && n <= cfg::LORA_MAX_PAYLOAD);
  CHECK_EQ((usedU - 3) % 2, 0u);
  CHECK_EQ(static_cast<unsigned char>(longTxt[usedU - 1]), 0xA9);
}

TEST(wa_text_with_the_display_limit_always_fits_one_frame) {
  // What the node really sends: at most WA_TEXT_MAX display characters.
  char t[cfg::WA_TEXT_MAX + 1];
  memset(t, '"', cfg::WA_TEXT_MAX);                   // worst case: every char escaped
  t[cfg::WA_TEXT_MAX] = '\0';
  char b[cfg::LORA_MAX_PAYLOAD + 1];
  size_t used = 0;
  CHECK(proto::encodeText(cfg::DEVICE_ID, cfg::GATEWAY_ID, 4294967295u, t, b, sizeof b, &used) > 0);
  memset(t, 'a', cfg::WA_TEXT_MAX);
  CHECK(proto::encodeText(cfg::DEVICE_ID, cfg::GATEWAY_ID, 4294967295u, t, b, sizeof b, &used) > 0);
  CHECK_EQ(used, cfg::WA_TEXT_MAX);                   // plain text: nothing is lost on the radio
}
