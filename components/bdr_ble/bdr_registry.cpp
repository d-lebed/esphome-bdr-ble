#include "bdr_registry.h"

#ifdef USE_BDR_BLE_NUMBER
#include "number/bdr_ble_number.h"
#endif
#ifdef USE_BDR_BLE_SELECT
#include "select/bdr_ble_select.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace esphome::bdr_ble {

namespace {

template <typename T>
inline auto entity_log_name(const T *e, int)
    -> decltype(LOG_STR_ARG(e->get_log_name())) {
  return LOG_STR_ARG(e->get_log_name());
}
template <typename T>
inline const char *entity_log_name(const T *e, long) {
  return e->get_name().c_str();
}

bool extract_raw_int64(BdrDataType data_type, const uint8_t *data,
                       size_t data_len, int64_t &out_raw) {
  switch (data_type) {
    case BDR_TYPE_U8:
    case BDR_TYPE_ENUM:
      if (data_len == 1) {
        out_raw = data[0];
        return true;
      }
      if (data_len == 2 && data_type == BDR_TYPE_ENUM) {
        out_raw = static_cast<uint16_t>(data[0]) |
                  (static_cast<uint16_t>(data[1]) << 8);
        return true;
      }
      return false;
    case BDR_TYPE_I8:
      if (data_len == 1) {
        out_raw = static_cast<int8_t>(data[0]);
        return true;
      }
      return false;
    case BDR_TYPE_U16:
      if (data_len == 2) {
        out_raw = static_cast<uint16_t>(data[0]) |
                  (static_cast<uint16_t>(data[1]) << 8);
        return true;
      }
      return false;
    case BDR_TYPE_I16:
      if (data_len == 2) {
        out_raw = static_cast<int16_t>(
            static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8));
        return true;
      }
      return false;
    case BDR_TYPE_U32:
      if (data_len == 4) {
        out_raw = static_cast<uint32_t>(data[0]) |
                  (static_cast<uint32_t>(data[1]) << 8) |
                  (static_cast<uint32_t>(data[2]) << 16) |
                  (static_cast<uint32_t>(data[3]) << 24);
        return true;
      }
      if (data_len == 2) {
        out_raw = static_cast<uint16_t>(data[0]) |
                  (static_cast<uint16_t>(data[1]) << 8);
        return true;
      }
      return false;
    case BDR_TYPE_I32:
      if (data_len == 4) {
        out_raw = static_cast<int32_t>(
            static_cast<uint32_t>(data[0]) |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24));
        return true;
      }
      return false;
    default:
      return false;
  }
}

PollGroup &find_or_create_group(std::vector<PollGroup> &groups,
                                uint32_t interval_ms) {
  for (auto &grp : groups) {
    if (grp.interval_ms == interval_ms) {
      return grp;
    }
  }
  PollGroup new_grp;
  new_grp.interval_ms = interval_ms;
  new_grp.last_cycle_start_ms = 0;
  groups.push_back(std::move(new_grp));
  return groups.back();
}

}  // namespace

const char *lookup_enum_label(const EnumEntry *table, size_t len,
                              int32_t raw_val) {
  if (table == nullptr || len == 0 || raw_val < 0 || raw_val > 0xFFFF) {
    return nullptr;
  }
  uint16_t target = static_cast<uint16_t>(raw_val);
  for (size_t i = 0; i < len; i++) {
    if (table[i].value == target) {
      return table[i].label;
    }
  }
  return nullptr;
}

bool is_boolean_enum_table(const EnumEntry *table, size_t len) {
  if (table == nullptr || len == 0) {
    return false;
  }
  return lookup_enum_label(table, len, 1) != nullptr &&
         lookup_enum_label(table, len, 0xFF) == nullptr;
}

void BdrDatapointRegistry::reserve() {
  this->discovered_snapshots_.reserve(4);
}

DatapointRegistration *BdrDatapointRegistry::find_datapoint(uint8_t node_id,
                                                            uint16_t index,
                                                            uint8_t sub_index) {
  for (auto &grp : this->poll_groups_) {
    for (auto &dp : grp.items) {
      if (dp.node_id == node_id && dp.index == index &&
          dp.sub_index == sub_index) {
        return &dp;
      }
    }
  }
  return nullptr;
}

