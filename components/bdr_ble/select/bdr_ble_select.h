#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "../bdr_ble.h"
#include "../bdr_registry.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome::bdr_ble {

class BdrBleSelect final : public select::Select,
                           public Component,
                           public Parented<BdrBleComponent> {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_datapoint_meta(uint8_t node_id, uint16_t index, uint8_t sub_index) {
    this->node_id_ = node_id;
    this->index_ = index;
    this->sub_index_ = sub_index;
  }

  void set_enum_table(const EnumEntry *table, size_t len) {
    this->enum_table_ = table;
    this->enum_table_len_ = len;
  }

  void set_use_device_limits(bool use) { this->use_device_limits_ = use; }

  /// Filter options to 0x502B hardware [raw_min..raw_max] bounds and persist them to NVS flash when changed.
  void apply_device_bounds(uint32_t raw_min, uint32_t raw_max);

 protected:
  void control(const std::string &value) override;

  struct SelectBoundsPref {
    uint32_t raw_min;
    uint32_t raw_max;
    uint32_t magic;
  };
  static constexpr uint32_t PREF_MAGIC = 0x42445331UL;  // "BDS1"

  uint8_t resolved_node_id_() const;
  uint32_t pref_hash_() const;
  bool filter_options_(uint32_t raw_min, uint32_t raw_max, size_t &out_count);

  uint8_t node_id_{0x01};
  uint16_t index_{0};
  uint8_t sub_index_{0};
  const EnumEntry *enum_table_{nullptr};
  size_t enum_table_len_{0};
  bool use_device_limits_{true};
  bool bounds_known_{false};
  bool bounds_persisted_{false};
  bool pref_initialized_{false};
  uint32_t dev_min_{0};
  uint32_t dev_max_{0};
  ESPPreferenceObject pref_;
};

}  // namespace esphome::bdr_ble
