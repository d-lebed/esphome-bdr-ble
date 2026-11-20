#include "bdr_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome::bdr_ble {

uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if (crc & 1) {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
      } else {
        crc = static_cast<uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

void tea_encrypt(uint8_t auth_level, uint32_t id1, uint32_t id2,
                 uint32_t &out_v0, uint32_t &out_v1) {
  uint32_t v0 = auth_level;
  uint32_t v1 = auth_level;
  const uint32_t k0 = 0x15EFA43Fu;
  const uint32_t k1 = id1;
  const uint32_t k2 = id2;
  const uint32_t k3 = auth_level;
  const uint32_t delta = 0x9E3779B9u;
  uint32_t sum = 0;

  for (int i = 0; i < 32; i++) {
    sum += delta;
    v0 += (((v1 << 4) + k0) ^ (v1 + sum)) ^ ((v1 >> 5) + k1);
    v1 += (((v0 << 4) + k2) ^ (v0 + sum)) ^ ((v0 >> 5) + k3);
  }
  out_v0 = v0;
  out_v1 = v1;
}

void wrap_direct_channel_frame_into(const uint8_t *payload, size_t len,
                                    std::vector<uint8_t> &out_frame) {
  out_frame.resize(len + 3);
  out_frame[0] = DIRECT_CHANNEL_CAN;
  if (len > 0 && payload != nullptr) {
    std::memcpy(&out_frame[1], payload, len);
  }
  uint16_t crc = crc16_ccitt(out_frame.data(), len + 1);
  out_frame[1 + len] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  out_frame[2 + len] = static_cast<uint8_t>(crc & 0xFF);
}

std::vector<uint8_t> wrap_direct_channel_frame(const uint8_t *payload,
                                               size_t len) {
  std::vector<uint8_t> frame;
  wrap_direct_channel_frame_into(payload, len, frame);
  return frame;
}

namespace {

bool check_unsigned_bounds(uint32_t val, bool has_bounds, uint32_t raw_min,
                           uint32_t raw_max) {
  if (!has_bounds || raw_min == 0xFFFFFFFFUL || raw_max == 0xFFFFFFFFUL ||
      raw_min > raw_max) {
    return true;
  }
  return val >= raw_min && val <= raw_max;
}

bool check_signed_bounds(int32_t val, bool has_bounds, uint32_t raw_min,
                         uint32_t raw_max) {
  if (!has_bounds || raw_min == 0xFFFFFFFFUL || raw_max == 0xFFFFFFFFUL) {
    return true;
  }
  int32_t s_min = static_cast<int32_t>(raw_min);
  int32_t s_max = static_cast<int32_t>(raw_max);
  if (s_min > s_max) {
    return true;
  }
  return val >= s_min && val <= s_max;
}

}  // namespace

bool decode_numeric_datapoint(BdrDataType data_type, float scale,
                              const uint8_t *data, size_t data_len,
                              float &out_numeric, int32_t &out_raw_enum,
                              uint8_t expected_len, bool has_bounds,
                              uint32_t raw_min, uint32_t raw_max) {
  out_numeric = 0.0f;
  out_raw_enum = 0;
  if (data == nullptr || data_len == 0) {
    return false;
  }
  if (expected_len > 0 && data_len != expected_len) {
    return false;
  }

  switch (data_type) {
    case BDR_TYPE_U8:
      if (data_len == 1 && data[0] != 0xFF) {
        uint32_t v = data[0];
        if (!check_unsigned_bounds(v, has_bounds, raw_min, raw_max)) {
          return false;
        }
        out_numeric = static_cast<float>(v) * scale;
        return true;
      }
      return false;

    case BDR_TYPE_I8:
      if (data_len == 1) {
        int8_t v = static_cast<int8_t>(data[0]);
        if (v != -128 && check_signed_bounds(v, has_bounds, raw_min, raw_max)) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_U16:
      if (data_len == 2) {
        uint16_t v = static_cast<uint16_t>(data[0]) |
                     (static_cast<uint16_t>(data[1]) << 8);
        if (v != 0xFFFF &&
            check_unsigned_bounds(v, has_bounds, raw_min, raw_max)) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_I16:
      if (data_len == 2) {
        int16_t v = static_cast<int16_t>(
            static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8));
        if (v != -32768 &&
            check_signed_bounds(v, has_bounds, raw_min, raw_max)) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_U32:
      if (data_len == 4) {
        uint32_t v = static_cast<uint32_t>(data[0]) |
                     (static_cast<uint32_t>(data[1]) << 8) |
                     (static_cast<uint32_t>(data[2]) << 16) |
                     (static_cast<uint32_t>(data[3]) << 24);
        if (v != 0xFFFFFFFFUL &&
            check_unsigned_bounds(v, has_bounds, raw_min, raw_max)) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      } else if (data_len == 2 && expected_len == 2) {
        uint16_t v = static_cast<uint16_t>(data[0]) |
                     (static_cast<uint16_t>(data[1]) << 8);
        if (v != 0xFFFF &&
            check_unsigned_bounds(v, has_bounds, raw_min, raw_max)) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_I32:
      if (data_len == 4) {
        int32_t v = static_cast<int32_t>(
            static_cast<uint32_t>(data[0]) |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24));
        if (v != static_cast<int32_t>(0x80000000UL) &&
            check_signed_bounds(v, has_bounds, raw_min, raw_max)) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_ENUM:
      if (data_len == 2) {
        uint16_t u = static_cast<uint16_t>(data[0]) |
                     (static_cast<uint16_t>(data[1]) << 8);
        if (u == 0xFFFF ||
            !check_unsigned_bounds(u, has_bounds, raw_min, raw_max)) {
          return false;
        }
        out_raw_enum = static_cast<int32_t>(u);
      } else if (data_len == 1) {
        if (!check_unsigned_bounds(data[0], has_bounds, raw_min, raw_max)) {
          return false;
        }
        out_raw_enum = data[0];
      } else {
        return false;
      }
      out_numeric = static_cast<float>(out_raw_enum);
      return true;

    default:
      return false;
  }
}

bool encode_enum_datapoint(int32_t raw_value, uint8_t wire_size,
                           std::vector<uint8_t> &out_bytes) {
  out_bytes.clear();
  if (raw_value < 0) {
    return false;
  }
  if (wire_size == 2 || (wire_size == 0 && raw_value > 254)) {
    if (raw_value > 65534) {
      return false;
    }
    out_bytes.push_back(static_cast<uint8_t>(raw_value & 0xFF));
    out_bytes.push_back(static_cast<uint8_t>((raw_value >> 8) & 0xFF));
    return true;
  }
  if (raw_value > 254) {
    return false;
  }
  out_bytes.push_back(static_cast<uint8_t>(raw_value & 0xFF));
  return true;
}

bool encode_numeric_datapoint(BdrDataType data_type, float scale, float value,
                              std::vector<uint8_t> &out_bytes,
                              uint8_t wire_size) {
  out_bytes.clear();
  if (!std::isfinite(value) || !std::isfinite(scale) || scale <= 0.0f) {
    return false;
  }
  int64_t raw = static_cast<int64_t>(std::llround(value / scale));

  switch (data_type) {
    case BDR_TYPE_ENUM:
      if (raw < 0 || raw > 65534) {
        return false;
      }
      return encode_enum_datapoint(static_cast<int32_t>(raw), wire_size,
                                   out_bytes);

    case BDR_TYPE_U8:
      // 0xFF is the BDR invalid/unplugged sentinel for U8
      if (raw < 0 || raw > 254) {
        return false;
      }
      out_bytes.push_back(static_cast<uint8_t>(raw & 0xFF));
      return true;

    case BDR_TYPE_I8:
      // -128 (0x80) is the BDR invalid/unplugged sentinel for I8
      if (raw < -127 || raw > 127) {
        return false;
      }
      out_bytes.push_back(static_cast<uint8_t>(raw & 0xFF));
      return true;

    case BDR_TYPE_U16:
      // 0xFFFF is the BDR invalid/unplugged sentinel for U16
      if (raw < 0 || raw > 65534) {
        return false;
      }
      out_bytes.push_back(static_cast<uint8_t>(raw & 0xFF));
      out_bytes.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
      return true;

    case BDR_TYPE_I16:
      // -32768 (0x8000) is the BDR invalid/unplugged sentinel for I16
      if (raw < -32767 || raw > 32767) {
        return false;
      }
      out_bytes.push_back(static_cast<uint8_t>(raw & 0xFF));
      out_bytes.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
      return true;

    case BDR_TYPE_U32:
      // 0xFFFFFFFF is the BDR invalid/unplugged sentinel for U32
      if (raw < 0 || raw > 0xFFFFFFFELL) {
        return false;
      }
      out_bytes.push_back(static_cast<uint8_t>(raw & 0xFF));
      out_bytes.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
      out_bytes.push_back(static_cast<uint8_t>((raw >> 16) & 0xFF));
      out_bytes.push_back(static_cast<uint8_t>((raw >> 24) & 0xFF));
      return true;

    case BDR_TYPE_I32:
      // -2147483648 (0x80000000) is the BDR invalid/unplugged sentinel for I32
      if (raw < -2147483647LL || raw > 2147483647LL) {
        return false;
      }
      out_bytes.push_back(static_cast<uint8_t>(raw & 0xFF));
      out_bytes.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
      out_bytes.push_back(static_cast<uint8_t>((raw >> 16) & 0xFF));
      out_bytes.push_back(static_cast<uint8_t>((raw >> 24) & 0xFF));
      return true;

    default:
      return false;
  }
}

namespace {

size_t utf8_codepoint_len(const uint8_t *p, size_t rem) {
  if (rem == 0) {
    return 0;
  }
  uint8_t c = p[0];
  if (c >= 0x20 && c <= 0x7E) {
    return 1;
  }
  if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
    if (rem >= 2 && (p[1] & 0xC0) == 0x80) {
      return 2;
    }
    return 0;
  }
  if ((c & 0xF0) == 0xE0) {
    if (rem >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      if (c == 0xE0 && p[1] < 0xA0) return 0;  // overlong
      if (c == 0xED && p[1] >= 0xA0) return 0;  // UTF-16 surrogate
      return 3;
    }
    return 0;
  }
  if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
    if (rem >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
        (p[3] & 0xC0) == 0x80) {
      if (c == 0xF0 && p[1] < 0x90) return 0;  // overlong
      if (c == 0xF4 && p[1] >= 0x90) return 0;  // > U+10FFFF
      return 4;
    }
    return 0;
  }
  return 0;
}

}  // namespace

size_t utf8_safe_truncate_len(const std::string &s, size_t max_bytes) {
  if (s.empty() || max_bytes == 0) {
    return 0;
  }
  const auto *bytes = reinterpret_cast<const uint8_t *>(s.data());
  const size_t total = s.size();

  // Validate entire input string for control chars or invalid UTF-8 sequences,
  // and track the largest valid codepoint boundary <= max_bytes.
  size_t safe_len = 0;
  size_t i = 0;
  while (i < total) {
    size_t cp_len = utf8_codepoint_len(bytes + i, total - i);
    if (cp_len == 0) {
      return 0;
    }
    if (i + cp_len <= max_bytes) {
      safe_len = i + cp_len;
    }
    i += cp_len;
  }
  return safe_len;
}

bool decode_visible_string(const uint8_t *data, size_t data_len,
                           uint8_t expected_max_len, std::string &out_str) {
  out_str.clear();
  if (data == nullptr || data_len == 0) {
    return false;
  }
  // Reject 4-byte error/status words masquerading as fixed-length VisibleString responses
  if (expected_max_len > 0 && expected_max_len != 4 && data_len == 4) {
    return false;
  }

  size_t slen = 0;
  while (slen < data_len && data[slen] != 0) {
    slen++;
  }

  size_t i = 0;
  while (i < slen) {
    size_t cp_len = utf8_codepoint_len(data + i, slen - i);
    if (cp_len == 0) {
      return false;
    }
    i += cp_len;
  }

  out_str.assign(reinterpret_cast<const char *>(data), slen);
  return true;
}

bool decode_time_of_day(const uint8_t *data, size_t data_len,
                        std::string &out_str) {
  out_str.clear();
  if (data == nullptr) {
    return false;
  }

  // CANopen CiA 301 TIME_OF_DAY (6 bytes LE):
  // bytes[0..3]: uint32_t LE (bits 0..27 = milliseconds since midnight)
  // bytes[4..5]: uint16_t LE (days since January 1, 1984)
  if (data_len == 6) {
    uint32_t raw_u32 = static_cast<uint32_t>(data[0]) |
                       (static_cast<uint32_t>(data[1]) << 8) |
                       (static_cast<uint32_t>(data[2]) << 16) |
                       (static_cast<uint32_t>(data[3]) << 24);
    uint16_t days_since_1984 = static_cast<uint16_t>(data[4]) |
                               (static_cast<uint16_t>(data[5]) << 8);
    if (raw_u32 == 0xFFFFFFFFUL || days_since_1984 == 0xFFFFU) {
      return false;
    }

    uint32_t ms_since_midnight = raw_u32 & 0x0FFFFFFFUL;
    if (ms_since_midnight >= 86400000UL) {
      return false;
    }
    uint32_t total_sec = ms_since_midnight / 1000UL;
    uint32_t hours = total_sec / 3600UL;
    uint32_t minutes = (total_sec % 3600UL) / 60UL;
    uint32_t seconds = total_sec % 60UL;

    char buf[32];
    if (days_since_1984 == 0) {
      if (seconds == 0) {
        snprintf(buf, sizeof(buf), "%02u:%02u", hours, minutes);
      } else {
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", hours, minutes, seconds);
      }
      out_str = buf;
      return true;
    }

    // Civil date from days since 1984-01-01 (1984-01-01 is 5113 days after 1970-01-01)
    int32_t z = static_cast<int32_t>(days_since_1984) + 5113 + 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = static_cast<int32_t>(yoe) + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2) ? 1 : 0;

    if (seconds == 0) {
      snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02u:%02u",
               static_cast<int>(y), m, d, hours, minutes);
    } else {
      snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02u:%02u:%02u",
               static_cast<int>(y), m, d, hours, minutes, seconds);
    }
    out_str = buf;
    return true;
  }

  // Legacy 2-byte [hour, minute] fallback
  if (data_len == 2) {
    if (data[0] < 24 && data[1] < 60) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%02u:%02u", data[0], data[1]);
      out_str = buf;
      return true;
    }
  }
  return false;
}

