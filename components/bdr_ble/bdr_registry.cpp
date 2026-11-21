#include "bdr_registry.h"

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

#ifdef USE_SELECT
inline const char *select_option_c_str(const char *s) { return s; }
inline const char *select_option_c_str(const std::string &s) { return s.c_str(); }

template <typename TraitsT>
void apply_dynamic_select_options(TraitsT &traits,
                                  const std::vector<const char *> &opts) {
  if constexpr (requires { traits.set_options(FixedVector<const char *>{}); }) {
    FixedVector<const char *> fv;
    fv.init(opts.size());
    for (const char *s : opts) {
      fv.push_back(s);
    }
    traits.set_options(fv);
  } else if constexpr (requires { traits.set_options(std::vector<std::string>{}); }) {
    std::vector<std::string> sv;
    sv.reserve(opts.size());
    for (const char *s : opts) {
      sv.emplace_back(s);
    }
    traits.set_options(sv);
  }
}
#endif

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
  this->discovered_datapoints_.reserve(256);
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

void BdrDatapointRegistry::on_connect() {
  this->discovered_datapoints_.clear();
  this->snapshot_logging_active_ = false;
  this->snapshot_header_logged_ = false;
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

uint8_t BdrDatapointRegistry::get_discovered_level(uint16_t index,
                                                   uint8_t sub_index) const {
  uint32_t packed_key = (static_cast<uint32_t>(index) << 8) | sub_index;
  for (uint32_t entry : this->discovered_datapoints_) {
    if ((entry >> 8) == packed_key) {
      return static_cast<uint8_t>(entry & 0xFF);
    }
  }
  return 0;
}

void BdrDatapointRegistry::handle_snapshot_descriptor(uint8_t node_id,
                                                      uint8_t sub_index,
                                                      const uint8_t *data,
                                                      size_t data_len) {
  if (data_len < 4) {
    ESP_LOGD(TAG, "Snapshot 0x502B:%02X -> %zu bytes (empty)", sub_index,
             data_len);
    return;
  }
  uint16_t count =
      static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  uint16_t parsed = 0;
  uint8_t dp_lvl = ((sub_index - 1) % 5) + 1;

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
#ifdef USE_NUMBER
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
        bool valid_range = is_signed ? (static_cast<int32_t>(raw_min_u) <
                                        static_cast<int32_t>(raw_max_u))
                                     : (raw_min_u < raw_max_u);
        if (valid_range) {
          dp->has_descriptor_bounds = true;
          dp->raw_min = raw_min_u;
          dp->raw_max = raw_max_u;
#ifdef USE_NUMBER
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
            if (min_f < max_f) {
              for (auto *num : dp->numbers) {
                num->traits.set_min_value(min_f);
                num->traits.set_max_value(max_f);
                if (raw_inc > 0 && raw_inc != 0xFF) {
                  num->traits.set_step(static_cast<float>(raw_inc) * dp->scale);
                }
              }
              ESP_LOGI(TAG,
                       "Updated limits for 0x%04X:%02X from 0x502B: min=%.2f max=%.2f",
                       dp_idx, dp_sub, min_f, max_f);
            }
          }
#endif
#ifdef USE_SELECT
          if (!dp->selects.empty() && dp->enum_table != nullptr &&
              dp->enum_table_len > 0) {
            std::vector<const char *> filtered;
            filtered.reserve(dp->enum_table_len);
            for (size_t i = 0; i < dp->enum_table_len; i++) {
              const auto &entry = dp->enum_table[i];
              if (entry.label == nullptr || entry.value < raw_min_u ||
                  entry.value > raw_max_u) {
                continue;
              }
              bool dup = false;
              for (const char *existing : filtered) {
                if (std::strcmp(existing, entry.label) == 0) {
                  dup = true;
                  break;
                }
              }
              if (!dup) {
                filtered.push_back(entry.label);
              }
            }
            if (!filtered.empty()) {
              bool updated_any = false;
              for (auto *sel : dp->selects) {
                const auto &cur = sel->traits.get_options();
                bool same = (cur.size() == filtered.size());
                if (same) {
                  for (size_t i = 0; i < filtered.size(); i++) {
                    if (std::strcmp(select_option_c_str(cur[i]), filtered[i]) != 0) {
                      same = false;
                      break;
                    }
                  }
                }
                if (!same) {
                  std::string prev_label;
                  bool had_prev = false;
                  if (auto idx = sel->active_index();
                      idx.has_value() && *idx < cur.size()) {
                    prev_label = select_option_c_str(cur[*idx]);
                    had_prev = true;
                  }
                  apply_dynamic_select_options(sel->traits, filtered);
                  if (had_prev) {
                    sel->publish_state(prev_label);
                  }
                  updated_any = true;
                }
              }
              if (updated_any) {
                ESP_LOGI(
                    TAG,
                    "Updated select options for 0x%04X:%02X from 0x502B: %zu options (range %u..%u)",
                    dp_idx, dp_sub, filtered.size(),
                    static_cast<unsigned>(raw_min_u),
                    static_cast<unsigned>(raw_max_u));
              }
            }
          }
#endif
        }
      }
    }

    uint32_t packed_key = (static_cast<uint32_t>(dp_idx) << 8) | dp_sub;
    bool exists = false;
    for (uint32_t existing : this->discovered_datapoints_) {
      if ((existing >> 8) == packed_key) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      this->discovered_datapoints_.push_back((packed_key << 8) | dp_lvl);
    }
    parsed++;
  }
  ESP_LOGI(TAG,
           "Snapshot 0x502B:%02X -> %u descriptors (%zu bytes, total unique=%zu)",
           sub_index, parsed, data_len, this->discovered_datapoints_.size());
}

