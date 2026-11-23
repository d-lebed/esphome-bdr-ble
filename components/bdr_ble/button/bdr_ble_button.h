#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"
#include "../bdr_ble.h"

namespace esphome::bdr_ble {

class BdrBleDiscoverButton final : public button::Button,
                                   public Parented<BdrBleComponent> {
 protected:
  void press_action() override;
};

}  // namespace esphome::bdr_ble
