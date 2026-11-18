#include "bdr_ble.h"

#include <cstdio>
#include <cstring>

namespace esphome::bdr_ble {

void BdrBleComponent::handle_notify_(uint16_t handle, const uint8_t *data,
                                     uint16_t len) {
  if (handle == HANDLE_SVC3_CHAL && len >= 8) {
    if (this->state_ != STATE_SVC3_WAIT_CHALLENGE &&
        this->state_ != STATE_SVC3_WAIT_RESULT) {
      return;
    }
    this->log_rx_hex_("SVC3[33]", data, len, true);
    uint32_t id1 = static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8) |
                   (static_cast<uint32_t>(data[2]) << 16) |
                   (static_cast<uint32_t>(data[3]) << 24);
    uint32_t id2 = static_cast<uint32_t>(data[4]) |
                   (static_cast<uint32_t>(data[5]) << 8) |
                   (static_cast<uint32_t>(data[6]) << 16) |
                   (static_cast<uint32_t>(data[7]) << 24);
    uint32_t v0 = 0, v1 = 0;
    tea_encrypt(this->auth_level_, id1, id2, v0, v1);

    uint8_t resp[9] = {
        this->auth_level_,
        static_cast<uint8_t>(v0 & 0xFF),
        static_cast<uint8_t>((v0 >> 8) & 0xFF),
        static_cast<uint8_t>((v0 >> 16) & 0xFF),
        static_cast<uint8_t>((v0 >> 24) & 0xFF),
        static_cast<uint8_t>(v1 & 0xFF),
        static_cast<uint8_t>((v1 >> 8) & 0xFF),
        static_cast<uint8_t>((v1 >> 16) & 0xFF),
        static_cast<uint8_t>((v1 >> 24) & 0xFF),
    };
    esp_ble_gattc_write_char(this->parent()->get_gattc_if(),
                             this->parent()->get_conn_id(), HANDLE_SVC3_RESP,
                             sizeof(resp), resp, ESP_GATT_WRITE_TYPE_RSP,
                             ESP_GATT_AUTH_REQ_NONE);
    this->state_ = STATE_SVC3_WAIT_RESULT;
    this->last_action_ms_ = millis();
    return;
  }

  if (handle == HANDLE_SVC3_ACK && len >= 1) {
    if (this->state_ != STATE_SVC3_WAIT_RESULT &&
        this->state_ != STATE_SVC3_WAIT_CHALLENGE) {
      return;
    }
    this->log_rx_hex_("SVC3[38]", data, len, true);
    if (data[0] == 0x01) {
      this->publish_status_("BLE Unlocked -> RUB Auth");
      const uint8_t rub_chal_req[6] = {
          0x02, 0x01, 0x00, MSG_TYPE_RUB_AUTH, RUB_FUNC_CHALLENGE_REQ, 0x03};
      this->send_direct_channel_(rub_chal_req, sizeof(rub_chal_req));
      this->state_ = STATE_RUB_WAIT_CHALLENGE;
      this->retry_count_ = 0;
      this->last_action_ms_ = millis();
    } else {
      this->publish_status_("Service 3 Auth Rejected");
      this->state_ = STATE_AUTH_FAILED;
      this->last_action_ms_ = millis();
    }
    return;
  }

  // Handle 27: BDR Error Management characteristic (de00707b)
  if (handle == HANDLE_SVC2_CH2 && len >= 4) {
    ESP_LOGW(TAG, "BLE Error Management notify: [%u, %u, %u, %u]", data[0],
             data[1], data[2], data[3]);
    // [3, 0, 0, 4] = "BLE services locked" -> re-trigger Service 3 unlock
    if (data[0] == 3 && data[1] == 0 && data[2] == 0 && data[3] == 4 &&
        this->state_ == STATE_READY) {
      this->publish_status_("BLE Services Locked -> Re-unlocking");
      this->in_flight_type_ = IN_FLIGHT_NONE;
      this->send_zero_length_write_(HANDLE_SVC3_TRIG);
      this->state_ = STATE_SVC3_WAIT_CHALLENGE;
      this->retry_count_ = 0;
      this->last_action_ms_ = millis();
    }
    return;
  }

