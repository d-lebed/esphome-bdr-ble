#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "../bdr_ble.h"
#include "../bdr_protocol.h"

#include <cmath>
#include <cstdint>

namespace esphome::bdr_ble {

class BdrBleNumber final : public number::Number,
                           public Component,
                           public Parented<BdrBleComponent> {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_datapoint_meta(uint8_t node_id, uint16_t index, uint8_t sub_index,
                          uint8_t data_type, float scale) {
    this->node_id_ = node_id;
    this->index_ = index;
    this->sub_index_ = sub_index;
    this->data_type_ = static_cast<BdrDataType>(data_type);
    this->scale_ = scale;
  }

  void set_use_device_limits(bool use) { this->use_device_limits_ = use; }

  /// Apply hardware min/max/step limits from 0x502B and persist them to NVS flash when changed.
  void apply_device_limits(float min_val, float max_val, float step);

 protected:
  void control(float value) override;

  struct NumberLimitsPref {
    float min_val;
    float max_val;
    float step;
    uint32_t magic;
  };
  static constexpr uint32_t PREF_MAGIC = 0x42444E31UL;  // "BDN1"

  uint8_t resolved_node_id_() const;
  uint32_t pref_hash_() const;

  uint8_t node_id_{0x01};
  uint16_t index_{0};
  uint8_t sub_index_{0};
  BdrDataType data_type_{BDR_TYPE_U16};
  float scale_{1.0f};
  bool use_device_limits_{true};
  bool limits_known_{false};
  bool limits_persisted_{false};
  bool pref_initialized_{false};
  float dev_min_{0.0f};
  float dev_max_{0.0f};
  float dev_step_{NAN};
  ESPPreferenceObject pref_;
};

}  // namespace esphome::bdr_ble
