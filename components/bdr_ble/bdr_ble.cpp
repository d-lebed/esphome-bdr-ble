#include "bdr_ble.h"

#include <cstdio>
#include <cstring>

extern "C" void BTA_GATTC_WriteCharValue(uint16_t conn_id, uint16_t handle,
                                         uint8_t write_type, uint16_t len,
                                         uint8_t *p_value, uint8_t auth_req);

namespace esphome::bdr_ble {

void BdrBleComponent::setup() {
  ESP_LOGI(TAG, "BDR Thermea BLE component initialized (%zu poll groups)",
           this->poll_groups_.size());
}

void BdrBleComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "BDR Thermea BLE Boiler Controller:");
  ESP_LOGCONFIG(TAG, "  Default Node ID: 0x%02X", this->default_node_id_);
  ESP_LOGCONFIG(TAG, "  Auth Level: %u", this->auth_level_);
  ESP_LOGCONFIG(TAG, "  Poll Groups: %zu", this->poll_groups_.size());
  for (size_t i = 0; i < this->poll_groups_.size(); i++) {
    ESP_LOGCONFIG(TAG, "    Group %zu: interval=%ums, datapoints=%zu", i,
                  static_cast<unsigned>(this->poll_groups_[i].interval_ms),
                  this->poll_groups_[i].items.size());
  }
}

DatapointRegistration &BdrBleComponent::get_or_create_datapoint_(
    uint32_t interval_ms, uint8_t node_id, uint16_t index, uint8_t sub_index,
    BdrDataType data_type, float scale) {
  uint8_t resolved_node = (node_id == 0xFF) ? this->default_node_id_ : node_id;

  PollGroup *target_group = nullptr;
  for (auto &grp : this->poll_groups_) {
    if (grp.interval_ms == interval_ms) {
      target_group = &grp;
      break;
    }
  }
  if (target_group == nullptr) {
    PollGroup new_grp;
    new_grp.interval_ms = interval_ms;
    new_grp.last_cycle_start_ms = 0;
    this->poll_groups_.push_back(new_grp);
    target_group = &this->poll_groups_.back();
  }

  for (auto &item : target_group->items) {
    if (item.node_id == resolved_node && item.index == index &&
        item.sub_index == sub_index) {
      return item;
    }
  }

  DatapointRegistration reg;
  reg.node_id = resolved_node;
  reg.index = index;
  reg.sub_index = sub_index;
  reg.data_type = data_type;
  reg.scale = scale;
  target_group->items.push_back(reg);
  return target_group->items.back();
}

#ifdef USE_SENSOR
void BdrBleComponent::register_sensor(sensor::Sensor *sens, uint16_t index,
                                      uint8_t sub_index, uint8_t data_type,
                                      float scale, uint32_t interval_ms,
                                      uint8_t node_id) {
  auto &dp = this->get_or_create_datapoint_(
      interval_ms, node_id, index, sub_index,
      static_cast<BdrDataType>(data_type), scale);
  dp.sensors.push_back(sens);
}
#endif

#ifdef USE_TEXT_SENSOR
void BdrBleComponent::register_text_sensor(text_sensor::TextSensor *sens,
                                           uint16_t index, uint8_t sub_index,
                                           uint8_t data_type,
                                           uint32_t interval_ms,
                                           uint8_t node_id) {
  auto &dp = this->get_or_create_datapoint_(
      interval_ms, node_id, index, sub_index,
      static_cast<BdrDataType>(data_type), 1.0f);
  dp.text_sensors.push_back(sens);
}
#endif

#if defined(USE_TEXT_SENSOR) || defined(USE_SELECT)
void BdrBleComponent::register_enum_translation(uint16_t index,
                                                uint8_t sub_index, int value,
                                                const char *description) {
  uint64_t key = (static_cast<uint64_t>(index) << 24) |
                 (static_cast<uint64_t>(sub_index) << 16) |
                 (static_cast<uint64_t>(static_cast<uint16_t>(value)));
  this->enum_translations_[key] = description;
}
#endif

