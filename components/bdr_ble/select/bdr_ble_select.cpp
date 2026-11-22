#include "bdr_ble_select.h"
#include "../bdr_protocol.h"

#include <cstring>
#include <vector>

namespace esphome::bdr_ble {

namespace {

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

}  // namespace

uint8_t BdrBleSelect::resolved_node_id_() const {
  if (this->node_id_ == 0xFF && this->parent_ != nullptr) {
    return this->parent_->get_default_node_id();
  }
  return (this->node_id_ == 0xFF) ? 0x01 : this->node_id_;
}

uint32_t BdrBleSelect::pref_hash_() const {
  uint32_t h = 0x811C9DC5UL;  // FNV-1a over stable tag + CANopen address
  const uint8_t key[] = {
      'b',
      'd',
      'r',
      'S',
      this->resolved_node_id_(),
      static_cast<uint8_t>((this->index_ >> 8) & 0xFF),
      static_cast<uint8_t>(this->index_ & 0xFF),
      this->sub_index_,
  };
  for (uint8_t b : key) {
    h ^= b;
    h *= 16777619UL;
  }
  return h;
}

bool BdrBleSelect::filter_options_(uint32_t raw_min, uint32_t raw_max,
                                   size_t &out_count) {
  out_count = 0;
  if (this->enum_table_ == nullptr || this->enum_table_len_ == 0 ||
      raw_min >= raw_max) {
    return false;
  }

  std::vector<const char *> filtered;
  filtered.reserve(this->enum_table_len_);
  for (size_t i = 0; i < this->enum_table_len_; i++) {
    const auto &entry = this->enum_table_[i];
    if (entry.label == nullptr || entry.value < raw_min ||
        entry.value > raw_max) {
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
  if (filtered.empty()) {
    return false;
  }

  out_count = filtered.size();
  const auto &cur = this->traits.get_options();
  bool same = (cur.size() == filtered.size());
  if (same) {
    for (size_t i = 0; i < filtered.size(); i++) {
      if (std::strcmp(select_option_c_str(cur[i]), filtered[i]) != 0) {
        same = false;
        break;
      }
    }
  }
  if (same) {
    return false;
  }

  std::string prev_label;
  bool had_prev = false;
  if (auto idx = this->active_index(); idx.has_value() && *idx < cur.size()) {
    prev_label = select_option_c_str(cur[*idx]);
    had_prev = true;
  }
  apply_dynamic_select_options(this->traits, filtered);
  if (had_prev) {
    for (const char *opt : filtered) {
      if (prev_label == opt) {
        this->publish_state(prev_label);
        break;
      }
    }
  }
  return true;
}

void BdrBleSelect::setup() {
  if (!this->use_device_limits_) {
    return;
  }
  this->pref_ =
      global_preferences->make_preference<SelectBoundsPref>(this->pref_hash_(), true);
  this->pref_initialized_ = true;

  SelectBoundsPref p{};
  if (this->pref_.load(&p) && p.magic == PREF_MAGIC && p.raw_min < p.raw_max) {
    this->dev_min_ = p.raw_min;
    this->dev_max_ = p.raw_max;
    this->bounds_known_ = true;
    this->bounds_persisted_ = true;
    size_t count = 0;
    this->filter_options_(p.raw_min, p.raw_max, count);
    ESP_LOGD(
        TAG,
        "Restored saved 0x502B select bounds for 0x%04X:%02X (%zu options, range %u..%u)",
        this->index_, this->sub_index_, count,
        static_cast<unsigned>(p.raw_min), static_cast<unsigned>(p.raw_max));
  }
}

void BdrBleSelect::apply_device_bounds(uint32_t raw_min, uint32_t raw_max) {
  if (!this->use_device_limits_ || raw_min >= raw_max) {
    return;
  }

  size_t count = 0;
  this->filter_options_(raw_min, raw_max, count);
  if (count == 0) {
    return;
  }

  const bool same = this->bounds_persisted_ && this->dev_min_ == raw_min &&
                    this->dev_max_ == raw_max;
  this->dev_min_ = raw_min;
  this->dev_max_ = raw_max;
  this->bounds_known_ = true;

  if (same) {
    return;
  }

  if (!this->pref_initialized_) {
    this->pref_ = global_preferences->make_preference<SelectBoundsPref>(
        this->pref_hash_(), true);
    this->pref_initialized_ = true;
  }
  SelectBoundsPref p{raw_min, raw_max, PREF_MAGIC};
  this->pref_.save(&p);
  this->bounds_persisted_ = true;
  ESP_LOGI(
      TAG,
      "Saved 0x502B select bounds for 0x%04X:%02X (%zu options, range %u..%u) to flash "
      "(HA UI will show updated options after next API reconnect or reboot)",
      this->index_, this->sub_index_, count, static_cast<unsigned>(raw_min),
      static_cast<unsigned>(raw_max));
}

void BdrBleSelect::control(const std::string &value) {
  if (this->parent_ == nullptr || this->enum_table_ == nullptr ||
      this->enum_table_len_ == 0) {
    return;
  }

  int32_t raw_val = -1;
  for (size_t i = 0; i < this->enum_table_len_; i++) {
    if (this->enum_table_[i].label != nullptr &&
        value == this->enum_table_[i].label) {
      raw_val = static_cast<int32_t>(this->enum_table_[i].value);
      break;
    }
  }
  if (raw_val < 0) {
    ESP_LOGW(TAG, "Unknown select option '%s' for 0x%04X:%02X", value.c_str(),
             this->index_, this->sub_index_);
    if (auto idx = this->active_index(); idx.has_value()) {
      this->publish_state(*idx);
    }
    return;
  }

  uint8_t resolved_node = this->resolved_node_id_();
  if (this->use_device_limits_) {
    uint32_t min_bound = 0, max_bound = 0;
    bool out_of_range = false;
    if (this->bounds_known_) {
      min_bound = this->dev_min_;
      max_bound = this->dev_max_;
      out_of_range = (static_cast<uint32_t>(raw_val) < min_bound ||
                      static_cast<uint32_t>(raw_val) > max_bound);
    } else if (!this->parent_->check_datapoint_bounds(
                   resolved_node, this->index_, this->sub_index_,
                   static_cast<uint32_t>(raw_val), min_bound, max_bound)) {
      out_of_range = true;
    }
    if (out_of_range) {
      ESP_LOGW(
          TAG,
          "Refusing select write '%s' (raw=%d) for 0x%04X:%02X: outside hardware range [%u..%u]",
          value.c_str(), static_cast<int>(raw_val), this->index_,
          this->sub_index_, static_cast<unsigned>(min_bound),
          static_cast<unsigned>(max_bound));
      if (auto idx = this->active_index(); idx.has_value()) {
        this->publish_state(*idx);
      }
      return;
    }
  }

  uint8_t wire_size = this->parent_->get_datapoint_wire_size(
      resolved_node, this->index_, this->sub_index_);

  std::vector<uint8_t> bytes;
  if (!encode_enum_datapoint(raw_val, wire_size, bytes)) {
    ESP_LOGW(TAG, "Failed to encode select option '%s' (raw=%d, size=%u) for 0x%04X:%02X",
             value.c_str(), static_cast<int>(raw_val), wire_size, this->index_,
             this->sub_index_);
    if (auto idx = this->active_index(); idx.has_value()) {
      this->publish_state(*idx);
    }
    return;
  }

  if (!this->parent_->queue_write_datapoint(resolved_node, this->index_,
                                            this->sub_index_, bytes)) {
    ESP_LOGW(TAG, "Write for 0x%04X:%02X rejected (BLE not connected)",
             this->index_, this->sub_index_);
    if (auto idx = this->active_index(); idx.has_value()) {
      this->publish_state(*idx);
    }
  }
}

}  // namespace esphome::bdr_ble