void BdrDatapointRegistry::begin_snapshot_logging() {
  this->snapshot_logging_active_ = true;
  this->snapshot_header_logged_ = false;
  this->snapshot_log_cursor_ = 0;
}

bool BdrDatapointRegistry::log_snapshot_chunk(size_t max_lines) {
  if (!this->snapshot_logging_active_) {
    return true;
  }

  if (!this->snapshot_header_logged_) {
    this->snapshot_header_logged_ = true;
    ESP_LOGI(TAG,
             "=== Appliance Snapshot (%zu supported datapoints from 0x502B:01..15) ===",
             this->discovered_datapoints_.size());
    if (this->discovered_datapoints_.empty()) {
      ESP_LOGI(TAG, "=== No 0x502B descriptors returned by appliance ===");
      this->snapshot_logging_active_ = false;
      return true;
    }
  }

  size_t lines_emitted = 0;
  char line[200];
  while (this->snapshot_log_cursor_ < this->discovered_datapoints_.size() &&
         lines_emitted < max_lines) {
    int pos = snprintf(line, sizeof(line), "BDR_SNAPSHOT:");
    while (this->snapshot_log_cursor_ < this->discovered_datapoints_.size() &&
           pos <= 170) {
      uint32_t entry = this->discovered_datapoints_[this->snapshot_log_cursor_++];
      uint16_t idx = static_cast<uint16_t>((entry >> 16) & 0xFFFF);
      uint8_t sub = static_cast<uint8_t>((entry >> 8) & 0xFF);
      uint8_t lvl = static_cast<uint8_t>(entry & 0xFF);
      pos += snprintf(line + pos, sizeof(line) - pos, "%s%04X:%02X@%u",
                      (pos > 13) ? "," : "", idx, sub, lvl);
    }
    if (pos > 13) {
      ESP_LOGI(TAG, "%s", line);
      lines_emitted++;
    }
  }

  if (this->snapshot_log_cursor_ >= this->discovered_datapoints_.size()) {
    ESP_LOGI(TAG,
             "=== End of Appliance Snapshot (paste BDR_SNAPSHOT lines into configurator.html) ===");
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
#ifdef USE_NUMBER
  if (!dp->numbers.empty() && dp->numbers[0] != nullptr) {
    entity_name = entity_log_name(dp->numbers[0], 0);
  }
#endif
#ifdef USE_SELECT
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

#ifdef USE_NUMBER
  if (valid_numeric) {
    for (auto *num : dp->numbers) {
      num->publish_state(numeric_val);
    }
  }
#endif

#ifdef USE_SELECT
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
