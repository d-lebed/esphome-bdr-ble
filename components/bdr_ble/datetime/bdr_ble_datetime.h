#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/datetime/datetime_entity.h"
#include "../bdr_ble.h"
#include "../bdr_protocol.h"

#include <cstdint>

namespace esphome::bdr_ble {

class BdrBleDateTime final : public datetime::DateTimeEntity,
                             public Parented<BdrBleComponent> {
 public:
  void set_datapoint_meta(uint8_t node_id, uint16_t index, uint8_t sub_index) {
    this->node_id_ = node_id;
    this->index_ = index;
    this->sub_index_ = sub_index;
  }

  /// Apply a decoded CiA 301 TIME_OF_DAY value read back from the boiler.
  void apply_boiler_value(const BdrDateTime &dt);

  /// Clear (unset) this timestamp on the boiler by writing 6 zero bytes (00 00 00 00 00 00).
  void clear();

 protected:
  void control(const datetime::DateTimeCall &call) override;

  uint8_t node_id_{0x01};
  uint16_t index_{0};
  uint8_t sub_index_{0};
};

template <typename... Ts> class ClearDateTimeAction : public Action<Ts...> {
 public:
  explicit ClearDateTimeAction(BdrBleDateTime *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->clear(); }

 protected:
  BdrBleDateTime *parent_;
};

}  // namespace esphome::bdr_ble
