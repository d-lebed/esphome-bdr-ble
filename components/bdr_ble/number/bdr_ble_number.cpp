#include "bdr_ble_number.h"

namespace esphome::bdr_ble {

void BdrBleNumber::control(float value) {
  if (this->parent_ == nullptr) {
    return;
  }
  std::vector<uint8_t> bytes;
  if (!encode_numeric_datapoint(this->data_type_, this->scale_, value, bytes)) {
    ESP_LOGW(TAG, "Failed to encode value %.2f for 0x%04X:%02X", value,
             this->index_, this->sub_index_);
    return;
  }
  uint8_t resolved_node = (this->node_id_ == 0xFF)
                              ? this->parent_->get_default_node_id()
                              : this->node_id_;
  if (!this->parent_->queue_write_datapoint(resolved_node, this->index_,
                                            this->sub_index_, bytes)) {
    ESP_LOGW(TAG, "Write for 0x%04X:%02X rejected (BLE not connected)",
             this->index_, this->sub_index_);
  }
}

}  // namespace esphome::bdr_ble