  if (handle == HANDLE_DIRECT_RX && len >= 2) {
    this->handle_direct_segment_(data, len);
  }
}

void BdrBleComponent::handle_direct_segment_(const uint8_t *data,
                                             uint16_t len) {
  const uint8_t seg = data[0];
  const uint32_t now = millis();

  // Discard stale partial reassembly buffer if more than 1000 ms old
  if (!this->rx_reassembly_buf_.empty() &&
      (now - this->last_rx_seg_ms_ > 1000)) {
    ESP_LOGW(TAG, "Discarding stale partial RX buffer (%zu bytes)",
             this->rx_reassembly_buf_.size());
    this->rx_reassembly_buf_.clear();
    this->expected_rx_seg_ = 0;
  }

  if (seg == 0x00) {
    this->rx_reassembly_buf_.assign(data + 1, data + len);
    this->expected_rx_seg_ = 1;
    this->last_rx_seg_ms_ = now;
    return;
  }
  if (seg < SEGMENT_FINAL) {
    if (seg == this->expected_rx_seg_ && !this->rx_reassembly_buf_.empty() &&
        this->rx_reassembly_buf_.size() + (len - 1) <= 2048) {
      this->rx_reassembly_buf_.insert(this->rx_reassembly_buf_.end(), data + 1,
                                      data + len);
      this->expected_rx_seg_++;
      this->last_rx_seg_ms_ = now;
    } else {
      ESP_LOGW(TAG, "Out-of-order RX segment 0x%02X (expected 0x%02X); resetting",
               seg, this->expected_rx_seg_);
      this->rx_reassembly_buf_.clear();
      this->expected_rx_seg_ = 0;
    }
    return;
  }

  // seg == 0xFF: final segment (or single-segment frame)
  const uint8_t *frame_ptr = nullptr;
  size_t frame_len = 0;
  if (!this->rx_reassembly_buf_.empty() && this->expected_rx_seg_ > 0) {
    this->rx_reassembly_buf_.insert(this->rx_reassembly_buf_.end(), data + 1,
                                    data + len);
    frame_ptr = this->rx_reassembly_buf_.data();
    frame_len = this->rx_reassembly_buf_.size();
  } else {
    frame_ptr = data + 1;
    frame_len = len - 1;
  }

  if (frame_len >= 4) {
    uint16_t rx_crc =
        (static_cast<uint16_t>(frame_ptr[frame_len - 2]) << 8) |
        frame_ptr[frame_len - 1];
    uint16_t calc_crc = crc16_ccitt(frame_ptr, frame_len - 2);
    if (rx_crc == calc_crc && frame_ptr[0] == DIRECT_CHANNEL_CAN) {
      this->handle_direct_payload_(frame_ptr + 1, frame_len - 3);
    } else {
      ESP_LOGW(TAG, "DirectChannel CRC/channel mismatch (rx=0x%04X calc=0x%04X)",
               rx_crc, calc_crc);
    }
  }
  this->rx_reassembly_buf_.clear();
  this->expected_rx_seg_ = 0;
}