const DatapointRegistration *BdrDatapointRegistry::find_datapoint(
    uint8_t node_id, uint16_t index, uint8_t sub_index) const {
  for (const auto &grp : this->poll_groups_) {
    for (const auto &dp : grp.items) {
      if (dp.node_id == node_id && dp.index == index &&
          dp.sub_index == sub_index) {
        return &dp;
      }
    }
  }
  return nullptr;
}

DatapointRegistration &BdrDatapointRegistry::get_or_create_datapoint(
    uint32_t interval_ms, uint8_t resolved_node_id, uint16_t index,
    uint8_t sub_index, BdrDataType data_type, float scale, bool is_counter) {
  // Search all poll groups globally to prevent duplicate registrations across intervals
  for (size_t g = 0; g < this->poll_groups_.size(); g++) {
    auto &grp = this->poll_groups_[g];
    for (size_t i = 0; i < grp.items.size(); i++) {
      auto &item = grp.items[i];
      if (item.node_id == resolved_node_id && item.index == index &&
          item.sub_index == sub_index) {
        // Preserve non-trivial scale if previously registered via binary_sensor/select (scale=1.0)
        if (std::fabs(item.scale - 1.0f) < 1e-6f &&
            std::fabs(scale - 1.0f) > 1e-6f) {
          item.scale = scale;
        }
        if (item.data_type == BDR_TYPE_ENUM && data_type != BDR_TYPE_ENUM) {
          // Keep BDR_TYPE_ENUM if enum, otherwise prefer specific numeric type
        } else if (data_type != BDR_TYPE_U16) {
          item.data_type = data_type;
        }
        if (is_counter) {
          item.is_counter = true;
        }
        // If the newly registered entity requests a faster interval, promote the datapoint
        if (interval_ms < grp.interval_ms) {
          DatapointRegistration moved = std::move(item);
          grp.items.erase(grp.items.begin() + static_cast<ptrdiff_t>(i));
          PollGroup &faster_grp =
              find_or_create_group(this->poll_groups_, interval_ms);
          faster_grp.items.push_back(std::move(moved));
          return faster_grp.items.back();
        }
        return item;
      }
    }
  }

  PollGroup &target_group =
      find_or_create_group(this->poll_groups_, interval_ms);
  DatapointRegistration reg;
  reg.node_id = resolved_node_id;
  reg.index = index;
  reg.sub_index = sub_index;
  reg.data_type = data_type;
  reg.scale = scale;
  reg.is_counter = is_counter;
  target_group.items.push_back(std::move(reg));
  return target_group.items.back();
}

void BdrDatapointRegistry::set_enum_table(uint8_t resolved_node_id,
                                          uint16_t index, uint8_t sub_index,
                                          const EnumEntry *table, size_t len) {
  if (auto *dp = this->find_datapoint(resolved_node_id, index, sub_index)) {
    dp->enum_table = table;
    dp->enum_table_len = len;
  }
}

uint8_t BdrDatapointRegistry::get_datapoint_wire_size(
    uint8_t node_id, uint16_t index, uint8_t sub_index) const {
  if (const auto *dp = this->find_datapoint(node_id, index, sub_index)) {
    if (dp->expected_len > 0) {
      return dp->expected_len;
    }
    if (dp->observed_len > 0) {
      return dp->observed_len;
    }
  }
  return 1;
}

bool BdrDatapointRegistry::check_datapoint_bounds(uint8_t node_id,
                                                  uint16_t index,
                                                  uint8_t sub_index,
                                                  uint32_t raw_val,
                                                  uint32_t &out_min,
                                                  uint32_t &out_max) const {
  out_min = 0;
  out_max = 0;
  const auto *dp = this->find_datapoint(node_id, index, sub_index);
  if (dp == nullptr || !dp->has_descriptor_bounds ||
      dp->raw_min == 0xFFFFFFFFUL || dp->raw_max == 0xFFFFFFFFUL ||
      dp->raw_min >= dp->raw_max) {
    return true;
  }
  out_min = dp->raw_min;
  out_max = dp->raw_max;
  return raw_val >= dp->raw_min && raw_val <= dp->raw_max;
}

