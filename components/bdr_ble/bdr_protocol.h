#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome::bdr_ble {

inline constexpr const char *TAG = "bdr_ble";

// Default GATT Attribute Handles on EHC-08 (resolved dynamically by UUID on connect)
inline constexpr uint16_t HANDLE_DIRECT_TX = 18;       // 496b1b03 (Write)
inline constexpr uint16_t HANDLE_DIRECT_RX = 20;       // ab9af948 (Notify)
inline constexpr uint16_t HANDLE_DIRECT_RX_CCCD = 21;
inline constexpr uint16_t HANDLE_SVC2_CH1 = 23;        // cff32957 (Notify)
inline constexpr uint16_t HANDLE_SVC2_CH1_CCCD = 24;
inline constexpr uint16_t HANDLE_SVC2_CH2 = 27;        // 5b5dcf8d (Notify)
inline constexpr uint16_t HANDLE_SVC2_CH2_CCCD = 28;
inline constexpr uint16_t HANDLE_SVC3_TRIG = 31;       // 94e54dc4 (Write 0 bytes)
inline constexpr uint16_t HANDLE_SVC3_CHAL = 33;       // 69092300 (Notify 8 bytes)
inline constexpr uint16_t HANDLE_SVC3_CHAL_CCCD = 34;
inline constexpr uint16_t HANDLE_SVC3_RESP = 36;       // f1ca5aeb (Write 9 bytes)
inline constexpr uint16_t HANDLE_SVC3_ACK = 38;        // e2c47825 (Notify 1 byte)
inline constexpr uint16_t HANDLE_SVC3_ACK_CCCD = 39;

// 128-bit Characteristic UUIDs for dynamic GATT handle discovery (EHC-08, EHC-16, CU-*, GTW-22, GTW-IoT)
inline constexpr const char *UUID_DIRECT_TX = "496b1b03-cebc-4d59-9c32-14ea88c266f9";
inline constexpr const char *UUID_DIRECT_RX = "ab9af948-fb86-492d-820d-bdea2dcb7ecf";
inline constexpr const char *UUID_SVC2_CH1 = "cff32957-5279-4075-bf3d-8e583d418c0d";
inline constexpr const char *UUID_SVC2_CH2 = "5b5dcf8d-57f0-455e-b88e-e8e080031032";
inline constexpr const char *UUID_SVC3_TRIG = "94e54dc4-f69a-4c0c-8a6f-cc8f3135c665";
inline constexpr const char *UUID_SVC3_CHAL = "69092300-a182-4fea-803b-24ada406da8a";
inline constexpr const char *UUID_SVC3_RESP = "f1ca5aeb-02cd-44a8-972b-44b6e7250d0b";
inline constexpr const char *UUID_SVC3_ACK = "e2c47825-6dff-4d79-b74a-5bdc4f4661cf";

// DirectChannel & CAN-IP / RUB v2.01 protocol constants
inline constexpr uint8_t DIRECT_CHANNEL_CAN = 0x01;
inline constexpr uint8_t SEGMENT_FINAL = 0xFF;
inline constexpr size_t DIRECT_TX_MAX_CHUNK_DATA = 19;  // 1B seq + 19B payload = 20B BLE packet

inline constexpr uint8_t MSG_TYPE_SDO = 0x00;
inline constexpr uint8_t MSG_TYPE_RUB_AUTH = 0x02;

inline constexpr uint8_t FUNC_READ_REQ = 0x00;
inline constexpr uint8_t FUNC_READ_POS_ACK = 0x01;
inline constexpr uint8_t FUNC_READ_NEG_ACK = 0x02;
inline constexpr uint8_t FUNC_WRITE_UNCONFIRMED_REQ = 0x03;
inline constexpr uint8_t FUNC_WRITE_CONFIRMED_REQ = 0x04;
inline constexpr uint8_t FUNC_WRITE_POS_ACK = 0x05;
inline constexpr uint8_t FUNC_WRITE_NEG_ACK = 0x06;
inline constexpr uint8_t FUNC_GET_LIST_REQ = 0x08;
inline constexpr uint8_t FUNC_GET_LIST_RESP = 0x09;