void BdrBleComponent::handle_direct_payload_(const uint8_t *payload,
                                             size_t len) {
  if (len < 6) {
    return;
  }
  const uint8_t msg_type = payload[3];
  const uint8_t func = payload[4];

  // RUB v2.01 Challenge Response: 02 01 00 02 01 03 <serial_BE(4)> <chal_BE(4)>
  if (msg_type == MSG_TYPE_RUB_AUTH && func == RUB_FUNC_CHALLENGE_RESP &&
      len >= 14) {
    if (this->state_ != STATE_RUB_WAIT_CHALLENGE &&
        this->state_ != STATE_RUB_WAIT_AUTH_ACK) {
      return;
    }
    this->log_rx_hex_("RUB_CHAL", payload, len, true);
    uint32_t srv_serial = (static_cast<uint32_t>(payload[6]) << 24) |
                          (static_cast<uint32_t>(payload[7]) << 16) |
                          (static_cast<uint32_t>(payload[8]) << 8) | payload[9];
    uint32_t time_chal = (static_cast<uint32_t>(payload[10]) << 24) |
                         (static_cast<uint32_t>(payload[11]) << 16) |
                         (static_cast<uint32_t>(payload[12]) << 8) |
                         payload[13];
    uint32_t v0 = 0, v1 = 0;
    tea_encrypt(this->auth_level_, srv_serial, time_chal, v0, v1);

    uint8_t auth_req[16] = {
        0x02,
        0x01,
        0x00,
        MSG_TYPE_RUB_AUTH,
        RUB_FUNC_AUTH_REQ,
        0x03,
        0xFF,
        this->auth_level_,
        static_cast<uint8_t>((v0 >> 24) & 0xFF),
        static_cast<uint8_t>((v0 >> 16) & 0xFF),
        static_cast<uint8_t>((v0 >> 8) & 0xFF),
        static_cast<uint8_t>(v0 & 0xFF),
        static_cast<uint8_t>((v1 >> 24) & 0xFF),
        static_cast<uint8_t>((v1 >> 16) & 0xFF),
        static_cast<uint8_t>((v1 >> 8) & 0xFF),
        static_cast<uint8_t>(v1 & 0xFF),
    };
    this->send_direct_channel_(auth_req, sizeof(auth_req));
    this->state_ = STATE_RUB_WAIT_AUTH_ACK;
    this->last_action_ms_ = millis();
    return;
  }

  // RUB v2.01 Auth Ack: 02 01 00 02 03 03 FF <level> ...
  if (msg_type == MSG_TYPE_RUB_AUTH && func == RUB_FUNC_AUTH_ACK && len >= 8) {
    if (this->state_ != STATE_RUB_WAIT_AUTH_ACK &&
        this->state_ != STATE_RUB_WAIT_CHALLENGE) {
      return;
    }
    this->log_rx_hex_("RUB_ACK", payload, len, true);
    uint8_t granted = payload[7];
    if (granted > 0) {
      char msg[64];
      snprintf(msg, sizeof(msg), "Connected (auth level %u, reading snapshot)",
               granted);
      this->publish_status_(msg);
      this->state_ = STATE_READING_SNAPSHOT;
      this->snapshot_sub_idx_ = 1;
      this->discovered_datapoints_.clear();
      this->in_flight_type_ = IN_FLIGHT_NONE;
      this->consecutive_timeouts_ = 0;
      this->last_action_ms_ = millis();
    } else {
      this->publish_status_("CAN-BUS Auth Rejected");
      this->state_ = STATE_AUTH_FAILED;
      this->last_action_ms_ = millis();
    }
    return;
  }

  // Single Read Positive Ack: 02 01 00 00 01 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx> <Data...>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_READ_POS_ACK && len >= 10) {
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    if ((this->in_flight_type_ == IN_FLIGHT_READ ||
         this->in_flight_type_ == IN_FLIGHT_SNAPSHOT) &&
        this->in_flight_node_ == node_id &&
        this->in_flight_index_ == index &&
        this->in_flight_sub_ == sub_index) {
      this->in_flight_type_ = IN_FLIGHT_NONE;
      this->consecutive_timeouts_ = 0;
    }
    this->handle_single_read_ack_(node_id, index, sub_index, payload + 10,
                                  len - 10);
    return;
  }

  // Single Read Negative Ack (SDO Abort): 02 01 00 00 02 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx> <AbortCode(4)>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_READ_NEG_ACK && len >= 10) {
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    if ((this->in_flight_type_ == IN_FLIGHT_READ ||
         this->in_flight_type_ == IN_FLIGHT_SNAPSHOT) &&
        this->in_flight_node_ == node_id &&
        this->in_flight_index_ == index &&
        this->in_flight_sub_ == sub_index) {
      this->in_flight_type_ = IN_FLIGHT_NONE;
      this->consecutive_timeouts_ = 0;
    }
    uint32_t abort_code = 0;
    if (len >= 14) {
      abort_code = static_cast<uint32_t>(payload[10]) |
                   (static_cast<uint32_t>(payload[11]) << 8) |
                   (static_cast<uint32_t>(payload[12]) << 16) |
                   (static_cast<uint32_t>(payload[13]) << 24);
    }
    if (index == INDEX_AP_MENU_DESCRIPTOR) {
      ESP_LOGD(TAG, "Snapshot 0x502B:%02X -> empty/NACK", sub_index);
      if (this->state_ == STATE_READING_SNAPSHOT &&
          sub_index == this->snapshot_sub_idx_) {
        this->snapshot_sub_idx_++;
      }
    } else if (abort_code == 0 ||
               abort_code == SDO_ABORT_OBJECT_NOT_EXIST ||
               abort_code == SDO_ABORT_SUBINDEX_NOT_EXIST ||
               abort_code == SDO_ABORT_UNSUPPORTED_ACCESS ||
               abort_code == SDO_ABORT_WRITE_ONLY) {
      this->mark_datapoint_unsupported_(node_id, index, sub_index,
                                        FUNC_READ_NEG_ACK);
    } else {
      ESP_LOGW(TAG, "Transient SDO read abort on 0x%04X:%02X: 0x%08X", index,
               sub_index, static_cast<unsigned>(abort_code));
    }
    return;
  }

  // Write Confirmed Positive Ack: 02 01 00 00 05 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_WRITE_POS_ACK && len >= 10) {
    if (this->in_flight_type_ != IN_FLIGHT_WRITE) {
      return;
    }
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    this->consecutive_timeouts_ = 0;
    ESP_LOGI(TAG, "Write ACK for 0x%04X:%02X; reading back value", index,
             sub_index);
    this->send_read_param_(node_id, index, sub_index);
    this->in_flight_type_ = IN_FLIGHT_READ;
    this->in_flight_node_ = node_id;
    this->in_flight_index_ = index;
    this->in_flight_sub_ = sub_index;
    this->last_req_ms_ = millis();
    this->last_action_ms_ = this->last_req_ms_;
    return;
  }

  // Write Confirmed Negative Ack (SDO Abort): 02 01 00 00 06 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx> <AbortCode(4)>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_WRITE_NEG_ACK && len >= 10) {
    if (this->in_flight_type_ != IN_FLIGHT_WRITE) {
      return;
    }
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    uint32_t abort_code = 0;
    if (len >= 14) {
      abort_code = static_cast<uint32_t>(payload[10]) |
                   (static_cast<uint32_t>(payload[11]) << 8) |
                   (static_cast<uint32_t>(payload[12]) << 16) |
                   (static_cast<uint32_t>(payload[13]) << 24);
    }
    this->consecutive_timeouts_ = 0;
    ESP_LOGW(TAG,
             "Write NACK for 0x%04X:%02X (SDO abort=0x%08X); reverting state",
             index, sub_index, static_cast<unsigned>(abort_code));
    this->send_read_param_(node_id, index, sub_index);
    this->in_flight_type_ = IN_FLIGHT_READ;
    this->in_flight_node_ = node_id;
    this->in_flight_index_ = index;
    this->in_flight_sub_ = sub_index;
    this->last_req_ms_ = millis();
    this->last_action_ms_ = this->last_req_ms_;
    return;
  }

  // Batch GetListResponse: 02 xx 00 00 09 xx <Count> <Items...>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_GET_LIST_RESP && len >= 7) {
    if (this->in_flight_type_ == IN_FLIGHT_GET_LIST) {
      this->in_flight_type_ = IN_FLIGHT_NONE;
      this->consecutive_timeouts_ = 0;
      this->get_list_stage_ = 2;  // Confirmed working!
    }
    this->handle_get_list_response_(payload, len);
  }
}