BdrDatapointRegistry::NodeSnapshot &
BdrDatapointRegistry::find_or_create_node_snapshot_(uint8_t node_id) {
  for (auto &snap : this->discovered_snapshots_) {
    if (snap.node_id == node_id) {
      return snap;
    }
  }
  NodeSnapshot snap;
  snap.node_id = node_id;
  snap.entries.reserve(256);
  this->discovered_snapshots_.push_back(std::move(snap));
  return this->discovered_snapshots_.back();
}

void BdrDatapointRegistry::on_connect() {
  this->discovered_snapshots_.clear();
  this->snapshot_logging_active_ = false;
  this->snapshot_header_logged_ = false;
  this->snapshot_node_log_idx_ = 0;
  this->snapshot_log_cursor_ = 0;
  for (auto &grp : this->poll_groups_) {
    for (auto &dp : grp.items) {
      dp.unsupported = false;
      dp.single_read_only = false;
    }
  }
}

void BdrDatapointRegistry::reset_poll_timers() {
  for (auto &grp : this->poll_groups_) {
    grp.last_cycle_start_ms = 0;
  }
}

void BdrDatapointRegistry::trigger_immediate_repoll_for_node(uint8_t node_id) {
  for (auto &grp : this->poll_groups_) {
    for (const auto &dp : grp.items) {
      if (dp.node_id == node_id && !dp.unsupported) {
        grp.last_cycle_start_ms = 0;
        break;
      }
    }
  }
}

uint8_t BdrDatapointRegistry::get_discovered_level(uint8_t node_id,
                                                   uint16_t index,
                                                   uint8_t sub_index) const {
  uint32_t packed_key = (static_cast<uint32_t>(index) << 8) | sub_index;
  for (const auto &snap : this->discovered_snapshots_) {
    if (snap.node_id != node_id) {
      continue;
    }
    for (uint32_t entry : snap.entries) {
      if ((entry >> 8) == packed_key) {
        return static_cast<uint8_t>(entry & 0xFF);
      }
    }
  }
  return 0;
}