#ifdef USE_NUMBER
void BdrBleComponent::register_number(number::Number *num, uint16_t index,
                                      uint8_t sub_index, uint8_t data_type,
                                      float scale, uint32_t interval_ms,
                                      uint8_t node_id) {
  auto &dp = this->get_or_create_datapoint_(
      interval_ms, node_id, index, sub_index,
      static_cast<BdrDataType>(data_type), scale);
  dp.numbers.push_back(num);
}
#endif

#ifdef USE_SELECT
void BdrBleComponent::register_select(select::Select *sel, uint16_t index,
                                      uint8_t sub_index, uint32_t interval_ms,
                                      uint8_t node_id) {
  auto &dp = this->get_or_create_datapoint_(
      interval_ms, node_id, index, sub_index, BDR_TYPE_ENUM, 1.0f);
  dp.selects.push_back(sel);
}
#endif

void BdrBleComponent::queue_write_datapoint(uint8_t node_id, uint16_t index,
                                            uint8_t sub_index,
                                            const std::vector<uint8_t> &data) {
  for (auto &w : this->write_queue_) {
    if (w.node_id == node_id && w.index == index && w.sub_index == sub_index) {
      w.data = data;
      ESP_LOGI(TAG, "Updated queued write for 0x%04X:%02X (%zu bytes)", index,
               sub_index, data.size());
      return;
    }
  }
  PendingWrite pw;
  pw.node_id = node_id;
  pw.index = index;
  pw.sub_index = sub_index;
  pw.data = data;
  this->write_queue_.push_back(pw);
  ESP_LOGI(TAG, "Queued write for 0x%04X:%02X (%zu bytes)", index, sub_index,
           data.size());
}

void BdrBleComponent::publish_status_(const char *msg) {
  ESP_LOGI(TAG, "Status: %s", msg);
#ifdef USE_TEXT_SENSOR
  if (this->status_text_ != nullptr) {
    this->status_text_->publish_state(msg);
  }
#endif
}

void BdrBleComponent::log_rx_hex_(const char *prefix, const uint8_t *data,
                                  uint16_t len, bool info_level) {
  char buf[160];
  int pos = snprintf(buf, sizeof(buf), "%s: ", prefix);
  for (uint16_t i = 0; i < len && pos < static_cast<int>(sizeof(buf)) - 4; i++) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", data[i]);
  }
  if (info_level) {
    ESP_LOGI(TAG, "RX %s", buf);
  } else {
    ESP_LOGD(TAG, "RX %s", buf);
  }
}

void BdrBleComponent::write_cccd_(uint16_t cccd_handle) {
  if (this->parent() == nullptr) {
    return;
  }
  uint8_t val[2] = {0x01, 0x00};
  esp_ble_gattc_write_char_descr(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
      cccd_handle, sizeof(val), val, ESP_GATT_WRITE_TYPE_RSP,
      ESP_GATT_AUTH_REQ_NONE);
}

void BdrBleComponent::send_zero_length_write_(uint16_t char_handle) {
  if (this->parent() == nullptr) {
    return;
  }
  uint16_t bta_conn_id = static_cast<uint16_t>(
      (this->parent()->get_conn_id() << 8) |
      (this->parent()->get_gattc_if() & 0xFF));
  uint8_t dummy = 0;
  ESP_LOGI(TAG, "Triggering Service 3 challenge (0-byte write on handle %u)",
           char_handle);
  BTA_GATTC_WriteCharValue(bta_conn_id, char_handle, 2 /* GATT_WRITE */, 0,
                           &dummy, 0);
}