void BdrBleComponent::handle_single_read_ack_(uint8_t node_id, uint16_t index,
                                              uint8_t sub_index,
                                              const uint8_t *data,
                                              size_t data_len) {
  if (index == INDEX_AP_MENU_DESCRIPTOR) {
    this->handle_snapshot_descriptor_(sub_index, data, data_len);
    if (this->state_ == STATE_READING_SNAPSHOT &&
        sub_index == this->snapshot_sub_idx_) {
      this->snapshot_sub_idx_++;
    }
    return;
  }
  char tag[24];
  snprintf(tag, sizeof(tag), "0x%04X:%02X", index, sub_index);
  this->log_rx_hex_(tag, data, data_len, false);
  this->dispatch_datapoint_value_(node_id, index, sub_index, data, data_len);
}

void BdrBleComponent::handle_get_list_response_(const uint8_t *payload,
                                                size_t len) {
  uint8_t count = payload[6];
  char tag[24];
  snprintf(tag, sizeof(tag), "LIST[%u]", count);
  this->log_rx_hex_(tag, payload, (len > 32) ? 32 : len, false);
  size_t offset = 7;
  for (uint8_t i = 0; i < count && offset + 8 <= len; i++) {
    uint8_t status = payload[offset + 0];
    uint8_t node_id = payload[offset + 1];
    uint16_t index = (static_cast<uint16_t>(payload[offset + 2]) << 8) |
                     payload[offset + 3];
    uint8_t sub_index = payload[offset + 4];
    uint16_t data_len = (static_cast<uint16_t>(payload[offset + 6]) << 8) |
                        payload[offset + 7];
    offset += 8;
    if (offset + data_len > len) {
      break;
    }
    if (status == 0x01) {
      this->dispatch_datapoint_value_(node_id, index, sub_index,
                                      payload + offset, data_len);
    } else {
      this->mark_datapoint_unsupported_(node_id, index, sub_index, status);
    }
    offset += data_len;
  }
}