SdoAbortCategory classify_sdo_abort(uint32_t abort_code) {
  switch (abort_code) {
    case SDO_ABORT_OBJECT_NOT_EXIST:
    case SDO_ABORT_SUBINDEX_NOT_EXIST:
    case SDO_ABORT_WRITE_ONLY:
      return SdoAbortCategory::PERMANENT_UNSUPPORTED;

    case SDO_ABORT_UNSUPPORTED_ACCESS:
      return SdoAbortCategory::AUTH_OR_ACCESS_DENIED;

    case SDO_ABORT_PARAM_INCOMPATIBLE:
    case SDO_ABORT_INTERNAL_INCOMPATIBLE:
    case SDO_ABORT_TYPE_MISMATCH:
    case SDO_ABORT_TYPE_LEN_HIGH:
    case SDO_ABORT_TYPE_LEN_LOW:
      return SdoAbortCategory::GET_LIST_INCOMPATIBLE;

    default:
      return SdoAbortCategory::TRANSIENT_ERROR;
  }
}

const char *ble_auth_fail_reason_str(uint8_t reason) {
  switch (reason) {
    case 0x4E:
      return "Passkey Entry Failed";
    case 0x50:
      return "Authentication Requirements";
    case 0x51:
      return "Confirm Value Failed (wrong PIN?)";
    case 0x52:
      return "Pairing Not Supported";
    case 0x53:
      return "Encryption Key Size";
    case 0x55:
      return "Unspecified Reason";
    case 0x56:
      return "Repeated Attempts (60s gateway lockout)";
    case 0x57:
      return "Invalid Parameters";
    case 0x58:
      return "DHKey Check Failed";
    case 0x60:
      return "SMP Busy";
    case 0x61:
      return "Encryption Failed (stale bond?)";
    case 0x63:
      return "SMP Response Timeout";
    case 0x66:
      return "Connection Timeout";
    default:
      return "SMP Error";
  }
}

