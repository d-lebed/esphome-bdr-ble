#include "../components/bdr_ble/bdr_protocol.h"
#include "../components/bdr_ble/bdr_transport.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace esphome::bdr_ble;

namespace {

bool approx_eq(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) <= eps;
}

void test_crc16_and_framing() {
  // RUB v2.01 Challenge Request: 02 01 00 02 00 03
  const uint8_t rub_chal[6] = {0x02, 0x01, 0x00, 0x02, 0x00, 0x03};
  auto frame = wrap_direct_channel_frame(rub_chal, sizeof(rub_chal));
  assert(frame.size() == 9);
  assert(frame[0] == DIRECT_CHANNEL_CAN);
  uint16_t calc = crc16_ccitt(frame.data(), frame.size() - 2);
  uint16_t wire_crc =
      (static_cast<uint16_t>(frame[frame.size() - 2]) << 8) |
      frame[frame.size() - 1];
  assert(calc == wire_crc);

  // Verify in-place wrapper produces identical bytes
  std::vector<uint8_t> reuse_buf;
  wrap_direct_channel_frame_into(rub_chal, sizeof(rub_chal), reuse_buf);
  assert(reuse_buf == frame);
}

void test_tea_cipher() {
  // Deterministic 32-round TEA verification for Service 3 (LE) & RUB v2.01 (BE)
  uint32_t v0 = 0, v1 = 0;
  tea_encrypt(4, 0x12345678UL, 0x9ABCDEF0UL, v0, v1);
  assert(v0 != 0 && v1 != 0);

  uint32_t v0_repeat = 0, v1_repeat = 0;
  tea_encrypt(4, 0x12345678UL, 0x9ABCDEF0UL, v0_repeat, v1_repeat);
  assert(v0 == v0_repeat && v1 == v1_repeat);

  // Different auth level or challenge produces distinct output
  uint32_t v0_lvl1 = 0, v1_lvl1 = 0;
  tea_encrypt(1, 0x12345678UL, 0x9ABCDEF0UL, v0_lvl1, v1_lvl1);
  assert(v0 != v0_lvl1 || v1 != v1_lvl1);
}

