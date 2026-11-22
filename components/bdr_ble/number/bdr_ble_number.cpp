#include "bdr_ble_number.h"

#include <cmath>
#include <vector>

namespace esphome::bdr_ble {

uint8_t BdrBleNumber::resolved_node_id_() const {
  if (this->node_id_ == 0xFF && this->parent_ != nullptr) {
    return this->parent_->get_default_node_id();
  }
  return (this->node_id_ == 0xFF) ? 0x01 : this->node_id_;
}

uint32_t BdrBleNumber::pref_hash_() const {
  uint32_t h = 0x811C9DC5UL;  // FNV-1a over stable tag + CANopen address
  const uint8_t key[] = {
      'b',
      'd',
      'r',
      'N',
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

void BdrBleNumber::setup() {
  if (!this->use_device_limits_) {
    return;
  }
  this->pref_ =
      global_preferences->make_preference<NumberLimitsPref>(this->pref_hash_(), true);
  this->pref_initialized_ = true;

  NumberLimitsPref p{};
  if (this->pref_.load(&p) && p.magic == PREF_MAGIC &&
      std::isfinite(p.min_val) && std::isfinite(p.max_val) &&
      p.min_val < p.max_val) {
    this->traits.set_min_value(p.min_val);
    this->traits.set_max_value(p.max_val);
    const bool has_step = std::isfinite(p.step) && p.step > 0.0f;
    if (has_step) {
      this->traits.set_step(p.step);
    }
    this->dev_min_ = p.min_val;
    this->dev_max_ = p.max_val;
    this->dev_step_ = has_step ? p.step : NAN;
    this->limits_known_ = true;
    this->limits_persisted_ = true;
    ESP_LOGD(TAG,
             "Restored saved 0x502B limits for 0x%04X:%02X (min=%.2f, max=%.2f, step=%.2f)",
             this->index_, this->sub_index_, p.min_val, p.max_val,
             this->traits.get_step());
  }
}

void BdrBleNumber::apply_device_limits(float min_val, float max_val,
                                       float step) {
  if (!this->use_device_limits_ || !std::isfinite(min_val) ||
      !std::isfinite(max_val) || min_val >= max_val) {
    return;
  }
  const float norm_step = (std::isfinite(step) && step > 0.0f) ? step : NAN;
  const bool step_same =
      (std::isnan(norm_step) && std::isnan(this->dev_step_)) ||
      (!std::isnan(norm_step) && !std::isnan(this->dev_step_) &&
       std::fabs(norm_step - this->dev_step_) < 1e-4f);
  const bool same = this->limits_persisted_ &&
                    std::fabs(min_val - this->dev_min_) < 1e-4f &&
                    std::fabs(max_val - this->dev_max_) < 1e-4f && step_same;

  this->traits.set_min_value(min_val);
  this->traits.set_max_value(max_val);
  if (!std::isnan(norm_step)) {
    this->traits.set_step(norm_step);
  }
  this->dev_min_ = min_val;
  this->dev_max_ = max_val;
  this->dev_step_ = norm_step;
  this->limits_known_ = true;

  if (same) {
    return;
  }

  if (!this->pref_initialized_) {
    this->pref_ = global_preferences->make_preference<NumberLimitsPref>(
        this->pref_hash_(), true);
    this->pref_initialized_ = true;
  }
  NumberLimitsPref p{min_val, max_val, norm_step, PREF_MAGIC};
  this->pref_.save(&p);
  this->limits_persisted_ = true;
  ESP_LOGI(
      TAG,
      "Saved 0x502B limits for 0x%04X:%02X (min=%.2f, max=%.2f, step=%.2f) to flash "
      "(HA UI will show updated limits after next API reconnect or reboot)",
      this->index_, this->sub_index_, min_val, max_val,
      this->traits.get_step());
}

void BdrBleNumber::control(float value) {
  if (this->parent_ == nullptr) {
    return;
  }
  if (!std::isfinite(value)) {
    ESP_LOGW(TAG, "Refusing non-finite number write for 0x%04X:%02X",
             this->index_, this->sub_index_);
    if (this->has_state()) {
      this->publish_state(this->state);
    }
    return;
  }

  const bool enforce_hw = this->use_device_limits_ && this->limits_known_;
  const float min_bound =
      enforce_hw ? this->dev_min_ : this->traits.get_min_value();
  const float max_bound =
      enforce_hw ? this->dev_max_ : this->traits.get_max_value();
  if (std::isfinite(min_bound) && std::isfinite(max_bound) &&
      min_bound < max_bound &&
      (value < min_bound - 1e-4f || value > max_bound + 1e-4f)) {
    ESP_LOGW(
        TAG,
        "Refusing number write %.2f for 0x%04X:%02X: outside %s range [%.2f..%.2f]",
        value, this->index_, this->sub_index_,
        enforce_hw ? "hardware" : "configured", min_bound, max_bound);
    if (this->has_state()) {
      this->publish_state(this->state);
    }
    return;
  }

  std::vector<uint8_t> bytes;
  if (!encode_numeric_datapoint(this->data_type_, this->scale_, value, bytes)) {
    ESP_LOGW(TAG, "Failed to encode value %.2f for 0x%04X:%02X", value,
             this->index_, this->sub_index_);
    if (this->has_state()) {
      this->publish_state(this->state);
    }
    return;
  }
  uint8_t resolved_node = this->resolved_node_id_();
  if (!this->parent_->queue_write_datapoint(resolved_node, this->index_,
                                            this->sub_index_, bytes)) {
    ESP_LOGW(TAG, "Write for 0x%04X:%02X rejected (BLE not connected)",
             this->index_, this->sub_index_);
    if (this->has_state()) {
      this->publish_state(this->state);
    }
  }
}

}  // namespace esphome::bdr_ble