void BdrBleComponent::send_direct_channel_(const uint8_t *payload, size_t len) {
  if (this->parent() == nullptr || len == 0 || len > 512) {
    return;
  }
  std::vector<uint8_t> frame = wrap_direct_channel_frame(payload, len);

  // Max payload per ATT write is (MTU - 3), minus 1 byte for segment header
  size_t max_att_len = (this->negotiated_mtu_ > 23)
                           ? static_cast<size_t>(this->negotiated_mtu_ - 3)
                           : 20;
  if (max_att_len > 244) {
    max_att_len = 244;
  }
  size_t max_chunk_data = max_att_len - 1;

  size_t offset = 0;
  uint8_t seg_idx = 0;
  while (offset < frame.size()) {
    size_t rem = frame.size() - offset;
    bool is_last = (rem <= max_chunk_data);
    size_t chunk_len = is_last ? rem : max_chunk_data;

    uint8_t pkt[248];
    pkt[0] = is_last ? SEGMENT_FINAL : seg_idx++;
    std::memcpy(&pkt[1], frame.data() + offset, chunk_len);
    esp_ble_gattc_write_char(this->parent()->get_gattc_if(),
                             this->parent()->get_conn_id(), HANDLE_DIRECT_TX,
                             chunk_len + 1, pkt, ESP_GATT_WRITE_TYPE_RSP,
                             ESP_GATT_AUTH_REQ_NONE);
    offset += chunk_len;
  }
}

void BdrBleComponent::send_read_param_(uint8_t node_id, uint16_t index,
                                       uint8_t sub_index) {
  uint8_t req[10] = {
      0x02,
      0x01,
      0x00,
      MSG_TYPE_SDO,
      FUNC_READ_REQ,
      0x03,
      node_id,
      static_cast<uint8_t>((index >> 8) & 0xFF),
      static_cast<uint8_t>(index & 0xFF),
      sub_index,
  };
  this->send_direct_channel_(req, sizeof(req));
}

void BdrBleComponent::send_write_param_(uint8_t node_id, uint16_t index,
                                        uint8_t sub_index, const uint8_t *data,
                                        size_t data_len) {
  if (data_len == 0 || data_len > 32) {
    return;
  }
  std::vector<uint8_t> req(10 + data_len);
  req[0] = 0x02;
  req[1] = 0x01;
  req[2] = 0x00;
  req[3] = MSG_TYPE_SDO;
  req[4] = FUNC_WRITE_CONFIRMED_REQ;
  req[5] = 0x03;
  req[6] = node_id;
  req[7] = static_cast<uint8_t>((index >> 8) & 0xFF);
  req[8] = static_cast<uint8_t>(index & 0xFF);
  req[9] = sub_index;
  std::memcpy(&req[10], data, data_len);
  ESP_LOGI(TAG, "Sending WriteConfirmedRequest for 0x%04X:%02X (%zu bytes)",
           index, sub_index, data_len);
  this->send_direct_channel_(req.data(), req.size());
}

void BdrBleComponent::send_get_list_batch_(
    const PollGroup &grp, const std::vector<size_t> &item_indices,
    bool use_v200_header) {
  size_t count = item_indices.size();
  if (count == 0 || count > 32) {
    return;
  }
  std::vector<uint8_t> req(7 + count * 5);
  req[0] = 0x02;
  req[1] = use_v200_header ? 0x00 : 0x01;
  req[2] = 0x00;
  req[3] = MSG_TYPE_SDO;
  req[4] = FUNC_GET_LIST_REQ;
  req[5] = use_v200_header ? 0x00 : 0x03;
  req[6] = static_cast<uint8_t>(count);

  for (size_t i = 0; i < count; i++) {
    const auto &dp = grp.items[item_indices[i]];
    size_t off = 7 + i * 5;
    req[off + 0] = 0x00;
    req[off + 1] = dp.node_id;
    req[off + 2] = static_cast<uint8_t>((dp.index >> 8) & 0xFF);
    req[off + 3] = static_cast<uint8_t>(dp.index & 0xFF);
    req[off + 4] = dp.sub_index;
  }
  ESP_LOGI(TAG, "Sending GetListRequest (%s, %zu items)",
           use_v200_header ? "v2.00" : "v2.01", count);
  this->send_direct_channel_(req.data(), req.size());
}