void test_numeric_codecs_and_bounds() {
  float val = 0.0f;
  int32_t raw_enum = 0;

  // U8 valid & 0xFF sentinel
  const uint8_t u8_ok[1] = {200};
  const uint8_t u8_sentinel[1] = {0xFF};
  assert(decode_numeric_datapoint(BDR_TYPE_U8, 0.5f, u8_ok, 1, val, raw_enum));
  assert(approx_eq(val, 100.0f));
  assert(!decode_numeric_datapoint(BDR_TYPE_U8, 0.5f, u8_sentinel, 1, val,
                                   raw_enum));

  // I8 valid & -128 (0x80) sentinel
  const uint8_t i8_ok[1] = {static_cast<uint8_t>(-20)};
  const uint8_t i8_sentinel[1] = {0x80};
  assert(decode_numeric_datapoint(BDR_TYPE_I8, 1.0f, i8_ok, 1, val, raw_enum));
  assert(approx_eq(val, -20.0f));
  assert(!decode_numeric_datapoint(BDR_TYPE_I8, 1.0f, i8_sentinel, 1, val,
                                   raw_enum));

  // U16 & I16 sentinels and signed negative temperature
  const uint8_t i16_neg[2] = {0xCE, 0xFF};  // -50 -> -5.0 °C at 0.1 scale
  const uint8_t i16_sentinel[2] = {0x00, 0x80};
  assert(
      decode_numeric_datapoint(BDR_TYPE_I16, 0.1f, i16_neg, 2, val, raw_enum));
  assert(approx_eq(val, -5.0f));
  assert(!decode_numeric_datapoint(BDR_TYPE_I16, 0.1f, i16_sentinel, 2, val,
                                   raw_enum));

  // CRITICAL-02: Legitimate high U32 >= 0x30000000UL must succeed when not 0xFFFFFFFF
  const uint8_t u32_high[4] = {0x01, 0x00, 0xA7, 0x34};  // 0x34A70001UL
  const uint8_t u32_sentinel[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  assert(decode_numeric_datapoint(BDR_TYPE_U32, 1.0f, u32_high, 4, val,
                                  raw_enum));
  assert(!decode_numeric_datapoint(BDR_TYPE_U32, 1.0f, u32_sentinel, 4, val,
                                   raw_enum));

  // CRITICAL-02: 0x502B hardware descriptor min/max bounds reject out-of-range garbage (including 0x2Fxxxxxx)
  const uint8_t u32_garbage[4] = {0x00, 0x00, 0x00, 0x2F};  // 0x2F000000UL
  assert(!decode_numeric_datapoint(BDR_TYPE_U32, 1.0f, u32_garbage, 4, val,
                                   raw_enum, 4, true, 0, 1000000));
  const uint8_t u32_in_range[4] = {0xE8, 0x03, 0x00, 0x00};  // 1000
  assert(decode_numeric_datapoint(BDR_TYPE_U32, 1.0f, u32_in_range, 4, val,
                                  raw_enum, 4, true, 0, 1000000));
  assert(approx_eq(val, 1000.0f));

  // CRITICAL-02: Exact byte length enforcement via expected_len
  assert(!decode_numeric_datapoint(BDR_TYPE_U32, 1.0f, i16_neg, 2, val,
                                   raw_enum, 4));
  // When 0x502B explicitly reports expected_len == 2 on a U32 entry, 2-byte value is accepted
  const uint8_t u16_val[2] = {0x64, 0x00};
  assert(decode_numeric_datapoint(BDR_TYPE_U32, 1.0f, u16_val, 2, val, raw_enum,
                                  2));
  assert(approx_eq(val, 100.0f));

  // Signed bounds check on I16 (-300..900 -> -30.0..90.0 °C)
  assert(decode_numeric_datapoint(
      BDR_TYPE_I16, 0.1f, i16_neg, 2, val, raw_enum, 2, true,
      static_cast<uint32_t>(static_cast<int32_t>(-300)),
      static_cast<uint32_t>(static_cast<int32_t>(900))));
  assert(!decode_numeric_datapoint(
      BDR_TYPE_I16, 0.1f, i16_neg, 2, val, raw_enum, 2, true,
      static_cast<uint32_t>(static_cast<int32_t>(0)),
      static_cast<uint32_t>(static_cast<int32_t>(900))));

  // Encode 1-byte and 2-byte enums (HIGH-07)
  std::vector<uint8_t> encoded;
  assert(encode_enum_datapoint(3, 1, encoded));
  assert(encoded.size() == 1 && encoded[0] == 3);
  assert(encode_enum_datapoint(3, 2, encoded));
  assert(encoded.size() == 2 && encoded[0] == 3 && encoded[1] == 0);
  assert(encode_enum_datapoint(300, 0, encoded));
  assert(encoded.size() == 2 && encoded[0] == 0x2C && encoded[1] == 0x01);
  assert(!encode_enum_datapoint(0xFFFF, 2, encoded));  // 0xFFFF sentinel rejected
}

void test_visible_string_and_utf8_truncate() {
  // Valid ASCII & UTF-8 (Polish, French, German)
  assert(utf8_safe_truncate_len("Salon", 10) == 5);
  // "Piętro" = 'P'(1) 'i'(1) 'ę'(2: C4 99) 't'(1) 'r'(1) 'o'(1) = 7 bytes
  std::string pl = "Pi\xC4\x99tro";
  assert(pl.size() == 7);
  assert(utf8_safe_truncate_len(pl, 3) == 2);  // Must not cut inside 'ę'
  assert(utf8_safe_truncate_len(pl, 4) == 4);  // Includes full 'ę'
  assert(utf8_safe_truncate_len(pl, 20) == 7);

  // "Küche" = 'K'(1) 'ü'(2: C3 BC) 'c'(1) 'h'(1) 'e'(1) = 6 bytes
  std::string de = "K\xC3\xBC" "che";
  assert(utf8_safe_truncate_len(de, 2) == 1);
  assert(utf8_safe_truncate_len(de, 3) == 3);

  // "Chaudière" = "Chaudi"(6) + 'è'(2: C3 A8) + "re"(2) = 10 bytes
  std::string fr = "Chaudi\xC3\xA8re";
  assert(utf8_safe_truncate_len(fr, 7) == 6);
  assert(utf8_safe_truncate_len(fr, 8) == 8);

  // Control characters and invalid UTF-8 rejected
  assert(utf8_safe_truncate_len("Bad\nName", 10) == 0);
  assert(utf8_safe_truncate_len("Bad\x01Name", 10) == 0);
  assert(utf8_safe_truncate_len("Bad\xC3Name", 10) == 0);

  // decode_visible_string: rejects 4-byte binary error word when expected_max_len != 4
  std::string out;
  const uint8_t err_word[4] = {0x41, 0x42, 0x43, 0x44};
  assert(!decode_visible_string(err_word, 4, 20, out));
  assert(decode_visible_string(err_word, 4, 4, out));
  assert(out == "ABCD");

  // Null-padded UTF-8 string
  uint8_t padded[10] = {'P', 'i', 0xC4, 0x99, 't', 'r', 'o', 0, 0, 0};
  assert(decode_visible_string(padded, sizeof(padded), 10, out));
  assert(out == pl);
}

void test_time_of_day() {
  std::string out;
  // 6-byte CiA 301 TIME_OF_DAY: 14:30:00 (52,200,000 ms = 0x031C7F00), days = 0
  const uint32_t ms_14_30 = (14UL * 3600UL + 30UL * 60UL) * 1000UL;
  const uint8_t tod_time_only[6] = {
      static_cast<uint8_t>(ms_14_30 & 0xFF),
      static_cast<uint8_t>((ms_14_30 >> 8) & 0xFF),
      static_cast<uint8_t>((ms_14_30 >> 16) & 0xFF),
      static_cast<uint8_t>((ms_14_30 >> 24) & 0xFF),
      0x00,
      0x00,
  };
  assert(decode_time_of_day(tod_time_only, 6, out));
  assert(out == "14:30");

  // With date: 1984-01-02 (1 day since 1984-01-01) at 08:15
  const uint32_t ms_08_15 = (8UL * 3600UL + 15UL * 60UL) * 1000UL;
  const uint8_t tod_with_date[6] = {
      static_cast<uint8_t>(ms_08_15 & 0xFF),
      static_cast<uint8_t>((ms_08_15 >> 8) & 0xFF),
      static_cast<uint8_t>((ms_08_15 >> 16) & 0xFF),
      static_cast<uint8_t>((ms_08_15 >> 24) & 0xFF),
      0x01,
      0x00,
  };
  assert(decode_time_of_day(tod_with_date, 6, out));
  assert(out == "1984-01-02 08:15");

  // Unset all-zero 6-byte CiA 301 TIME_OF_DAY (days = 0, ms = 0) -> "Not set"
  const uint8_t tod_unset[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  assert(decode_time_of_day(tod_unset, 6, out));
  assert(out == "Not set");

  // Modern date at midnight: 2026-11-26 00:00 (15670 days since 1984-01-01, ms = 0)
  const uint16_t days_2026_11_26 = 15670;
  const uint8_t tod_date_midnight[6] = {
      0x00,
      0x00,
      0x00,
      0x00,
      static_cast<uint8_t>(days_2026_11_26 & 0xFF),
      static_cast<uint8_t>((days_2026_11_26 >> 8) & 0xFF),
  };
  assert(decode_time_of_day(tod_date_midnight, 6, out));
  assert(out == "2026-11-26 00:00");

  // Modern date with time & seconds: 2026-11-26 14:15:30
  const uint32_t ms_14_15_30 = (14UL * 3600UL + 15UL * 60UL + 30UL) * 1000UL;
  const uint8_t tod_date_hms[6] = {
      static_cast<uint8_t>(ms_14_15_30 & 0xFF),
      static_cast<uint8_t>((ms_14_15_30 >> 8) & 0xFF),
      static_cast<uint8_t>((ms_14_15_30 >> 16) & 0xFF),
      static_cast<uint8_t>((ms_14_15_30 >> 24) & 0xFF),
      static_cast<uint8_t>(days_2026_11_26 & 0xFF),
      static_cast<uint8_t>((days_2026_11_26 >> 8) & 0xFF),
  };
  assert(decode_time_of_day(tod_date_hms, 6, out));
  assert(out == "2026-11-26 14:15:30");

  // Sentinel 0xFFFFFFFF / 0xFFFF rejected
  const uint8_t tod_sentinel[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  assert(!decode_time_of_day(tod_sentinel, 6, out));

  // 2-byte fallback
  const uint8_t tod_2b[2] = {6, 45};
  assert(decode_time_of_day(tod_2b, 2, out));
  assert(out == "06:45");
}

void test_sdo_abort_classification() {
  assert(classify_sdo_abort(SDO_ABORT_OBJECT_NOT_EXIST) ==
         SdoAbortCategory::PERMANENT_UNSUPPORTED);
  assert(classify_sdo_abort(SDO_ABORT_SUBINDEX_NOT_EXIST) ==
         SdoAbortCategory::PERMANENT_UNSUPPORTED);
  assert(classify_sdo_abort(SDO_ABORT_UNSUPPORTED_ACCESS) ==
         SdoAbortCategory::AUTH_OR_ACCESS_DENIED);
  assert(classify_sdo_abort(SDO_ABORT_TYPE_MISMATCH) ==
         SdoAbortCategory::GET_LIST_INCOMPATIBLE);
  assert(classify_sdo_abort(SDO_ABORT_TIMEOUT) ==
         SdoAbortCategory::TRANSIENT_ERROR);
  assert(classify_sdo_abort(SDO_ABORT_GENERAL_ERROR) ==
         SdoAbortCategory::TRANSIENT_ERROR);
}

void test_direct_channel_transport() {
  DirectChannelTransport tr;
  tr.reserve_buffers();

  // 1) Multi-fragment TX (30-byte payload -> 33-byte frame -> 2 chunks: 0x00 (19B) + 0xFF (14B))
  std::vector<uint8_t> payload(30, 0x42);
  uint8_t pkt[1 + DIRECT_TX_MAX_CHUNK_DATA];
  size_t pkt_len = 0;

  assert(tr.begin_tx(payload.data(), payload.size(), pkt, pkt_len));
  assert(tr.is_tx_in_progress());
  assert(pkt_len == 20);
  assert(pkt[0] == 0x00);

  // HIGH-06: Reject concurrent begin_tx while tx_in_progress is true
  uint8_t dummy_pkt[1 + DIRECT_TX_MAX_CHUNK_DATA];
  size_t dummy_len = 0;
  assert(!tr.begin_tx(payload.data(), payload.size(), dummy_pkt, dummy_len));

  // Feed chunk 1 into RX side as well
  const uint8_t *rx_payload = nullptr;
  size_t rx_payload_len = 0;
  uint16_t rx_crc = 0, calc_crc = 0;
  bool stale = false;
  assert(tr.feed_rx_segment(pkt, pkt_len, 100, rx_payload, rx_payload_len,
                            rx_crc, calc_crc, stale) ==
         RxSegmentStatus::INCOMPLETE);
  assert(tr.is_rx_busy(150));

  // Second (final) TX chunk
  assert(tr.next_tx_fragment(pkt, pkt_len));
  assert(pkt_len == 15);
  assert(pkt[0] == SEGMENT_FINAL);
  assert(!tr.next_tx_fragment(dummy_pkt, dummy_len));
  assert(!tr.is_tx_in_progress());

  // Feed final chunk into RX side -> FRAME_READY with exact 30-byte inner payload
  assert(tr.feed_rx_segment(pkt, pkt_len, 120, rx_payload, rx_payload_len,
                            rx_crc, calc_crc, stale) ==
         RxSegmentStatus::FRAME_READY);
  assert(!tr.is_rx_busy(150));
  assert(rx_payload_len == payload.size());
  assert(std::memcmp(rx_payload, payload.data(), payload.size()) == 0);

  // Large >2 kB RX frame (e.g. ~2.3 kB 0x502B descriptor block with 150 entries)
  std::vector<uint8_t> big_payload(2300);
  for (size_t i = 0; i < big_payload.size(); i++) {
    big_payload[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
  }
  std::vector<uint8_t> big_frame;
  wrap_direct_channel_frame_into(big_payload.data(), big_payload.size(),
                                 big_frame);
  size_t off = 0;
  uint8_t seg_idx = 0;
  uint32_t ts = 200;
  while (off < big_frame.size()) {
    size_t rem = big_frame.size() - off;
    bool is_last = (rem <= DIRECT_TX_MAX_CHUNK_DATA);
    size_t chunk = is_last ? rem : DIRECT_TX_MAX_CHUNK_DATA;
    uint8_t seg_pkt[1 + DIRECT_TX_MAX_CHUNK_DATA];
    seg_pkt[0] = is_last ? SEGMENT_FINAL : seg_idx++;
    std::memcpy(&seg_pkt[1], big_frame.data() + off, chunk);
    off += chunk;
    auto st = tr.feed_rx_segment(seg_pkt, chunk + 1, ts, rx_payload,
                                 rx_payload_len, rx_crc, calc_crc, stale);
    ts += 10;
    if (is_last) {
      assert(st == RxSegmentStatus::FRAME_READY);
      assert(rx_payload_len == big_payload.size());
      assert(std::memcmp(rx_payload, big_payload.data(), big_payload.size()) ==
             0);
    } else {
      assert(st == RxSegmentStatus::INCOMPLETE);
    }
  }
}

void test_snapshot_auth_level_filtering() {
  assert(!is_snapshot_subindex_for_auth_level(0, 5));
  assert(!is_snapshot_subindex_for_auth_level(16, 5));
  assert(!is_snapshot_subindex_for_auth_level(1, 0));

  // auth_level 1: only 1, 6, 11
  for (uint8_t s = 1; s <= 15; s++) {
    bool expected = (s == 1 || s == 6 || s == 11);
    assert(is_snapshot_subindex_for_auth_level(s, 1) == expected);
  }
  // auth_level 3: 1..3, 6..8, 11..13 (9 of 15)
  int count_l3 = 0;
  for (uint8_t s = 1; s <= 15; s++) {
    if (is_snapshot_subindex_for_auth_level(s, 3)) {
      count_l3++;
    }
  }
  assert(count_l3 == 9);
  assert(!is_snapshot_subindex_for_auth_level(4, 3));
  assert(!is_snapshot_subindex_for_auth_level(5, 3));
  assert(is_snapshot_subindex_for_auth_level(4, 4));
  assert(!is_snapshot_subindex_for_auth_level(5, 4));
  assert(is_snapshot_subindex_for_auth_level(15, 5));
}

void test_rx_watchdog_calculation() {
  // No active poll groups -> disabled (0)
  assert(compute_rx_watchdog_ms(0) == 0);
  // Fast polling clamps to minimum 180 s
  assert(compute_rx_watchdog_ms(5000) == RX_WATCHDOG_MIN_MS);
  assert(compute_rx_watchdog_ms(10000) == RX_WATCHDOG_MIN_MS);
  assert(compute_rx_watchdog_ms(30000) == RX_WATCHDOG_MIN_MS);
  // Slower polling scales to 4 * interval + 30 s
  assert(compute_rx_watchdog_ms(60000) == 270000UL);
  assert(compute_rx_watchdog_ms(300000) == 1230000UL);
  // Overflow cap for very large intervals
  assert(compute_rx_watchdog_ms(3600000) == 3630000UL);
}

}  // namespace

int main() {
  test_crc16_and_framing();
  test_tea_cipher();
  test_numeric_codecs_and_bounds();
  test_visible_string_and_utf8_truncate();
  test_time_of_day();
  test_sdo_abort_classification();
  test_direct_channel_transport();
  test_snapshot_auth_level_filtering();
  test_rx_watchdog_calculation();
  std::printf("All bdr_protocol & bdr_transport unit tests passed!\n");
  return 0;
}
