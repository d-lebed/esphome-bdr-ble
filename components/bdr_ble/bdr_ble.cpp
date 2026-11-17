#include "bdr_ble.h"
#include <cstdio>
#include <cstring>

extern "C" void BTA_GATTC_WriteCharValue(uint16_t conn_id, uint16_t handle,
                                         uint8_t write_type, uint16_t len,
                                         uint8_t *p_value, uint8_t auth_req);

namespace esphome {
namespace bdr_ble {

static const char *const TAG = "bdr_ble";

// Fixed GATT Attribute Handles on CU-GH08 / EHC-08 / MK3 BLE Module
static constexpr uint16_t HANDLE_DIRECT_TX = 18;      // 496b1b03 (Write)
static constexpr uint16_t HANDLE_DIRECT_RX = 20;      // ab9af948 (Notify)
static constexpr uint16_t HANDLE_DIRECT_RX_CCCD = 21;
static constexpr uint16_t HANDLE_SVC2_CH1 = 23;       // 0c66c55b (Notify)
static constexpr uint16_t HANDLE_SVC2_CH1_CCCD = 24;
static constexpr uint16_t HANDLE_SVC2_CH2 = 27;       // de00707b (Notify)
static constexpr uint16_t HANDLE_SVC2_CH2_CCCD = 28;
static constexpr uint16_t HANDLE_SVC3_TRIG = 31;      // 94e54dc4 (Write 0 bytes)
static constexpr uint16_t HANDLE_SVC3_CHAL = 33;      // 69092300 (Notify 8 bytes)
static constexpr uint16_t HANDLE_SVC3_CHAL_CCCD = 34;
static constexpr uint16_t HANDLE_SVC3_RESP = 36;      // f1ca5aeb (Write 9 bytes)
static constexpr uint16_t HANDLE_SVC3_ACK = 38;       // e2c47825 (Notify 1 byte)
static constexpr uint16_t HANDLE_SVC3_ACK_CCCD = 39;

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

void BdrBleComponent::register_enum_translation(uint16_t index,
                                                uint8_t sub_index, int value,
                                                const char *description) {
  uint64_t key = (static_cast<uint64_t>(index) << 24) |
                 (static_cast<uint64_t>(sub_index) << 16) |
                 (static_cast<uint64_t>(static_cast<uint16_t>(value)));
  this->enum_translations_[key] = description;
}
#endif

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

uint16_t BdrBleComponent::crc16_ccitt_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if (crc & 1) {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
      } else {
        crc = static_cast<uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

void BdrBleComponent::tea_encrypt_(uint8_t auth_level, uint32_t id1,
                                   uint32_t id2, uint32_t &out_v0,
                                   uint32_t &out_v1) {
  uint32_t v0 = auth_level;
  uint32_t v1 = auth_level;
  const uint32_t k0 = 0x15EFA43Fu;
  const uint32_t k1 = id1;
  const uint32_t k2 = id2;
  const uint32_t k3 = auth_level;
  const uint32_t delta = 0x9E3779B9u;
  uint32_t sum = 0;

  for (int i = 0; i < 32; i++) {
    sum += delta;
    v0 += (((v1 << 4) + k0) ^ (v1 + sum)) ^ ((v1 >> 5) + k1);
    v1 += (((v0 << 4) + k2) ^ (v0 + sum)) ^ ((v0 >> 5) + k3);
  }
  out_v0 = v0;
  out_v1 = v1;
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
  std::vector<uint8_t> frame(len + 3);
  frame[0] = 0x01;  // Channel 1 (CAN / RUB)
  std::memcpy(&frame[1], payload, len);
  uint16_t crc = crc16_ccitt_(frame.data(), len + 1);
  frame[1 + len] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  frame[2 + len] = static_cast<uint8_t>(crc & 0xFF);

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
    pkt[0] = is_last ? 0xFF : seg_idx++;
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
      0x00,
      0x00,  // Function 0x00 = Read Request
      0x03,
      node_id,
      static_cast<uint8_t>((index >> 8) & 0xFF),
      static_cast<uint8_t>(index & 0xFF),
      sub_index,
  };
  this->send_direct_channel_(req, sizeof(req));
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
  req[3] = 0x00;
  req[4] = 0x08;  // Function 0x08 = GetListRequest
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
      this->rx_reassembly_buf_.clear();
      this->publish_status_("Disconnected");
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      // Register notifications BEFORE setting node_state = ESTABLISHED
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
    // Fallback if link was already encrypted without firing AUTH_CMPL
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
        const uint8_t rub_chal_req[6] = {0x02, 0x01, 0x00, 0x02, 0x00, 0x03};
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
      const uint8_t rub_chal_req[6] = {0x02, 0x01, 0x00, 0x02, 0x00, 0x03};
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
      if (this->snapshot_sub_idx_ <= 15) {
        this->send_read_param_(this->default_node_id_, 0x502B,
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
    if (this->poll_groups_.empty()) {
      return;
    }

    // Check if an active request timed out
    if (this->waiting_response_ && (now - this->last_req_ms_ >= 800)) {
      this->waiting_response_ = false;
      if (this->get_list_stage_ < 2) {
        this->get_list_stage_++;
        if (this->get_list_stage_ == 1) {
          ESP_LOGI(TAG, "GetListRequest v2.01 timed out; trying v2.00 header");
          this->active_item_idx_ = 0;
        } else {
          ESP_LOGI(TAG, "GetListRequest not supported over DirectChannel; using fast pipelined burst polling");
          this->get_list_stage_ = 3;
          this->active_item_idx_ = 0;
        }
      }
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
          this->last_req_ms_ = now;
          this->last_action_ms_ = now;
        } else if (!batch_indices.empty()) {
          const auto &dp = grp.items[batch_indices[0]];
          this->send_read_param_(dp.node_id, dp.index, dp.sub_index);
          this->waiting_response_ = true;
          this->last_req_ms_ = now;
          this->last_action_ms_ = now;
        }
      } else {
        this->active_group_idx_ = -1;
      }
    }
  }
}

void BdrBleComponent::handle_notify_(uint16_t handle, const uint8_t *data,
                                     uint16_t len) {
  if (handle == HANDLE_SVC3_CHAL && len >= 8) {
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
    tea_encrypt_(this->auth_level_, id1, id2, v0, v1);

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
    this->log_rx_hex_("SVC3[38]", data, len, true);
    if (data[0] == 0x01) {
      this->publish_status_("BLE Unlocked -> RUB Auth");
      const uint8_t rub_chal_req[6] = {0x02, 0x01, 0x00, 0x02, 0x00, 0x03};
      this->send_direct_channel_(rub_chal_req, sizeof(rub_chal_req));
      this->state_ = STATE_RUB_WAIT_CHALLENGE;
      this->retry_count_ = 0;
      this->last_action_ms_ = millis();
    } else if (this->state_ == STATE_SVC3_WAIT_RESULT) {
      this->publish_status_("Service 3 Auth Rejected");
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
  if (seg == 0x00) {
    this->rx_reassembly_buf_.assign(data + 1, data + len);
    return;
  }
  if (seg < 0xFF) {
    if (!this->rx_reassembly_buf_.empty() &&
        this->rx_reassembly_buf_.size() + (len - 1) <= 2048) {
      this->rx_reassembly_buf_.insert(this->rx_reassembly_buf_.end(), data + 1,
                                      data + len);
    }
    return;
  }

  // seg == 0xFF: final segment (or single-segment frame)
  const uint8_t *frame_ptr = nullptr;
  size_t frame_len = 0;
  if (!this->rx_reassembly_buf_.empty()) {
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
    uint16_t calc_crc = crc16_ccitt_(frame_ptr, frame_len - 2);
    if (rx_crc == calc_crc && frame_ptr[0] == 0x01) {
      this->handle_direct_payload_(frame_ptr + 1, frame_len - 3);
    } else {
      ESP_LOGW(TAG, "DirectChannel CRC/channel mismatch (rx=0x%04X calc=0x%04X)",
               rx_crc, calc_crc);
    }
  }
  this->rx_reassembly_buf_.clear();
}

void BdrBleComponent::handle_direct_payload_(const uint8_t *payload,
                                             size_t len) {
  if (len < 6) {
    return;
  }
  const uint8_t msg_type = payload[3];
  const uint8_t func = payload[4];

  // RUB v2.01 Challenge Response: 02 01 00 02 01 03 <serial_BE(4)> <chal_BE(4)>
  if (msg_type == 0x02 && func == 0x01 && len >= 14) {
    this->log_rx_hex_("RUB_CHAL", payload, len, true);
    uint32_t srv_serial = (static_cast<uint32_t>(payload[6]) << 24) |
                          (static_cast<uint32_t>(payload[7]) << 16) |
                          (static_cast<uint32_t>(payload[8]) << 8) | payload[9];
    uint32_t time_chal = (static_cast<uint32_t>(payload[10]) << 24) |
                         (static_cast<uint32_t>(payload[11]) << 16) |
                         (static_cast<uint32_t>(payload[12]) << 8) |
                         payload[13];
    uint32_t v0 = 0, v1 = 0;
    tea_encrypt_(this->auth_level_, srv_serial, time_chal, v0, v1);

    uint8_t auth_req[16] = {
        0x02,
        0x01,
        0x00,
        0x02,
        0x02,
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
  if (msg_type == 0x02 && func == 0x03 && len >= 8) {
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
      this->waiting_response_ = false;
      this->last_action_ms_ = millis();
    } else {
      this->publish_status_("CAN-BUS Auth Rejected");
    }
    return;
  }

  // Single Read Positive Ack: 02 01 00 00 01 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx> <Data...>
  if (msg_type == 0x00 && func == 0x01 && len >= 10) {
    this->waiting_response_ = false;
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    this->handle_single_read_ack_(node_id, index, sub_index, payload + 10,
                                  len - 10);
    return;
  }

  // Single Read Negative Ack (SDO Abort): 02 01 00 00 02 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx> <AbortCode(4)>
  if (msg_type == 0x00 && func == 0x02 && len >= 10) {
    this->waiting_response_ = false;
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    if (index == 0x502B) {
      ESP_LOGD(TAG, "Snapshot 0x502B:%02X -> empty/NACK", sub_index);
      if (this->state_ == STATE_READING_SNAPSHOT &&
          sub_index == this->snapshot_sub_idx_) {
        this->snapshot_sub_idx_++;
      }
    } else {
      this->mark_datapoint_unsupported_(node_id, index, sub_index, 0x02);
    }
    return;
  }

  // Batch GetListResponse: 02 xx 00 00 09 xx <Count> <Items...>
  if (msg_type == 0x00 && func == 0x09 && len >= 7) {
    this->waiting_response_ = false;
    this->get_list_stage_ = 2;  // Confirmed working!
    this->handle_get_list_response_(payload, len);
  }
}

void BdrBleComponent::handle_single_read_ack_(uint8_t node_id, uint16_t index,
                                              uint8_t sub_index,
                                              const uint8_t *data,
                                              size_t data_len) {
  if (index == 0x502B) {
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
    uint32_t packed = (static_cast<uint32_t>(dp_idx) << 8) | dp_sub;
    bool exists = false;
    for (uint32_t existing : this->discovered_datapoints_) {
      if (existing == packed) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      this->discovered_datapoints_.push_back(packed);
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
        static_cast<uint16_t>((this->discovered_datapoints_[i] >> 8) & 0xFFFF);
    uint8_t sub = static_cast<uint8_t>(this->discovered_datapoints_[i] & 0xFF);
    if (pos > 175) {
      ESP_LOGI(TAG, "%s", line);
      pos = snprintf(line, sizeof(line), "BDR_SNAPSHOT:");
    }
    pos += snprintf(line + pos, sizeof(line) - pos, "%s%04X:%02X",
                    (pos > 13) ? "," : "", idx, sub);
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

      // Decode numeric value if numeric/enum type
      bool valid_numeric = false;
      float numeric_val = 0.0f;
      int32_t raw_enum_val = 0;

      switch (dp.data_type) {
        case BDR_TYPE_U8:
          if (data_len >= 1 && data[0] != 0xFF) {
            numeric_val = static_cast<float>(data[0]) * dp.scale;
            valid_numeric = true;
          }
          break;

        case BDR_TYPE_I8:
          if (data_len >= 1) {
            int8_t v = static_cast<int8_t>(data[0]);
            if (v != -128) {
              numeric_val = static_cast<float>(v) * dp.scale;
              valid_numeric = true;
            }
          }
          break;

        case BDR_TYPE_U16:
          if (data_len >= 2) {
            uint16_t v = static_cast<uint16_t>(data[0]) |
                         (static_cast<uint16_t>(data[1]) << 8);
            if (v != 0xFFFF) {
              numeric_val = static_cast<float>(v) * dp.scale;
              valid_numeric = true;
            }
          }
          break;

        case BDR_TYPE_I16:
          if (data_len >= 2) {
            int16_t v = static_cast<int16_t>(
                static_cast<uint16_t>(data[0]) |
                (static_cast<uint16_t>(data[1]) << 8));
            if (v != -32768) {
              numeric_val = static_cast<float>(v) * dp.scale;
              valid_numeric = true;
            }
          }
          break;

        case BDR_TYPE_U32:
          if (data_len >= 4) {
            uint32_t v = static_cast<uint32_t>(data[0]) |
                         (static_cast<uint32_t>(data[1]) << 8) |
                         (static_cast<uint32_t>(data[2]) << 16) |
                         (static_cast<uint32_t>(data[3]) << 24);
            if (v != 0xFFFFFFFFUL) {
              numeric_val = static_cast<float>(v) * dp.scale;
              valid_numeric = true;
            }
          } else if (data_len == 2) {
            uint16_t v = static_cast<uint16_t>(data[0]) |
                         (static_cast<uint16_t>(data[1]) << 8);
            if (v != 0xFFFF) {
              numeric_val = static_cast<float>(v) * dp.scale;
              valid_numeric = true;
            }
          }
          break;

        case BDR_TYPE_I32:
          if (data_len >= 4) {
            int32_t v = static_cast<int32_t>(
                static_cast<uint32_t>(data[0]) |
                (static_cast<uint32_t>(data[1]) << 8) |
                (static_cast<uint32_t>(data[2]) << 16) |
                (static_cast<uint32_t>(data[3]) << 24));
            if (v != static_cast<int32_t>(0x80000000UL)) {
              numeric_val = static_cast<float>(v) * dp.scale;
              valid_numeric = true;
            }
          }
          break;

        case BDR_TYPE_ENUM:
          if (data_len >= 2) {
            raw_enum_val = static_cast<int32_t>(
                static_cast<uint16_t>(data[0]) |
                (static_cast<uint16_t>(data[1]) << 8));
          } else {
            raw_enum_val = data[0];
          }
          numeric_val = static_cast<float>(raw_enum_val);
          valid_numeric = true;
          break;

        default:
          break;
      }

#ifdef USE_SENSOR
      if (valid_numeric) {
        for (auto *sens : dp.sensors) {
          sens->publish_state(numeric_val);
        }
      }
#endif

#ifdef USE_TEXT_SENSOR
      if (!dp.text_sensors.empty()) {
        std::string text_val;
        if (dp.data_type == BDR_TYPE_ENUM) {
          uint64_t key =
              (static_cast<uint64_t>(index) << 24) |
              (static_cast<uint64_t>(sub_index) << 16) |
              (static_cast<uint64_t>(static_cast<uint16_t>(raw_enum_val)));
          auto it = this->enum_translations_.find(key);
          if (it != this->enum_translations_.end()) {
            text_val = it->second;
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

}  // namespace bdr_ble
}  // namespace esphome