void BdrDatapointRegistry::handle_snapshot_descriptor(uint8_t node_id,
                                                      uint8_t sub_index,
                                                      const uint8_t *data,
                                                      size_t data_len) {
  if (data_len < 4) {
    ESP_LOGD(TAG, "Snapshot N%u 0x502B:%02X -> %zu bytes (empty)", node_id,
             sub_index, data_len);
    return;
  }
  uint16_t count =
      static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  uint16_t parsed = 0;
  uint8_t dp_lvl = ((sub_index - 1) % 5) + 1;
  auto &node_snap = this->find_or_create_node_snapshot_(node_id);

  for (uint16_t k = 0; k < count; k++) {
    size_t off = 4 + static_cast<size_t>(k) * 15;
    if (off + 15 > data_len) {
      break;
    }
    uint8_t dp_len = data[off + 1];
    uint16_t dp_idx = static_cast<uint16_t>(data[off + 2]) |
                      (static_cast<uint16_t>(data[off + 3]) << 8);
    uint32_t raw_min_u = static_cast<uint32_t>(data[off + 4]) |
                         (static_cast<uint32_t>(data[off + 5]) << 8) |
                         (static_cast<uint32_t>(data[off + 6]) << 16) |
                         (static_cast<uint32_t>(data[off + 7]) << 24);
    uint32_t raw_max_u = static_cast<uint32_t>(data[off + 8]) |
                         (static_cast<uint32_t>(data[off + 9]) << 8) |
                         (static_cast<uint32_t>(data[off + 10]) << 16) |
                         (static_cast<uint32_t>(data[off + 11]) << 24);
    uint8_t dp_sub = data[off + 12];
#ifdef USE_BDR_BLE_NUMBER
    uint8_t raw_inc = data[off + 13];
#endif

    if (auto *dp = this->find_datapoint(node_id, dp_idx, dp_sub)) {
      dp->discovered_level = dp_lvl;
      if (dp_len > 0) {
        dp->expected_len = dp_len;
        if (dp->max_len == 0) {
          dp->max_len = dp_len;
        }
      }
      if (raw_min_u != 0xFFFFFFFFUL && raw_max_u != 0xFFFFFFFFUL) {
        bool is_signed = (dp->data_type == BDR_TYPE_I8 ||
                          dp->data_type == BDR_TYPE_I16 ||
                          dp->data_type == BDR_TYPE_I32);
        // Some extension modules (e.g. Node 4 SCB) pack negative 8/16-bit minimums into
        // the 32-bit 0x502B slot without sign-extension (e.g. 0x0000F63C for -2500).
        if (is_signed &&
            static_cast<int32_t>(raw_min_u) > static_cast<int32_t>(raw_max_u)) {
          if (dp->data_type == BDR_TYPE_I16 && raw_min_u >= 0x8000UL &&
              raw_min_u <= 0xFFFFUL) {
            raw_min_u = static_cast<uint32_t>(
                static_cast<int32_t>(static_cast<int16_t>(raw_min_u)));
          } else if (dp->data_type == BDR_TYPE_I8 && raw_min_u >= 0x80UL &&
                     raw_min_u <= 0xFFUL) {
            raw_min_u = static_cast<uint32_t>(
                static_cast<int32_t>(static_cast<int8_t>(raw_min_u)));
          }
        }
        bool valid_range = is_signed ? (static_cast<int32_t>(raw_min_u) <
                                        static_cast<int32_t>(raw_max_u))
                                     : (raw_min_u < raw_max_u);
        if (valid_range) {
          dp->has_descriptor_bounds = true;
          dp->raw_min = raw_min_u;
          dp->raw_max = raw_max_u;
#ifdef USE_BDR_BLE_NUMBER
          if (!dp->numbers.empty()) {
            float min_f =
                is_signed
                    ? static_cast<float>(static_cast<int32_t>(raw_min_u)) *
                          dp->scale
                    : static_cast<float>(raw_min_u) * dp->scale;
            float max_f =
                is_signed
                    ? static_cast<float>(static_cast<int32_t>(raw_max_u)) *
                          dp->scale
                    : static_cast<float>(raw_max_u) * dp->scale;
            float step_f = (raw_inc > 0 && raw_inc != 0xFF)
                               ? static_cast<float>(raw_inc) * dp->scale
                               : NAN;
            if (min_f < max_f) {
              for (auto *num : dp->numbers) {
                num->apply_device_limits(min_f, max_f, step_f);
              }
            }
          }
#endif
#ifdef USE_BDR_BLE_SELECT
          if (!dp->selects.empty()) {
            for (auto *sel : dp->selects) {
              sel->apply_device_bounds(raw_min_u, raw_max_u);
            }
          }
#endif
        }
      }
    }

    uint32_t packed_key = (static_cast<uint32_t>(dp_idx) << 8) | dp_sub;
    bool exists = false;
    for (uint32_t existing : node_snap.entries) {
      if ((existing >> 8) == packed_key) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      node_snap.entries.push_back((packed_key << 8) | dp_lvl);
    }
    parsed++;
  }
  ESP_LOGI(TAG,
           "Snapshot N%u 0x502B:%02X -> %u descriptors (%zu bytes, node unique=%zu)",
           node_id, sub_index, parsed, data_len, node_snap.entries.size());
}

void BdrDatapointRegistry::begin_snapshot_logging(uint8_t default_node_id) {
  this->snapshot_default_node_id_ = default_node_id;
  this->snapshot_logging_active_ = true;
  this->snapshot_header_logged_ = false;
  this->snapshot_node_log_idx_ = 0;
  this->snapshot_log_cursor_ = 0;
}

