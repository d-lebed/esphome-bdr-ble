#include "bdr_ble_text.h"
#include "../bdr_protocol.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace esphome::bdr_ble {

void BdrBleText::control(const std::string &value) {
  if (this->parent_ == nullptr || this->max_len_ == 0) {
    return;
  }
  size_t copy_len =
      std::min(value.size(), static_cast<size_t>(this->max_len_));
  std::vector<uint8_t> bytes(this->max_len_, 0x00);
  if (copy_len > 0) {
    std::memcpy(bytes.data(), value.data(), copy_len);
  }
  this->publish_state(std::string(value.data(), copy_len));
  uint8_t resolved_node = (this->node_id_ == 0xFF)
                              ? this->parent_->get_default_node_id()
                              : this->node_id_;
  this->parent_->queue_write_datapoint(resolved_node, this->index_,
                                       static_cast<uint8_t>(this->sub_index_),
                                       bytes);
}

}  // namespace esphome::bdr_ble
