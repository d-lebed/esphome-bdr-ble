#include "bdr_ble_select.h"
#include "../bdr_protocol.h"

namespace esphome::bdr_ble {

void BdrBleSelect::control(const std::string &value) {
  if (this->parent_ == nullptr) {
    return;
  }
  auto it = this->label_to_val_.find(value);
  if (it == this->label_to_val_.end()) {
    ESP_LOGW(TAG, "Unknown select option '%s' for 0x%04X:%02X", value.c_str(),
             this->index_, this->sub_index_);
    return;
  }
  int raw_val = it->second;
  std::vector<uint8_t> bytes = {static_cast<uint8_t>(raw_val & 0xFF)};
  this->publish_state(value);
  uint8_t resolved_node = (this->node_id_ == 0xFF)
                              ? this->parent_->get_default_node_id()
                              : this->node_id_;
  this->parent_->queue_write_datapoint(resolved_node, this->index_,
                                       this->sub_index_, bytes);
}

}  // namespace esphome::bdr_ble
