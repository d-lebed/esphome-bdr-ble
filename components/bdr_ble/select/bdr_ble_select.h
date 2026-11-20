#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/helpers.h"
#include "../bdr_ble.h"
#include "../bdr_registry.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome::bdr_ble {

class BdrBleSelect final : public select::Select, public Parented<BdrBleComponent> {
 public:
  void set_datapoint_meta(uint8_t node_id, uint16_t index, uint8_t sub_index) {
    this->node_id_ = node_id;
    this->index_ = index;
    this->sub_index_ = sub_index;
  }

  void set_enum_table(const EnumEntry *table, size_t len) {
    this->enum_table_ = table;
    this->enum_table_len_ = len;
  }

 protected:
  void control(const std::string &value) override;

  uint8_t node_id_{0x01};
  uint16_t index_{0};
  uint8_t sub_index_{0};
  const EnumEntry *enum_table_{nullptr};
  size_t enum_table_len_{0};
};

}  // namespace esphome::bdr_ble
