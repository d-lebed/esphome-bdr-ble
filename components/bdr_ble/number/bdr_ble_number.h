#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/helpers.h"
#include "../bdr_ble.h"
#include "../bdr_protocol.h"

namespace esphome::bdr_ble {

class BdrBleNumber final : public number::Number, public Parented<BdrBleComponent> {
 public:
  void set_datapoint_meta(uint8_t node_id, uint16_t index, uint8_t sub_index,
                          uint8_t data_type, float scale) {
    this->node_id_ = node_id;
    this->index_ = index;
    this->sub_index_ = sub_index;
    this->data_type_ = static_cast<BdrDataType>(data_type);
    this->scale_ = scale;
  }

 protected:
  void control(float value) override;

  uint8_t node_id_{0x01};
  uint16_t index_{0};
  uint8_t sub_index_{0};
  BdrDataType data_type_{BDR_TYPE_U16};
  float scale_{1.0f};
};

}  // namespace esphome::bdr_ble