void BdrBleComponent::gattc_event_handler(esp_gattc_cb_event_t event,
                                          esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK) {
        this->state_ = STATE_CONNECTED_WAIT_SEC;
        this->cccd_step_ = 0;
        this->retry_count_ = 0;
        this->active_group_idx_ = -1;
        this->waiting_response_ = false;
        this->waiting_for_get_list_ = false;
        this->get_list_stage_ = 0;
        this->negotiated_mtu_ = 23;
        this->rx_reassembly_buf_.clear();
        this->discovered_datapoints_.clear();
        for (auto &grp : this->poll_groups_) {
          for (auto &dp : grp.items) {
            dp.unsupported = false;
          }
        }
        this->last_action_ms_ = millis();
        this->publish_status_("Connected (pairing...)");
        if (this->parent() != nullptr) {
          this->parent()->pair();
        }
      }
      break;

    case ESP_GATTC_CFG_MTU_EVT:
      if (param->cfg_mtu.status == ESP_GATT_OK) {
        this->negotiated_mtu_ = param->cfg_mtu.mtu;
        ESP_LOGI(TAG, "Negotiated BLE ATT MTU: %u", this->negotiated_mtu_);
      }
      break;

    case ESP_GATTC_DISCONNECT_EVT:
      this->state_ = STATE_DISCONNECTED;
      this->active_group_idx_ = -1;
      this->waiting_response_ = false;
      this->waiting_for_get_list_ = false;
      this->rx_reassembly_buf_.clear();
      this->publish_status_("Disconnected");
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      static const uint16_t NOTIFY_HANDLES[5] = {
          HANDLE_SVC3_CHAL, HANDLE_SVC3_ACK, HANDLE_DIRECT_RX, HANDLE_SVC2_CH1,
          HANDLE_SVC2_CH2};
      for (uint16_t h : NOTIFY_HANDLES) {
        esp_ble_gattc_register_for_notify(
            gattc_if, this->parent()->get_remote_bda(), h);
      }
      this->node_state = esphome::esp32_ble_tracker::ClientState::ESTABLISHED;
      this->last_action_ms_ = millis();
      this->publish_status_("GATT ready (waiting for encryption)");
      break;
    }

    case ESP_GATTC_NOTIFY_EVT:
      this->handle_notify_(param->notify.handle, param->notify.value,
                           param->notify.value_len);
      break;

    default:
      break;
  }
}

void BdrBleComponent::gap_event_handler(esp_gap_ble_cb_event_t event,
                                        esp_ble_gap_cb_param_t *param) {
  if (event == ESP_GAP_BLE_PASSKEY_REQ_EVT) {
    if (this->has_pin_ && this->parent() != nullptr &&
        std::memcmp(param->ble_security.ble_req.bd_addr,
                    this->parent()->get_remote_bda(), 6) == 0) {
      ESP_LOGI(TAG, "Replying to BLE passkey request with configured PIN");
      this->publish_status_("Passkey requested (replying)");
      esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true,
                            this->pin_);
    }
    return;
  }
  if (event == ESP_GAP_BLE_AUTH_CMPL_EVT) {
    if (param->ble_security.auth_cmpl.success) {
      ESP_LOGI(TAG, "BLE link encrypted; subscribing to GATT notifications");
      if (this->state_ == STATE_CONNECTED_WAIT_SEC) {
        this->state_ = STATE_SUBSCRIBING;
        this->cccd_step_ = 0;
        this->last_action_ms_ = millis();
      }
    } else {
      ESP_LOGW(TAG, "BLE pairing/encryption failed: reason=0x%02X",
               param->ble_security.auth_cmpl.fail_reason);
      this->publish_status_("BLE Pairing Failed");
    }
  }
}