void BdrBleComponent::mark_datapoint_unsupported_(uint8_t node_id,
                                                  uint16_t index,
                                                  uint8_t sub_index,
                                                  uint8_t status_code) {
  for (auto &grp : this->poll_groups_) {
    for (auto &dp : grp.items) {
      if (dp.node_id == node_id && dp.index == index &&
          dp.sub_index == sub_index && !dp.unsupported) {
        dp.unsupported = true;
        const char *entity_name = "unregistered";
#ifdef USE_SENSOR
        if (!dp.sensors.empty() && dp.sensors[0] != nullptr) {
          entity_name = dp.sensors[0]->get_name().c_str();
        }
#endif
#ifdef USE_TEXT_SENSOR
        if (!dp.text_sensors.empty() && dp.text_sensors[0] != nullptr) {
          entity_name = dp.text_sensors[0]->get_name().c_str();
        }
#endif
#ifdef USE_NUMBER
        if (!dp.numbers.empty() && dp.numbers[0] != nullptr) {
          entity_name = dp.numbers[0]->get_name().c_str();
        }
#endif
#ifdef USE_SELECT
        if (!dp.selects.empty() && dp.selects[0] != nullptr) {
          entity_name = dp.selects[0]->get_name().c_str();
        }
#endif
        ESP_LOGW(TAG,
                 "Datapoint 0x%04X:%02X (%s) reported unsupported by boiler "
                 "(status=0x%02X); disabling polling",
                 index, sub_index, entity_name, status_code);
      }
    }
  }
}

