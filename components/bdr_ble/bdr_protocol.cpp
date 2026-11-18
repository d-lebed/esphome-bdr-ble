#include "bdr_protocol.h"

#include <cmath>
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

std::vector<uint8_t> wrap_direct_channel_frame(const uint8_t *payload,
                                               size_t len) {
  std::vector<uint8_t> frame(len + 3);
  frame[0] = DIRECT_CHANNEL_CAN;
  std::memcpy(&frame[1], payload, len);
  uint16_t crc = crc16_ccitt(frame.data(), len + 1);
  frame[1 + len] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  frame[2 + len] = static_cast<uint8_t>(crc & 0xFF);
  return frame;
}

bool decode_numeric_datapoint(BdrDataType data_type, float scale,
                              const uint8_t *data, size_t data_len,
                              float &out_numeric, int32_t &out_raw_enum) {
  out_numeric = 0.0f;
  out_raw_enum = 0;
  if (data_len == 0) {
    return false;
  }

  switch (data_type) {
    case BDR_TYPE_U8:
      if (data_len >= 1 && data[0] != 0xFF) {
        out_numeric = static_cast<float>(data[0]) * scale;
        return true;
      }
      return false;

    case BDR_TYPE_I8:
      if (data_len >= 1) {
        int8_t v = static_cast<int8_t>(data[0]);
        if (v != -128) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_U16:
      if (data_len >= 2) {
        uint16_t v = static_cast<uint16_t>(data[0]) |
                     (static_cast<uint16_t>(data[1]) << 8);
        if (v != 0xFFFF) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_I16:
      if (data_len >= 2) {
        int16_t v = static_cast<int16_t>(
            static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8));
        if (v != -32768) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_U32:
      if (data_len >= 4) {
        uint32_t v = static_cast<uint32_t>(data[0]) |
                     (static_cast<uint32_t>(data[1]) << 8) |
                     (static_cast<uint32_t>(data[2]) << 16) |
                     (static_cast<uint32_t>(data[3]) << 24);
        if (v != 0xFFFFFFFFUL) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      } else if (data_len == 2) {
        uint16_t v = static_cast<uint16_t>(data[0]) |
                     (static_cast<uint16_t>(data[1]) << 8);
        if (v != 0xFFFF) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_I32:
      if (data_len >= 4) {
        int32_t v = static_cast<int32_t>(
            static_cast<uint32_t>(data[0]) |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24));
        if (v != static_cast<int32_t>(0x80000000UL)) {
          out_numeric = static_cast<float>(v) * scale;
          return true;
        }
      }
      return false;

    case BDR_TYPE_ENUM:
      if (data_len >= 2) {
        out_raw_enum = static_cast<int32_t>(
            static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8));
      } else {
        out_raw_enum = data[0];
      }
      out_numeric = static_cast<float>(out_raw_enum);
      return true;

    default:
      return false;
  }
}

bool encode_numeric_datapoint(BdrDataType data_type, float scale, float value,
                              std::vector<uint8_t> &out_bytes) {
  out_bytes.clear();
  if (!std::isfinite(value) || !std::isfinite(scale) || scale <= 0.0f) {
    return false;
  }
  int64_t raw = static_cast<int64_t>(std::llround(value / scale));

  switch (data_type) {
    case BDR_TYPE_U8:
    case BDR_TYPE_ENUM:
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

}  // namespace esphome::bdr_ble
