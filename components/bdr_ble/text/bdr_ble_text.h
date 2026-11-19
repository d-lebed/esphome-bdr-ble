#pragma once

#include "esphome/components/text/text.h"
#include "esphome/core/helpers.h"
#include "../bdr_ble.h"

#include <cstdint>
#include <string>

namespace esphome::bdr_ble {

class BdrBleText final : public text::Text, public Parented<BdrBleComponent> {
 public:
  void set_datapoint_meta(uint8_t node_id, uint16_t index, uint8_t sub_index,
                          uint8_t max_len) {
    this->node_id_ = node_id;
    this->index_ = index;
    this->sub_index_ = sub_index;
    this->max_len_ = max_len;
  }

 protected:
  void control(const std::string &value) override;

  uint8_t node_id_{0x01};
  uint16_t index_{0};
  uint16_t sub_index_{0};
  uint8_t max_len_{20};
};

}  // namespace esphome::bdr_ble
