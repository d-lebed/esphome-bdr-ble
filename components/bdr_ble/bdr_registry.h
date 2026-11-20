#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_TEXT
#include "esphome/components/text/text.h"
#endif

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bdr_protocol.h"

namespace esphome::bdr_ble {

/// Flash-backed (.rodata) enum option entry emitted at compile time by obd.py / schema_helpers.py.
struct EnumEntry {
  uint16_t value;
  const char *label;
};

struct DatapointRegistration {
  uint8_t node_id{0x01};
  uint16_t index{0};
  uint8_t sub_index{0};
  BdrDataType data_type{BDR_TYPE_U16};
  float scale{1.0f};
  uint8_t max_len{0};
  uint8_t expected_len{0};
  uint8_t observed_len{0};
  uint8_t discovered_level{0};
  bool has_descriptor_bounds{false};
  uint32_t raw_min{0xFFFFFFFFUL};
  uint32_t raw_max{0xFFFFFFFFUL};
  bool is_counter{false};
  bool has_last_valid_raw{false};
  int64_t last_valid_raw{0};
  bool ever_succeeded{false};
  bool unsupported{false};
  bool single_read_only{false};
  const EnumEntry *enum_table{nullptr};
  size_t enum_table_len{0};

#ifdef USE_SENSOR
  std::vector<sensor::Sensor *> sensors;
#endif
#ifdef USE_BINARY_SENSOR
  std::vector<binary_sensor::BinarySensor *> binary_sensors;
#endif
#ifdef USE_TEXT_SENSOR
  std::vector<text_sensor::TextSensor *> text_sensors;
#endif
#ifdef USE_NUMBER
  std::vector<number::Number *> numbers;
#endif
#ifdef USE_SELECT
  std::vector<select::Select *> selects;
#endif
#ifdef USE_TEXT
  std::vector<text::Text *> texts;
#endif
};

struct PollGroup {
  uint32_t interval_ms{30000};
  uint32_t last_cycle_start_ms{0};
  std::vector<DatapointRegistration> items;
};

/// Owns the globally deduplicated datapoint store, poll group scheduler metadata,
/// 0x502B hardware descriptor metadata, and entity state dispatching.
class BdrDatapointRegistry {
 public:
  void reserve();

  std::vector<PollGroup> &groups() { return this->poll_groups_; }
  const std::vector<PollGroup> &groups() const { return this->poll_groups_; }

  DatapointRegistration &get_or_create_datapoint(
      uint32_t interval_ms, uint8_t resolved_node_id, uint16_t index,
      uint8_t sub_index, BdrDataType data_type, float scale,
      bool is_counter = false);

  DatapointRegistration *find_datapoint(uint8_t node_id, uint16_t index,
                                        uint8_t sub_index);
  const DatapointRegistration *find_datapoint(uint8_t node_id, uint16_t index,
                                              uint8_t sub_index) const;

  void set_enum_table(uint8_t resolved_node_id, uint16_t index,
                      uint8_t sub_index, const EnumEntry *table, size_t len);

  uint8_t get_datapoint_wire_size(uint8_t node_id, uint16_t index,
                                  uint8_t sub_index) const;

  /// Reset per-connection flags (`unsupported`, `single_read_only`, `discovered_datapoints_`)
  void on_connect();

  /// Reset poll cycle timestamps when entering STATE_READY
  void reset_poll_timers();

  /// Return bitmask of all configured node_ids (< 32) across poll groups
  uint32_t get_configured_nodes_mask() const;

  /// Reset last_cycle_start_ms = 0 for any group containing `node_id` so it is re-polled immediately
  void trigger_immediate_repoll_for_node(uint8_t node_id);

  /// Parse a 0x502B:01..15 menu descriptor response for `node_id`
  void handle_snapshot_descriptor(uint8_t node_id, uint8_t sub_index,
                                  const uint8_t *data, size_t data_len);

  /// Begin chunked BDR_SNAPSHOT logging across loop() ticks
  void begin_snapshot_logging();
  bool is_snapshot_logging_active() const {
    return this->snapshot_logging_active_;
  }
  /// Emit up to `max_lines` of BDR_SNAPSHOT log output; returns true when finished
  bool log_snapshot_chunk(size_t max_lines = 2);

  /// Return discovered auth level (1..5) from 0x502B if present, else 0
  uint8_t get_discovered_level(uint16_t index, uint8_t sub_index) const;

  void mark_datapoint_unsupported(uint8_t node_id, uint16_t index,
                                  uint8_t sub_index, uint8_t status_code,
                                  uint32_t abort_code = 0);

  void mark_datapoint_single_read_only(uint8_t node_id, uint16_t index,
                                       uint8_t sub_index, uint8_t status_code,
                                       uint32_t abort_code);

  void dispatch_datapoint_value(uint8_t node_id, uint16_t index,
                                uint8_t sub_index, const uint8_t *data,
                                size_t data_len);

  /// Publish NAN to numeric sensor entities after extended BLE disconnect
  void publish_stale_nan();

 protected:
  std::vector<PollGroup> poll_groups_;
  std::vector<uint32_t> discovered_datapoints_;
  bool snapshot_logging_active_{false};
  bool snapshot_header_logged_{false};
  size_t snapshot_log_cursor_{0};
};

/// Look up an enum label in a Flash-backed EnumEntry table.
const char *lookup_enum_label(const EnumEntry *table, size_t len,
                              int32_t raw_val);

/// Return true if an EnumEntry table represents a boolean {0, 1} enum (has 1, lacks 0xFF).
bool is_boolean_enum_table(const EnumEntry *table, size_t len);

}  // namespace esphome::bdr_ble