bool BdrDatapointRegistry::log_snapshot_chunk(size_t max_lines) {
  if (!this->snapshot_logging_active_) {
    return true;
  }

  if (this->discovered_snapshots_.empty()) {
    ESP_LOGI(TAG, "=== No 0x502B descriptors returned by appliance ===");
    this->snapshot_logging_active_ = false;
    return true;
  }

  size_t lines_emitted = 0;
  while (this->snapshot_node_log_idx_ < this->discovered_snapshots_.size() &&
         lines_emitted < max_lines) {
    const auto &snap = this->discovered_snapshots_[this->snapshot_node_log_idx_];
    const bool is_default = (snap.node_id == this->snapshot_default_node_id_);

    if (!this->snapshot_header_logged_) {
      this->snapshot_header_logged_ = true;
      if (is_default) {
        ESP_LOGI(TAG,
                 "=== Appliance Snapshot (%zu supported datapoints from 0x502B:01..15) ===",
                 snap.entries.size());
      } else {
        ESP_LOGI(TAG,
                 "=== Appliance Snapshot (Node %u: %zu supported datapoints from 0x502B:01..15) ===",
                 snap.node_id, snap.entries.size());
      }
    }

    char prefix[24];
    int prefix_len = 0;
    if (is_default) {
      prefix_len = snprintf(prefix, sizeof(prefix), "BDR_SNAPSHOT:");
    } else {
      prefix_len =
          snprintf(prefix, sizeof(prefix), "BDR_SNAPSHOT[N%u]:", snap.node_id);
    }

    char line[200];
    while (this->snapshot_log_cursor_ < snap.entries.size() &&
           lines_emitted < max_lines) {
      int pos = snprintf(line, sizeof(line), "%s", prefix);
      while (this->snapshot_log_cursor_ < snap.entries.size() && pos <= 170) {
        uint32_t entry = snap.entries[this->snapshot_log_cursor_++];
        uint16_t idx = static_cast<uint16_t>((entry >> 16) & 0xFFFF);
        uint8_t sub = static_cast<uint8_t>((entry >> 8) & 0xFF);
        uint8_t lvl = static_cast<uint8_t>(entry & 0xFF);
        pos += snprintf(line + pos, sizeof(line) - pos, "%s%04X:%02X@%u",
                        (pos > prefix_len) ? "," : "", idx, sub, lvl);
      }
      if (pos > prefix_len) {
        ESP_LOGI(TAG, "%s", line);
        lines_emitted++;
      }
    }

    if (this->snapshot_log_cursor_ >= snap.entries.size()) {
      if (is_default) {
        ESP_LOGI(
            TAG,
            "=== End of Appliance Snapshot (paste BDR_SNAPSHOT lines into configurator.html) ===");
      } else {
        ESP_LOGI(TAG, "=== End of Appliance Snapshot (Node %u) ===",
                 snap.node_id);
      }
      this->snapshot_node_log_idx_++;
      this->snapshot_log_cursor_ = 0;
      this->snapshot_header_logged_ = false;
    }
  }

  if (this->snapshot_node_log_idx_ >= this->discovered_snapshots_.size()) {
    this->snapshot_logging_active_ = false;
    return true;
  }
  return false;
}

void BdrDatapointRegistry::mark_datapoint_unsupported(uint8_t node_id,
                                                      uint16_t index,
                                                      uint8_t sub_index,
                                                      uint8_t status_code,
                                                      uint32_t abort_code) {
  auto *dp = this->find_datapoint(node_id, index, sub_index);
  if (dp == nullptr || dp->unsupported) {
    return;
  }
  dp->unsupported = true;
  const char *entity_name = "unregistered";
#ifdef USE_SENSOR
  if (!dp->sensors.empty() && dp->sensors[0] != nullptr) {
    entity_name = entity_log_name(dp->sensors[0], 0);
  }
#endif
#ifdef USE_BINARY_SENSOR
  if (!dp->binary_sensors.empty() && dp->binary_sensors[0] != nullptr) {
    entity_name = entity_log_name(dp->binary_sensors[0], 0);
  }
#endif
#ifdef USE_TEXT_SENSOR
  if (!dp->text_sensors.empty() && dp->text_sensors[0] != nullptr) {
    entity_name = entity_log_name(dp->text_sensors[0], 0);
  }
#endif
#ifdef USE_BDR_BLE_NUMBER
  if (!dp->numbers.empty() && dp->numbers[0] != nullptr) {
    entity_name = entity_log_name(dp->numbers[0], 0);
  }
#endif
#ifdef USE_BDR_BLE_SELECT
  if (!dp->selects.empty() && dp->selects[0] != nullptr) {
    entity_name = entity_log_name(dp->selects[0], 0);
  }
#endif
#ifdef USE_TEXT
  if (!dp->texts.empty() && dp->texts[0] != nullptr) {
    entity_name = entity_log_name(dp->texts[0], 0);
  }
#endif
  ESP_LOGW(TAG,
           "Datapoint 0x%04X:%02X (%s) reported unsupported by boiler "
           "(status=0x%02X, abort=0x%08X: %s); disabling polling",
           index, sub_index, entity_name, status_code,
           static_cast<unsigned>(abort_code), sdo_abort_code_str(abort_code));
}