inline constexpr uint8_t RUB_FUNC_CHALLENGE_REQ = 0x00;
inline constexpr uint8_t RUB_FUNC_CHALLENGE_RESP = 0x01;
inline constexpr uint8_t RUB_FUNC_AUTH_REQ = 0x02;
inline constexpr uint8_t RUB_FUNC_AUTH_ACK = 0x03;

// Dynamic appliance menu descriptor table index (SubIndex 1..15)
inline constexpr uint16_t INDEX_CAN_DEVICE_TYPE = 0x1000;
inline constexpr uint16_t INDEX_AP_MENU_DESCRIPTOR = 0x502B;
inline constexpr uint8_t SNAPSHOT_MAX_SUB_INDEX = 15;

// Timeouts aligned with boiler's 1000 ms CAN SDO timeout and 10000 ms RUB session timeout
inline constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 3000;
inline constexpr uint32_t REQUEST_TIMEOUT_MS = 1500;
inline constexpr uint32_t KEEPALIVE_INTERVAL_MS = 5000;
inline constexpr uint32_t AUTH_COOLDOWN_MS = 30000;
inline constexpr uint32_t PAIRING_TIMEOUT_MS = 30000;
inline constexpr uint32_t PAIRING_BACKOFF_SECONDS[4] = {90, 180, 300, 600};
inline constexpr uint32_t MIN_TX_INTERVAL_MS = 60;
inline constexpr size_t MAX_WRITE_QUEUE_SIZE = 16;
inline constexpr uint8_t MAX_CONSECUTIVE_TIMEOUTS = 10;

// SDO Abort codes that indicate a permanently unsupported object/sub-index
inline constexpr uint32_t SDO_ABORT_UNSUPPORTED_ACCESS = 0x06010000UL;
inline constexpr uint32_t SDO_ABORT_WRITE_ONLY = 0x06010001UL;
inline constexpr uint32_t SDO_ABORT_OBJECT_NOT_EXIST = 0x06020000UL;
inline constexpr uint32_t SDO_ABORT_SUBINDEX_NOT_EXIST = 0x06090011UL;

enum BdrDataType : uint8_t {
  BDR_TYPE_U8 = 0,
  BDR_TYPE_I8 = 1,
  BDR_TYPE_U16 = 2,
  BDR_TYPE_I16 = 3,
  BDR_TYPE_U32 = 4,
  BDR_TYPE_I32 = 5,
  BDR_TYPE_ENUM = 6,
  BDR_TYPE_VISIBLE_STRING = 7,
  BDR_TYPE_OCTET_STRING = 8,
  BDR_TYPE_TIME_OF_DAY = 9,
};

/// Reflected CRC-16 (poly 0xA001, init 0xFFFF) used by BDR DirectChannel frames.
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

/// 32-round TEA block cipher used for Service 3 and RUB v2.01 challenge-response.
void tea_encrypt(uint8_t auth_level, uint32_t id1, uint32_t id2,
                 uint32_t &out_v0, uint32_t &out_v1);

/// Wrap a raw CAN-IP / RUB payload in a DirectChannel frame (0x01 prefix + big-endian CRC16).
std::vector<uint8_t> wrap_direct_channel_frame(const uint8_t *payload, size_t len);

/// Decode a Little-Endian numeric or enum datapoint payload into a scaled float and raw enum integer.
/// Returns false if the value matches the BDR invalid/unplugged sentinel (0xFF, -128, 0xFFFF, -32768, etc.).
bool decode_numeric_datapoint(BdrDataType data_type, float scale,
                              const uint8_t *data, size_t data_len,
                              float &out_numeric, int32_t &out_raw_enum);

/// Encode a scaled float value into Little-Endian wire bytes according to BdrDataType.
bool encode_numeric_datapoint(BdrDataType data_type, float scale, float value,
                              std::vector<uint8_t> &out_bytes);

/// Return human-readable description for ESP-IDF BLE SMP authentication failure reason (0x4D + SMP code).
const char *ble_auth_fail_reason_str(uint8_t reason);

/// Return true if BLE auth failure is a transient timeout/busy error where stored bond key is still valid.
bool is_transient_ble_auth_failure(uint8_t reason);

/// Return human-readable description for a 32-bit CANopen SDO abort code.
const char *sdo_abort_code_str(uint32_t abort_code);

}  // namespace esphome::bdr_ble
