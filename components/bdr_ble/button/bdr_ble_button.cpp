#include "bdr_ble_button.h"

namespace esphome::bdr_ble {

void BdrBleDiscoverButton::press_action() {
  if (this->parent_ != nullptr) {
    this->parent_->start_discovery();
  }
}

}  // namespace esphome::bdr_ble
