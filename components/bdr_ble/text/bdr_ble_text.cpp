#include "bdr_ble_text.h"
#include "../bdr_protocol.h"

#include <cstring>
#include <vector>

namespace esphome::bdr_ble {

void BdrBleText::control(const std::string &value) {
  if (this->parent_ == nullptr || this->max_len_ == 0) {
    return;
  }
  size_t copy_len = utf8_safe_truncate_len(value, this->max_len_);
  if (!value.empty() && copy_len == 0) {
    ESP_LOGW(TAG,
             "Rejecting text write for 0x%04X:%02X containing invalid UTF-8 or control characters",
             this->index_, static_cast<unsigned>(this->sub_index_));
    return;
  }

  std::vector<uint8_t> bytes(this->max_len_, 0x00);
  if (copy_len > 0) {
    std::memcpy(bytes.data(), value.data(), copy_len);
  }

  uint8_t resolved_node = (this->node_id_ == 0xFF)
                              ? this->parent_->get_default_node_id()
                              : this->node_id_;
  if (!this->parent_->queue_write_datapoint(
          resolved_node, this->index_,
          static_cast<uint8_t>(this->sub_index_), bytes)) {
    ESP_LOGW(TAG, "Write for 0x%04X:%02X rejected (BLE not connected)",
             this->index_, static_cast<unsigned>(this->sub_index_));
  }
}

}  // namespace esphome::bdr_ble
