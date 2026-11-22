#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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
inline constexpr size_t GET_LIST_MAX_ITEMS = 16;
inline constexpr size_t GET_LIST_MAX_RESPONSE_BYTES = 240;

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

// CANopen NMT node count (0x1F85:00) & per-node hardware type (0x2001:02 parApDeviceInfoType)
inline constexpr uint16_t INDEX_CAN_VENDOR_ID = 0x1F85;
inline constexpr uint16_t INDEX_DEVICE_INFO = 0x2001;
inline constexpr uint8_t SUBINDEX_DEVICE_INFO_TYPE = 0x02;
inline constexpr uint8_t MAX_CAN_NODE_PROBE = 16;

// Dynamic appliance menu descriptor table index (SubIndex 1..15)
inline constexpr uint16_t INDEX_AP_MENU_DESCRIPTOR = 0x502B;
inline constexpr uint8_t SNAPSHOT_MAX_SUB_INDEX = 15;

// Timeouts aligned with boiler's CAN SDO / GetList round-trip
inline constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 3000;
inline constexpr uint32_t REQUEST_TIMEOUT_MS = 4000;
inline constexpr uint32_t POST_TIMEOUT_COOLDOWN_MS = 500;
inline constexpr uint32_t AUTH_COOLDOWN_MS = 30000;
inline constexpr uint32_t PAIRING_TIMEOUT_MS = 30000;
inline constexpr uint32_t DISCONNECT_STALE_TIMEOUT_MS = 90000;
inline constexpr uint32_t PAIRING_BACKOFF_SECONDS[4] = {90, 180, 300, 600};
inline constexpr uint32_t MIN_TX_INTERVAL_MS = 60;
inline constexpr size_t MAX_WRITE_QUEUE_SIZE = 16;
inline constexpr uint8_t MAX_CONSECUTIVE_TIMEOUTS = 10;

// Standard CANopen SDO Abort codes
inline constexpr uint32_t SDO_ABORT_TOGGLE_BIT = 0x05030000UL;
inline constexpr uint32_t SDO_ABORT_TIMEOUT = 0x05040000UL;
inline constexpr uint32_t SDO_ABORT_INVALID_CS = 0x05040001UL;
inline constexpr uint32_t SDO_ABORT_UNSUPPORTED_ACCESS = 0x06010000UL;
inline constexpr uint32_t SDO_ABORT_WRITE_ONLY = 0x06010001UL;
inline constexpr uint32_t SDO_ABORT_READ_ONLY = 0x06010002UL;
inline constexpr uint32_t SDO_ABORT_OBJECT_NOT_EXIST = 0x06020000UL;
inline constexpr uint32_t SDO_ABORT_PDO_MAP = 0x06040041UL;
inline constexpr uint32_t SDO_ABORT_PARAM_INCOMPATIBLE = 0x06040043UL;
inline constexpr uint32_t SDO_ABORT_INTERNAL_INCOMPATIBLE = 0x06040047UL;
inline constexpr uint32_t SDO_ABORT_HARDWARE_ERROR = 0x06060000UL;
inline constexpr uint32_t SDO_ABORT_TYPE_MISMATCH = 0x06070010UL;
inline constexpr uint32_t SDO_ABORT_TYPE_LEN_HIGH = 0x06070012UL;
inline constexpr uint32_t SDO_ABORT_TYPE_LEN_LOW = 0x06070013UL;
inline constexpr uint32_t SDO_ABORT_SUBINDEX_NOT_EXIST = 0x06090011UL;
inline constexpr uint32_t SDO_ABORT_VALUE_RANGE = 0x06090030UL;
inline constexpr uint32_t SDO_ABORT_VALUE_HIGH = 0x06090031UL;
inline constexpr uint32_t SDO_ABORT_VALUE_LOW = 0x06090032UL;
inline constexpr uint32_t SDO_ABORT_GENERAL_ERROR = 0x08000000UL;
inline constexpr uint32_t SDO_ABORT_TRANSFER_FAILED = 0x08000020UL;
inline constexpr uint32_t SDO_ABORT_LOCAL_CONTROL = 0x08000021UL;
inline constexpr uint32_t SDO_ABORT_DEVICE_STATE = 0x08000022UL;
inline constexpr uint32_t SDO_ABORT_NO_DATA = 0x08000024UL;

enum class SdoAbortCategory : uint8_t {
  PERMANENT_UNSUPPORTED = 0,
  AUTH_OR_ACCESS_DENIED = 1,
  GET_LIST_INCOMPATIBLE = 2,
  TRANSIENT_ERROR = 3,
};

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

/// Wrap a raw CAN-IP / RUB payload into a caller-provided buffer (avoids heap reallocation).
void wrap_direct_channel_frame_into(const uint8_t *payload, size_t len,
                                    std::vector<uint8_t> &out_frame);

/// Decode a Little-Endian numeric or enum datapoint payload into a scaled float and raw enum integer.
/// Validates exact byte length, BDR invalid/unplugged sentinels, and optional 0x502B hardware min/max bounds.
bool decode_numeric_datapoint(BdrDataType data_type, float scale,
                              const uint8_t *data, size_t data_len,
                              float &out_numeric, int32_t &out_raw_enum,
                              uint8_t expected_len = 0,
                              bool has_bounds = false,
                              uint32_t raw_min = 0,
                              uint32_t raw_max = 0);

/// Encode a scaled float value into Little-Endian wire bytes according to BdrDataType.
bool encode_numeric_datapoint(BdrDataType data_type, float scale, float value,
                              std::vector<uint8_t> &out_bytes,
                              uint8_t wire_size = 0);

/// Encode an enum integer value into 1-byte or 2-byte Little-Endian wire bytes.
bool encode_enum_datapoint(int32_t raw_value, uint8_t wire_size,
                           std::vector<uint8_t> &out_bytes);

/// Decode a VisibleString payload into a validated printable UTF-8 string.
/// Returns false if the payload contains invalid UTF-8 / control characters or a 4-byte error word.
bool decode_visible_string(const uint8_t *data, size_t data_len,
                           uint8_t expected_max_len, std::string &out_str);

/// Return the maximum byte length <= max_bytes of `s` that ends on a valid UTF-8 codepoint boundary.
/// Returns 0 if `s` is empty or contains any control character (< 0x20 or 0x7F) or invalid UTF-8 sequence.
size_t utf8_safe_truncate_len(const std::string &s, size_t max_bytes);

/// Decode a CANopen CiA 301 TIME_OF_DAY (6 bytes: uint32_t ms_since_midnight + uint16_t days_since_1984)
/// or 2-byte [hour, minute] payload into an ISO/time string.
bool decode_time_of_day(const uint8_t *data, size_t data_len,
                        std::string &out_str);

/// Classify a 32-bit CANopen SDO abort code into permanent, auth/access, GetList-incompatible, or transient.
SdoAbortCategory classify_sdo_abort(uint32_t abort_code);

/// Return human-readable description for ESP-IDF BLE SMP authentication failure reason (0x4D + SMP code).
const char *ble_auth_fail_reason_str(uint8_t reason);

/// Return true if BLE auth failure is a transient timeout/busy error where stored bond key is still valid.
bool is_transient_ble_auth_failure(uint8_t reason);

/// Return human-readable description for a 32-bit CANopen SDO abort code.
const char *sdo_abort_code_str(uint32_t abort_code);

}  // namespace esphome::bdr_ble
