#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/helpers.h"
#include "../bdr_ble.h"

#include <map>
#include <string>

namespace esphome::bdr_ble {

class BdrBleSelect final : public select::Select, public Parented<BdrBleComponent> {
 public:
  void set_datapoint_meta(uint8_t node_id, uint16_t index, uint8_t sub_index) {
    this->node_id_ = node_id;
    this->index_ = index;
    this->sub_index_ = sub_index;
  }

  void add_enum_option(int raw_value, const char *label) {
    this->label_to_val_[label] = raw_value;
  }

 protected:
  void control(const std::string &value) override;

  uint8_t node_id_{0x01};
  uint16_t index_{0};
  uint8_t sub_index_{0};
  std::map<std::string, int> label_to_val_;
};

}  // namespace esphome::bdr_ble