void BdrDatapointRegistry::mark_datapoint_single_read_only(
    uint8_t node_id, uint16_t index, uint8_t sub_index, uint8_t status_code,
    uint32_t abort_code) {
  auto *dp = this->find_datapoint(node_id, index, sub_index);
  if (dp != nullptr && !dp->single_read_only) {
    dp->single_read_only = true;
    ESP_LOGI(TAG,
             "Datapoint 0x%04X:%02X returned status=0x%02X (abort=0x%08X: %s) in GetList; "
             "switching to single-read polling",
             index, sub_index, status_code, static_cast<unsigned>(abort_code),
             sdo_abort_code_str(abort_code));
  }
}

void BdrDatapointRegistry::dispatch_datapoint_value(uint8_t node_id,
                                                    uint16_t index,
                                                    uint8_t sub_index,
                                                    const uint8_t *data,
                                                    size_t data_len) {
  if (data == nullptr || data_len == 0) {
    return;
  }

  auto *dp = this->find_datapoint(node_id, index, sub_index);
  if (dp == nullptr) {
    return;
  }

  // CRITICAL-02: Validate hardware wire length from 0x502B before decoding.
  // Fixed-width numeric/enum/TimeOfDay types must match expected_len exactly;
  // variable-length VisibleString/OctetString responses may be shorter if null-terminated,
  // so only reject if data_len exceeds expected_len.
  if (dp->expected_len > 0) {
    bool is_variable_len = (dp->data_type == BDR_TYPE_VISIBLE_STRING ||
                            dp->data_type == BDR_TYPE_OCTET_STRING);
    bool len_invalid = is_variable_len ? (data_len > dp->expected_len)
                                       : (data_len != dp->expected_len);
    if (len_invalid) {
      ESP_LOGW(TAG,
               "Rejecting 0x%04X:%02X payload with invalid length %zu (expected %s%u from 0x502B)",
               index, sub_index, data_len, is_variable_len ? "<=" : "",
               dp->expected_len);
      return;
    }
  }

  float numeric_val = 0.0f;
  int32_t raw_enum_val = 0;
  bool valid_numeric = decode_numeric_datapoint(
      dp->data_type, dp->scale, data, data_len, numeric_val, raw_enum_val,
      dp->expected_len, dp->has_descriptor_bounds, dp->raw_min, dp->raw_max);

  // CRITICAL-02: Counter single-cycle jump guard for total_increasing statistics
  if (valid_numeric && dp->is_counter) {
    int64_t raw_i64 = 0;
    if (extract_raw_int64(dp->data_type, data, data_len, raw_i64)) {
      if (dp->has_last_valid_raw) {
        int64_t raw_delta = raw_i64 - dp->last_valid_raw;
        float scaled_delta = static_cast<float>(raw_delta) * dp->scale;
        if (raw_delta < -10 || scaled_delta > 1000.0f) {
          ESP_LOGW(TAG,
                   "Rejecting implausible single-cycle counter jump on 0x%04X:%02X (prev=%lld, new=%lld, delta=%.2f)",
                   index, sub_index, static_cast<long long>(dp->last_valid_raw),
                   static_cast<long long>(raw_i64), scaled_delta);
          return;
        }
      }
      dp->last_valid_raw = raw_i64;
      dp->has_last_valid_raw = true;
    }
  }

  if (valid_numeric) {
    dp->ever_succeeded = true;
    if (data_len <= 255) {
      dp->observed_len = static_cast<uint8_t>(data_len);
    }
  }

  // LOW-15: Normalize 0xFF (255) -> 1 uniformly for any {0, 1} boolean enum
  if (valid_numeric && dp->data_type == BDR_TYPE_ENUM && raw_enum_val == 0xFF) {
    bool is_bool_enum = is_boolean_enum_table(dp->enum_table, dp->enum_table_len);
#ifdef USE_BINARY_SENSOR
    if (!dp->binary_sensors.empty() && dp->enum_table == nullptr) {
      is_bool_enum = true;
    }
#endif
    if (is_bool_enum) {
      raw_enum_val = 1;
      numeric_val = 1.0f;
    }
  }

  const char *enum_label = nullptr;
  if (valid_numeric && dp->data_type == BDR_TYPE_ENUM &&
      dp->enum_table != nullptr) {
    enum_label =
        lookup_enum_label(dp->enum_table, dp->enum_table_len, raw_enum_val);
  }

#ifdef USE_SENSOR
  if (valid_numeric) {
    for (auto *sens : dp->sensors) {
      sens->publish_state(numeric_val);
    }
  }
#endif

#ifdef USE_BINARY_SENSOR
  if (valid_numeric) {
    bool bval = (dp->data_type == BDR_TYPE_ENUM)
                    ? (raw_enum_val != 0)
                    : (std::fabs(numeric_val) > 1e-6f);
    for (auto *bsens : dp->binary_sensors) {
      bsens->publish_state(bval);
    }
  }
#endif

#ifdef USE_BDR_BLE_NUMBER
  if (valid_numeric) {
    for (auto *num : dp->numbers) {
      num->publish_state(numeric_val);
    }
  }
#endif

#ifdef USE_BDR_BLE_SELECT
  if (enum_label != nullptr) {
    for (auto *sel : dp->selects) {
      sel->publish_state(enum_label);
    }
  }
#endif

#ifdef USE_TEXT
  if (!dp->texts.empty() && dp->data_type == BDR_TYPE_VISIBLE_STRING) {
    std::string str_val;
    if (decode_visible_string(data, data_len, dp->max_len, str_val)) {
      dp->ever_succeeded = true;
      for (auto *txt : dp->texts) {
        txt->publish_state(str_val);
      }
    } else {
      ESP_LOGW(TAG,
               "Ignoring invalid VisibleString payload on 0x%04X:%02X (len=%zu, expected=%u)",
               index, sub_index, data_len, dp->max_len);
    }
  }
#endif

#ifdef USE_TEXT_SENSOR
  if (!dp->text_sensors.empty()) {
    std::string text_val;
    bool valid_text = true;
    if (dp->data_type == BDR_TYPE_ENUM) {
      if (enum_label != nullptr) {
        text_val = enum_label;
      } else if (valid_numeric) {
        char fbuf[24];
        snprintf(fbuf, sizeof(fbuf), "%d", static_cast<int>(raw_enum_val));
        text_val = fbuf;
      } else {
        valid_text = false;
      }
    } else if (dp->data_type == BDR_TYPE_VISIBLE_STRING) {
      if (!decode_visible_string(data, data_len, dp->max_len, text_val)) {
        ESP_LOGW(TAG,
                 "Ignoring invalid VisibleString payload on 0x%04X:%02X (len=%zu, expected=%u)",
                 index, sub_index, data_len, dp->max_len);
        valid_text = false;
      }
    } else if (dp->data_type == BDR_TYPE_OCTET_STRING) {
      char hbuf[96];
      int hpos = 0;
      for (size_t b = 0;
           b < data_len && hpos < static_cast<int>(sizeof(hbuf)) - 4; b++) {
        hpos += snprintf(hbuf + hpos, sizeof(hbuf) - hpos, "%s%02X",
                         (b > 0) ? " " : "", data[b]);
      }
      text_val = hbuf;
    } else if (dp->data_type == BDR_TYPE_TIME_OF_DAY) {
      if (!decode_time_of_day(data, data_len, text_val)) {
        valid_text = false;
      }
    } else {
      valid_text = false;
    }
    if (valid_text) {
      dp->ever_succeeded = true;
      for (auto *ts : dp->text_sensors) {
        ts->publish_state(text_val);
      }
    }
  }
#endif
}

void BdrDatapointRegistry::publish_stale_nan() {
#ifdef USE_SENSOR
  for (auto &grp : this->poll_groups_) {
    for (auto &dp : grp.items) {
      for (auto *sens : dp.sensors) {
        if (sens != nullptr && sens->has_state() &&
            !std::isnan(sens->state)) {
          sens->publish_state(NAN);
        }
      }
    }
  }
#endif
}

}  // namespace esphome::bdr_ble