void BdrBleComponent::handle_snapshot_descriptor_(uint8_t sub_index,
                                                  const uint8_t *data,
                                                  size_t data_len) {
  if (data_len < 4) {
    ESP_LOGD(TAG, "Snapshot 0x502B:%02X -> %zu bytes (empty)", sub_index,
             data_len);
    return;
  }
  uint16_t count =
      static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  uint16_t parsed = 0;
  for (uint16_t k = 0; k < count; k++) {
    size_t off = 4 + static_cast<size_t>(k) * 15;
    if (off + 15 > data_len) {
      break;
    }
    uint16_t dp_idx = static_cast<uint16_t>(data[off + 2]) |
                      (static_cast<uint16_t>(data[off + 3]) << 8);
    uint8_t dp_sub = data[off + 12];
#ifdef USE_NUMBER
    uint32_t raw_min_u = static_cast<uint32_t>(data[off + 4]) |
                         (static_cast<uint32_t>(data[off + 5]) << 8) |
                         (static_cast<uint32_t>(data[off + 6]) << 16) |
                         (static_cast<uint32_t>(data[off + 7]) << 24);
    uint32_t raw_max_u = static_cast<uint32_t>(data[off + 8]) |
                         (static_cast<uint32_t>(data[off + 9]) << 8) |
                         (static_cast<uint32_t>(data[off + 10]) << 16) |
                         (static_cast<uint32_t>(data[off + 11]) << 24);
    uint8_t raw_inc = data[off + 13];
    if (raw_min_u != 0xFFFFFFFFUL && raw_max_u != 0xFFFFFFFFUL) {
      for (auto &grp : this->poll_groups_) {
        for (auto &dp : grp.items) {
          if (dp.node_id == this->default_node_id_ && dp.index == dp_idx &&
              dp.sub_index == dp_sub && !dp.numbers.empty()) {
            bool is_signed = (dp.data_type == BDR_TYPE_I8 ||
                              dp.data_type == BDR_TYPE_I16 ||
                              dp.data_type == BDR_TYPE_I32);
            float min_f = is_signed
                              ? static_cast<float>(static_cast<int32_t>(raw_min_u)) * dp.scale
                              : static_cast<float>(raw_min_u) * dp.scale;
            float max_f = is_signed
                              ? static_cast<float>(static_cast<int32_t>(raw_max_u)) * dp.scale
                              : static_cast<float>(raw_max_u) * dp.scale;
            if (min_f < max_f) {
              for (auto *num : dp.numbers) {
                num->traits.set_min_value(min_f);
                num->traits.set_max_value(max_f);
                if (raw_inc > 0 && raw_inc != 0xFF) {
                  num->traits.set_step(static_cast<float>(raw_inc) * dp.scale);
                }
              }
              ESP_LOGI(TAG,
                       "Updated limits for 0x%04X:%02X from 0x502B: min=%.2f max=%.2f",
                       dp_idx, dp_sub, min_f, max_f);
            }
          }
        }
      }
    }
#endif
    uint8_t dp_lvl = ((sub_index - 1) % 5) + 1;
    uint32_t packed_key = (static_cast<uint32_t>(dp_idx) << 8) | dp_sub;
    bool exists = false;
    for (uint32_t existing : this->discovered_datapoints_) {
      if ((existing >> 8) == packed_key) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      this->discovered_datapoints_.push_back((packed_key << 8) | dp_lvl);
    }
    parsed++;
  }
  ESP_LOGI(TAG,
           "Snapshot 0x502B:%02X -> %u descriptors (%zu bytes, total unique=%zu)",
           sub_index, parsed, data_len, this->discovered_datapoints_.size());
}

void BdrBleComponent::log_appliance_snapshot_() {
  ESP_LOGI(TAG,
           "=== Appliance Snapshot (%zu supported datapoints from 0x502B:01..15) ===",
           this->discovered_datapoints_.size());
  if (this->discovered_datapoints_.empty()) {
    ESP_LOGI(TAG, "=== No 0x502B descriptors returned by appliance ===");
    return;
  }
  char line[200];
  int pos = snprintf(line, sizeof(line), "BDR_SNAPSHOT:");
  for (size_t i = 0; i < this->discovered_datapoints_.size(); i++) {
    uint16_t idx =
        static_cast<uint16_t>((this->discovered_datapoints_[i] >> 16) & 0xFFFF);
    uint8_t sub =
        static_cast<uint8_t>((this->discovered_datapoints_[i] >> 8) & 0xFF);
    uint8_t lvl = static_cast<uint8_t>(this->discovered_datapoints_[i] & 0xFF);
    if (pos > 170) {
      ESP_LOGI(TAG, "%s", line);
      pos = snprintf(line, sizeof(line), "BDR_SNAPSHOT:");
    }
    pos += snprintf(line + pos, sizeof(line) - pos, "%s%04X:%02X@%u",
                    (pos > 13) ? "," : "", idx, sub, lvl);
  }
  if (pos > 13) {
    ESP_LOGI(TAG, "%s", line);
  }
  ESP_LOGI(TAG,
           "=== End of Appliance Snapshot (paste BDR_SNAPSHOT lines into configurator.html) ===");
}

