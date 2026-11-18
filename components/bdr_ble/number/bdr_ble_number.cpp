#include "bdr_ble_number.h"

namespace esphome::bdr_ble {

void BdrBleNumber::control(float value) {
  if (this->parent_ == nullptr) {
    return;
  }
  std::vector<uint8_t> bytes;
  if (!encode_numeric_datapoint(this->data_type_, this->scale_, value, bytes)) {
    return;
  }
  this->publish_state(value);
  uint8_t resolved_node = (this->node_id_ == 0xFF)
                              ? this->parent_->get_default_node_id()
                              : this->node_id_;
  this->parent_->queue_write_datapoint(resolved_node, this->index_,
                                       this->sub_index_, bytes);
}

}  // namespace esphome::bdr_ble
