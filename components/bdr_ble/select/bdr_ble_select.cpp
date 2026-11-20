#include "bdr_ble_select.h"
#include "../bdr_protocol.h"

#include <cstring>
#include <vector>

namespace esphome::bdr_ble {

void BdrBleSelect::control(const std::string &value) {
  if (this->parent_ == nullptr || this->enum_table_ == nullptr ||
      this->enum_table_len_ == 0) {
    return;
  }

  int32_t raw_val = -1;
  for (size_t i = 0; i < this->enum_table_len_; i++) {
    if (this->enum_table_[i].label != nullptr &&
        value == this->enum_table_[i].label) {
      raw_val = static_cast<int32_t>(this->enum_table_[i].value);
      break;
    }
  }
  if (raw_val < 0) {
    ESP_LOGW(TAG, "Unknown select option '%s' for 0x%04X:%02X", value.c_str(),
             this->index_, this->sub_index_);
    return;
  }

  uint8_t resolved_node = (this->node_id_ == 0xFF)
                              ? this->parent_->get_default_node_id()
                              : this->node_id_;
  uint8_t wire_size = this->parent_->get_datapoint_wire_size(
      resolved_node, this->index_, this->sub_index_);

  std::vector<uint8_t> bytes;
  if (!encode_enum_datapoint(raw_val, wire_size, bytes)) {
    ESP_LOGW(TAG, "Failed to encode select option '%s' (raw=%d, size=%u) for 0x%04X:%02X",
             value.c_str(), static_cast<int>(raw_val), wire_size, this->index_,
             this->sub_index_);
    return;
  }

  if (!this->parent_->queue_write_datapoint(resolved_node, this->index_,
                                            this->sub_index_, bytes)) {
    ESP_LOGW(TAG, "Write for 0x%04X:%02X rejected (BLE not connected)",
             this->index_, this->sub_index_);
  }
}

}  // namespace esphome::bdr_ble