void BdrBleComponent::dispatch_datapoint_value_(uint8_t node_id, uint16_t index,
                                                uint8_t sub_index,
                                                const uint8_t *data,
                                                size_t data_len) {
  if (data_len == 0) {
    return;
  }

  for (auto &grp : this->poll_groups_) {
    for (auto &dp : grp.items) {
      if (dp.node_id != node_id || dp.index != index ||
          dp.sub_index != sub_index) {
        continue;
      }

      float numeric_val = 0.0f;
      int32_t raw_enum_val = 0;
      bool valid_numeric = decode_numeric_datapoint(
          dp.data_type, dp.scale, data, data_len, numeric_val, raw_enum_val);

#ifdef USE_SENSOR
      if (valid_numeric) {
        for (auto *sens : dp.sensors) {
          sens->publish_state(numeric_val);
        }
      }
#endif

#ifdef USE_NUMBER
      if (valid_numeric) {
        for (auto *num : dp.numbers) {
          num->publish_state(numeric_val);
        }
      }
#endif

#if defined(USE_TEXT_SENSOR) || defined(USE_SELECT)
      const std::string *enum_label = nullptr;
      if (valid_numeric && dp.data_type == BDR_TYPE_ENUM) {
        uint64_t key =
            (static_cast<uint64_t>(index) << 24) |
            (static_cast<uint64_t>(sub_index) << 16) |
            (static_cast<uint64_t>(static_cast<uint16_t>(raw_enum_val)));
        auto it = this->enum_translations_.find(key);
        if (it != this->enum_translations_.end()) {
          enum_label = &it->second;
        }
      }
#endif

#ifdef USE_SELECT
      if (enum_label != nullptr) {
        for (auto *sel : dp.selects) {
          sel->publish_state(*enum_label);
        }
      }
#endif

#ifdef USE_TEXT_SENSOR
      if (!dp.text_sensors.empty()) {
        std::string text_val;
        if (dp.data_type == BDR_TYPE_ENUM) {
          if (enum_label != nullptr) {
            text_val = *enum_label;
          } else {
            char fbuf[24];
            snprintf(fbuf, sizeof(fbuf), "%d", static_cast<int>(raw_enum_val));
            text_val = fbuf;
          }
        } else if (dp.data_type == BDR_TYPE_VISIBLE_STRING) {
          size_t slen = 0;
          while (slen < data_len && data[slen] != 0) {
            slen++;
          }
          text_val.assign(reinterpret_cast<const char *>(data), slen);
        } else if (dp.data_type == BDR_TYPE_OCTET_STRING) {
          char hbuf[96];
          int hpos = 0;
          for (size_t b = 0;
               b < data_len && hpos < static_cast<int>(sizeof(hbuf)) - 4; b++) {
            hpos += snprintf(hbuf + hpos, sizeof(hbuf) - hpos, "%s%02X",
                             (b > 0) ? " " : "", data[b]);
          }
          text_val = hbuf;
        } else if (dp.data_type == BDR_TYPE_TIME_OF_DAY && data_len >= 2) {
          char tbuf[16];
          snprintf(tbuf, sizeof(tbuf), "%02u:%02u", data[0], data[1]);
          text_val = tbuf;
        }
        for (auto *ts : dp.text_sensors) {
          ts->publish_state(text_val);
        }
      }
#endif
    }
  }
}

}  // namespace esphome::bdr_ble