void BdrBleComponent::loop() {
  if (this->state_ == STATE_DISCONNECTED || this->parent() == nullptr) {
    return;
  }
  const uint32_t now = millis();

  if (this->state_ == STATE_CONNECTED_WAIT_SEC) {
    if (now - this->last_action_ms_ > 10000) {
      this->state_ = STATE_SUBSCRIBING;
      this->cccd_step_ = 0;
      this->last_action_ms_ = now;
    }
    return;
  }

  if (this->state_ == STATE_SUBSCRIBING) {
    if (now - this->last_action_ms_ < 250) {
      return;
    }
    this->last_action_ms_ = now;

    static const uint16_t CCCD_HANDLES[5] = {
        HANDLE_SVC3_CHAL_CCCD, HANDLE_SVC3_ACK_CCCD, HANDLE_DIRECT_RX_CCCD,
        HANDLE_SVC2_CH1_CCCD, HANDLE_SVC2_CH2_CCCD};
    if (this->cccd_step_ < 5) {
      this->write_cccd_(CCCD_HANDLES[this->cccd_step_]);
      this->cccd_step_++;
    } else {
      this->publish_status_("Unlocking BLE (Service 3)");
      this->send_zero_length_write_(HANDLE_SVC3_TRIG);
      this->state_ = STATE_SVC3_WAIT_CHALLENGE;
      this->retry_count_ = 0;
    }
    return;
  }

  if (this->state_ == STATE_SVC3_WAIT_CHALLENGE) {
    if (now - this->last_action_ms_ > 3000) {
      this->last_action_ms_ = now;
      if (this->retry_count_ < 3) {
        this->retry_count_++;
        this->send_zero_length_write_(HANDLE_SVC3_TRIG);
      } else {
        this->publish_status_("Unlocking CAN-BUS (RUB v2.01)");
        const uint8_t rub_chal_req[6] = {
            0x02, 0x01, 0x00, MSG_TYPE_RUB_AUTH, RUB_FUNC_CHALLENGE_REQ, 0x03};
        this->send_direct_channel_(rub_chal_req, sizeof(rub_chal_req));
        this->state_ = STATE_RUB_WAIT_CHALLENGE;
        this->retry_count_ = 0;
      }
    }
    return;
  }

  if (this->state_ == STATE_RUB_WAIT_CHALLENGE) {
    if (now - this->last_action_ms_ > 3000 && this->retry_count_ < 3) {
      this->last_action_ms_ = now;
      this->retry_count_++;
      const uint8_t rub_chal_req[6] = {
          0x02, 0x01, 0x00, MSG_TYPE_RUB_AUTH, RUB_FUNC_CHALLENGE_REQ, 0x03};
      this->send_direct_channel_(rub_chal_req, sizeof(rub_chal_req));
    }
    return;
  }

  if (this->state_ == STATE_READING_SNAPSHOT) {
    if (this->waiting_response_ && (now - this->last_req_ms_ >= 1500)) {
      ESP_LOGD(TAG, "Snapshot 0x502B:%02X timed out; advancing",
               this->snapshot_sub_idx_);
      this->waiting_response_ = false;
      this->snapshot_sub_idx_++;
    }
    if (!this->waiting_response_ && (now - this->last_action_ms_ >= 60)) {
      if (this->snapshot_sub_idx_ <= SNAPSHOT_MAX_SUB_INDEX) {
        this->send_read_param_(this->default_node_id_, INDEX_AP_MENU_DESCRIPTOR,
                               this->snapshot_sub_idx_);
        this->waiting_response_ = true;
        this->last_req_ms_ = now;
        this->last_action_ms_ = now;
      } else {
        this->log_appliance_snapshot_();
        this->publish_status_("Ready");
        this->state_ = STATE_READY;
        this->active_group_idx_ = -1;
        this->waiting_response_ = false;
        for (auto &grp : this->poll_groups_) {
          grp.last_cycle_start_ms = 0;
        }
        this->last_action_ms_ = now;
      }
    }
    return;
  }

  if (this->state_ == STATE_READY) {
    // Check if an active request timed out
    if (this->waiting_response_ && (now - this->last_req_ms_ >= 800)) {
      this->waiting_response_ = false;
      if (this->waiting_for_get_list_ && this->active_group_idx_ >= 0 &&
          this->get_list_stage_ < 2) {
        this->waiting_for_get_list_ = false;
        this->get_list_stage_++;
        if (this->get_list_stage_ == 1) {
          ESP_LOGI(TAG, "GetListRequest v2.01 timed out; trying v2.00 header");
          this->active_item_idx_ = 0;
        } else {
          ESP_LOGI(TAG,
                   "GetListRequest not supported over DirectChannel; using fast pipelined burst polling");
          this->get_list_stage_ = 3;
          this->active_item_idx_ = 0;
        }
      }
      this->waiting_for_get_list_ = false;
    }

    // Priority 1: Execute any queued parameter writes immediately
    if (!this->write_queue_.empty() && !this->waiting_response_ &&
        (now - this->last_action_ms_ >= 60)) {
      PendingWrite pw = this->write_queue_.front();
      this->write_queue_.pop_front();
      this->send_write_param_(pw.node_id, pw.index, pw.sub_index,
                              pw.data.data(), pw.data.size());
      this->waiting_response_ = true;
      this->waiting_for_get_list_ = false;
      this->last_req_ms_ = now;
      this->last_action_ms_ = now;
      return;
    }

    if (this->poll_groups_.empty()) {
      return;
    }

    // If no group is currently bursting, check if any group's interval is due
    if (this->active_group_idx_ < 0) {
      for (size_t g = 0; g < this->poll_groups_.size(); g++) {
        auto &grp = this->poll_groups_[g];
        bool has_supported = false;
        for (const auto &dp : grp.items) {
          if (!dp.unsupported) {
            has_supported = true;
            break;
          }
        }
        if (has_supported &&
            (grp.last_cycle_start_ms == 0 ||
             (now - grp.last_cycle_start_ms >= grp.interval_ms))) {
          this->active_group_idx_ = static_cast<int>(g);
          this->active_item_idx_ = 0;
          grp.last_cycle_start_ms = now;
          break;
        }
      }
    }

    // Execute batch GetListRequest or fast pipelined burst for the active group
    if (this->active_group_idx_ >= 0 && !this->waiting_response_ &&
        (now - this->last_action_ms_ >= 60)) {
      auto &grp = this->poll_groups_[this->active_group_idx_];
      while (this->active_item_idx_ < grp.items.size() &&
             grp.items[this->active_item_idx_].unsupported) {
        this->active_item_idx_++;
      }
      if (this->active_item_idx_ < grp.items.size()) {
        std::vector<size_t> batch_indices;
        size_t scan_idx = this->active_item_idx_;
        size_t max_batch = (this->get_list_stage_ <= 2) ? 16 : 1;
        while (scan_idx < grp.items.size() &&
               batch_indices.size() < max_batch) {
          if (!grp.items[scan_idx].unsupported) {
            batch_indices.push_back(scan_idx);
          }
          scan_idx++;
        }
        this->active_item_idx_ = scan_idx;

        if (this->get_list_stage_ <= 2 && batch_indices.size() > 1) {
          bool use_v200 = (this->get_list_stage_ == 1);
          this->send_get_list_batch_(grp, batch_indices, use_v200);
          this->waiting_response_ = true;
          this->waiting_for_get_list_ = true;
          this->last_req_ms_ = now;
          this->last_action_ms_ = now;
        } else if (!batch_indices.empty()) {
          const auto &dp = grp.items[batch_indices[0]];
          this->send_read_param_(dp.node_id, dp.index, dp.sub_index);
          this->waiting_response_ = true;
          this->waiting_for_get_list_ = false;
          this->last_req_ms_ = now;
          this->last_action_ms_ = now;
        }
      } else {
        this->active_group_idx_ = -1;
      }
    }
  }
}

}  // namespace esphome::bdr_ble