bool is_transient_ble_auth_failure(uint8_t reason) {
  return reason == 0x60 || reason == 0x63 || reason == 0x66;
}

const char *sdo_abort_code_str(uint32_t abort_code) {
  switch (abort_code) {
    case 0x05030000UL:
      return "Toggle bit not alternated";
    case 0x05040000UL:
      return "SDO protocol timed out";
    case 0x05040001UL:
      return "Command specifier not valid";
    case 0x06010000UL:
      return "Unsupported access to object (check auth_level)";
    case 0x06010001UL:
      return "Attempt to read a write-only object";
    case 0x06010002UL:
      return "Attempt to write a read-only object";
    case 0x06020000UL:
      return "Object does not exist in object dictionary";
    case 0x06040041UL:
      return "Object cannot be mapped to PDO";
    case 0x06040043UL:
      return "General parameter incompatibility";
    case 0x06040047UL:
      return "General internal incompatibility in device";
    case 0x06060000UL:
      return "Access failed due to hardware error";
    case 0x06070010UL:
      return "Data type/length mismatch";
    case 0x06070012UL:
      return "Data type mismatch (length too high)";
    case 0x06070013UL:
      return "Data type mismatch (length too low)";
    case 0x06090011UL:
      return "Sub-index does not exist";
    case 0x06090030UL:
      return "Value out of range";
    case 0x06090031UL:
      return "Value too high";
    case 0x06090032UL:
      return "Value too low";
    case 0x08000000UL:
      return "General SDO error";
    case 0x08000020UL:
      return "Data cannot be transferred or stored";
    case 0x08000021UL:
      return "Data cannot be transferred (local control)";
    case 0x08000022UL:
      return "Data cannot be transferred in present device state";
    case 0x08000024UL:
      return "No data available";
    default:
      return "Unknown SDO abort code";
  }
}

}  // namespace esphome::bdr_ble
