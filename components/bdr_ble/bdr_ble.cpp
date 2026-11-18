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
  if (this->has_pin_) {
    ESP_LOGCONFIG(TAG, "  PIN: ****%02u (Passkey pairing mode)",
                  static_cast<unsigned>(this->pin_ % 100));
  } else {
    ESP_LOGCONFIG(TAG, "  PIN: none (GTW-IoT plain BLE mode)");
  }
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

#ifdef USE_BINARY_SENSOR
void BdrBleComponent::register_binary_sensor(
    binary_sensor::BinarySensor *bsens, uint16_t index, uint8_t sub_index,
    uint8_t data_type, uint32_t interval_ms, uint8_t node_id) {
  auto &dp = this->get_or_create_datapoint_(
      interval_ms, node_id, index, sub_index,
      static_cast<BdrDataType>(data_type), 1.0f);
  dp.binary_sensors.push_back(bsens);
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
  if (this->state_ != STATE_READY) {
    ESP_LOGW(TAG, "Cannot write 0x%04X:%02X: BLE session not ready (state=%u)",
             index, sub_index, static_cast<unsigned>(this->state_));
    return;
  }
  for (auto &w : this->write_queue_) {
    if (w.node_id == node_id && w.index == index && w.sub_index == sub_index) {
      w.data = data;
      ESP_LOGI(TAG, "Updated queued write for 0x%04X:%02X (%zu bytes)", index,
               sub_index, data.size());
      return;
    }
  }
  if (this->write_queue_.size() >= MAX_WRITE_QUEUE_SIZE) {
    ESP_LOGW(TAG, "Write queue full (%zu items); dropping oldest pending write",
             this->write_queue_.size());
    this->write_queue_.pop_front();
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

void BdrBleComponent::resolve_gatt_handles_() {
  if (this->parent() == nullptr) {
    return;
  }
  namespace espbt = esphome::esp32_ble_tracker;
  auto gattc_if = this->parent()->get_gattc_if();
  auto conn_id = this->parent()->get_conn_id();

  uint16_t count = 0;
  esp_gatt_status_t st = esp_ble_gattc_get_attr_count(
      gattc_if, conn_id, ESP_GATT_DB_ALL, 0x0001, 0xFFFF, 0, &count);
  if (st != ESP_GATT_OK || count == 0) {
    ESP_LOGW(TAG, "GATT DB empty or unavailable (status=%d); using default handles",
             st);
    return;
  }

  std::vector<esp_gattc_db_elem_t> db(count);
  uint16_t got = count;
  st = esp_ble_gattc_get_db(gattc_if, conn_id, 0x0001, 0xFFFF, db.data(), &got);
  if (st != ESP_GATT_OK || got == 0) {
    ESP_LOGW(TAG, "esp_ble_gattc_get_db failed (status=%d); using default handles",
             st);
    return;
  }

  uint16_t tx = 0, rx = 0, rx_cccd = 0;
  uint16_t ch1 = 0, ch1_cccd = 0, ch2 = 0, ch2_cccd = 0;
  uint16_t s3_trig = 0, s3_chal = 0, s3_chal_cccd = 0, s3_resp = 0, s3_ack = 0,
           s3_ack_cccd = 0;

  struct Target {
    espbt::ESPBTUUID uuid;
    uint16_t *val_handle;
    uint16_t *cccd_handle;
  };
  Target targets[] = {
      {espbt::ESPBTUUID::from_raw(UUID_DIRECT_TX), &tx, nullptr},
      {espbt::ESPBTUUID::from_raw(UUID_DIRECT_RX), &rx, &rx_cccd},
      {espbt::ESPBTUUID::from_raw(UUID_SVC2_CH1), &ch1, &ch1_cccd},
      {espbt::ESPBTUUID::from_raw(UUID_SVC2_CH2), &ch2, &ch2_cccd},
      {espbt::ESPBTUUID::from_raw(UUID_SVC3_TRIG), &s3_trig, nullptr},
      {espbt::ESPBTUUID::from_raw(UUID_SVC3_CHAL), &s3_chal, &s3_chal_cccd},
      {espbt::ESPBTUUID::from_raw(UUID_SVC3_RESP), &s3_resp, nullptr},
      {espbt::ESPBTUUID::from_raw(UUID_SVC3_ACK), &s3_ack, &s3_ack_cccd},
  };
  constexpr size_t num_targets = sizeof(targets) / sizeof(targets[0]);

  int active_target = -1;
  for (uint16_t i = 0; i < got; i++) {
    const auto &elem = db[i];
    if (elem.type == ESP_GATT_DB_CHARACTERISTIC) {
      active_target = -1;
      espbt::ESPBTUUID char_uuid = espbt::ESPBTUUID::from_uuid(elem.uuid);
      for (size_t k = 0; k < num_targets; k++) {
        if (char_uuid == targets[k].uuid) {
          *targets[k].val_handle = elem.attribute_handle;
          active_target = static_cast<int>(k);
          break;
        }
      }
    } else if (elem.type == ESP_GATT_DB_DESCRIPTOR &&
               elem.uuid.len == ESP_UUID_LEN_16 &&
               elem.uuid.uuid.uuid16 == 0x2902) {
      if (active_target >= 0 && targets[active_target].cccd_handle != nullptr) {
        *targets[active_target].cccd_handle = elem.attribute_handle;
      }
    }
  }

  if (tx != 0 && rx != 0) {
    this->h_direct_tx_ = tx;
    this->h_direct_rx_ = rx;
    this->h_direct_rx_cccd_ = (rx_cccd != 0) ? rx_cccd : static_cast<uint16_t>(rx + 1);
    this->h_svc2_ch1_ = ch1;
    this->h_svc2_ch1_cccd_ = ch1_cccd;
    this->h_svc2_ch2_ = ch2;
    this->h_svc2_ch2_cccd_ = ch2_cccd;
    if (s3_trig != 0) this->h_svc3_trig_ = s3_trig;
    if (s3_chal != 0) {
      this->h_svc3_chal_ = s3_chal;
      this->h_svc3_chal_cccd_ =
          (s3_chal_cccd != 0) ? s3_chal_cccd : static_cast<uint16_t>(s3_chal + 1);
    }
    if (s3_resp != 0) this->h_svc3_resp_ = s3_resp;
    if (s3_ack != 0) {
      this->h_svc3_ack_ = s3_ack;
      this->h_svc3_ack_cccd_ =
          (s3_ack_cccd != 0) ? s3_ack_cccd : static_cast<uint16_t>(s3_ack + 1);
    }
    ESP_LOGI(TAG,
             "Resolved GATT handles by UUID: Direct(tx=%u rx=%u/%u) "
             "Svc3(trig=%u chal=%u/%u resp=%u ack=%u/%u) Aux(ch1=%u ch2=%u)",
             this->h_direct_tx_, this->h_direct_rx_, this->h_direct_rx_cccd_,
             this->h_svc3_trig_, this->h_svc3_chal_, this->h_svc3_chal_cccd_,
             this->h_svc3_resp_, this->h_svc3_ack_, this->h_svc3_ack_cccd_,
             this->h_svc2_ch1_, this->h_svc2_ch2_);
  } else {
    ESP_LOGW(TAG, "DirectChannel UUIDs not found in GATT DB; using default EHC-08 handles");
  }
}

void BdrBleComponent::handle_pairing_failure_(const uint8_t *bda,
                                              uint8_t reason) {
  bool had_bond = false;
  int bond_num = esp_ble_get_bond_device_num();
  if (bond_num > 0 && bda != nullptr) {
    std::vector<esp_ble_bond_dev_t> bond_list(bond_num);
    if (esp_ble_get_bond_device_list(&bond_num, bond_list.data()) == ESP_OK) {
      for (int i = 0; i < bond_num; i++) {
        if (std::memcmp(bond_list[i].bd_addr, bda, 6) == 0) {
          had_bond = true;
          break;
        }
      }
    }
  }

  // The appliance's GTW-Blue gateway stores at most 3 paired devices and evicts
  // the oldest bond when a 4th device pairs. If we already had a local bond and
  // encryption failed with a non-transient error, remove our stale bond so the
  // next connection attempt performs a fresh Passkey pairing.
  if (had_bond && !is_transient_ble_auth_failure(reason)) {
    ESP_LOGW(TAG,
             "Stored BLE bond rejected by gateway (reason=0x%02X: %s); "
             "removing stale local bond to re-pair cleanly",
             reason, ble_auth_fail_reason_str(reason));
    esp_bd_addr_t addr;
    std::memcpy(addr, bda, 6);
    esp_ble_remove_bond_device(addr);
  } else if (reason == 0x51 || reason == 0x4E) {
    ESP_LOGE(TAG,
             "BLE Passkey rejected (0x%02X: %s) — verify your 6-digit PIN in YAML",
             reason, ble_auth_fail_reason_str(reason));
  }

  // After a failed pairing, the gateway locks out new pairing attempts for 60 seconds
  // (returning 0x56 Repeated Attempts if retried early). Pause ble_client for 90..600s.
  size_t idx = (this->pairing_fail_count_ < 4) ? this->pairing_fail_count_ : 3;
  uint32_t wait_s = PAIRING_BACKOFF_SECONDS[idx];
  if (this->pairing_fail_count_ < 255) {
    this->pairing_fail_count_++;
  }

  this->pairing_failed_ = true;
  this->pairing_backoff_active_ = true;
  this->pairing_backoff_until_ms_ = millis() + wait_s * 1000UL;
  this->state_ = STATE_DISCONNECTED;
  this->active_group_idx_ = -1;
  this->in_flight_type_ = IN_FLIGHT_NONE;
  this->tx_frame_buf_.clear();
  this->tx_frame_offset_ = 0;
  this->tx_seg_idx_ = 0;
  this->tx_in_progress_ = false;
  this->rx_reassembly_buf_.clear();
  this->expected_rx_seg_ = 0;
  this->write_queue_.clear();

  char msg[96];
  snprintf(msg, sizeof(msg), "Pairing failed (0x%02X: %s) — retry in %us",
           reason, ble_auth_fail_reason_str(reason),
           static_cast<unsigned>(wait_s));
  ESP_LOGW(TAG, "%s (attempt %u)", msg,
           static_cast<unsigned>(this->pairing_fail_count_));
  this->publish_status_(msg);

  if (this->parent() != nullptr) {
    this->parent()->set_enabled(false);
  }
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
  this->tx_frame_buf_ = wrap_direct_channel_frame(payload, len);
  this->tx_frame_offset_ = 0;
  this->tx_seg_idx_ = 0;
  this->tx_in_progress_ = true;
  this->send_next_tx_fragment_();
}

void BdrBleComponent::send_next_tx_fragment_() {
  if (this->parent() == nullptr ||
      this->tx_frame_offset_ >= this->tx_frame_buf_.size()) {
    this->tx_in_progress_ = false;
    this->tx_frame_buf_.clear();
    return;
  }

  size_t rem = this->tx_frame_buf_.size() - this->tx_frame_offset_;
  bool is_last = (rem <= DIRECT_TX_MAX_CHUNK_DATA);
  size_t chunk_len = is_last ? rem : DIRECT_TX_MAX_CHUNK_DATA;

  uint8_t pkt[1 + DIRECT_TX_MAX_CHUNK_DATA];
  pkt[0] = is_last ? SEGMENT_FINAL : this->tx_seg_idx_++;
  std::memcpy(&pkt[1], this->tx_frame_buf_.data() + this->tx_frame_offset_,
              chunk_len);
  this->tx_frame_offset_ += chunk_len;

  const uint32_t now = millis();
  this->last_req_ms_ = now;
  this->last_action_ms_ = now;

  esp_err_t err = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
      this->h_direct_tx_, chunk_len + 1, pkt, ESP_GATT_WRITE_TYPE_RSP,
      ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "DirectChannel write_char failed: %d", err);
    this->tx_in_progress_ = false;
    this->tx_frame_buf_.clear();
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
        this->pairing_failed_ = false;
        this->pairing_backoff_active_ = false;
        this->auth_cmpl_handled_ = false;
        this->gatt_search_completed_ = false;
        this->ble_sec_ready_ = !this->has_pin_;
        this->h_direct_tx_ = HANDLE_DIRECT_TX;
        this->h_direct_rx_ = HANDLE_DIRECT_RX;
        this->h_direct_rx_cccd_ = HANDLE_DIRECT_RX_CCCD;
        this->h_svc2_ch1_ = HANDLE_SVC2_CH1;
        this->h_svc2_ch1_cccd_ = HANDLE_SVC2_CH1_CCCD;
        this->h_svc2_ch2_ = HANDLE_SVC2_CH2;
        this->h_svc2_ch2_cccd_ = HANDLE_SVC2_CH2_CCCD;
        this->h_svc3_trig_ = HANDLE_SVC3_TRIG;
        this->h_svc3_chal_ = HANDLE_SVC3_CHAL;
        this->h_svc3_chal_cccd_ = HANDLE_SVC3_CHAL_CCCD;
        this->h_svc3_resp_ = HANDLE_SVC3_RESP;
        this->h_svc3_ack_ = HANDLE_SVC3_ACK;
        this->h_svc3_ack_cccd_ = HANDLE_SVC3_ACK_CCCD;
        this->cccd_step_ = 0;
        this->retry_count_ = 0;
        this->consecutive_timeouts_ = 0;
        this->active_group_idx_ = -1;
        this->in_flight_type_ = IN_FLIGHT_NONE;
        this->get_list_stage_ = 0;
        this->get_list_use_v200_ = true;
        this->negotiated_mtu_ = 23;
        this->tx_frame_buf_.clear();
        this->tx_frame_offset_ = 0;
        this->tx_seg_idx_ = 0;
        this->tx_in_progress_ = false;
        this->rx_reassembly_buf_.clear();
        this->expected_rx_seg_ = 0;
        this->discovered_datapoints_.clear();
        this->write_queue_.clear();
        for (auto &grp : this->poll_groups_) {
          for (auto &dp : grp.items) {
            dp.unsupported = false;
            dp.single_read_only = false;
          }
        }
        this->last_action_ms_ = millis();
        if (this->has_pin_) {
          this->publish_status_("Connected (pairing...)");
          if (this->parent() != nullptr) {
            this->parent()->pair();
          }
        } else {
          this->publish_status_("Connected (GTW-IoT / no PIN)");
        }
      } else {
        ESP_LOGW(TAG, "GATT open failed: status=%d", param->open.status);
        this->publish_status_("Connection Failed");
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
      this->gatt_search_completed_ = false;
      this->ble_sec_ready_ = false;
      this->active_group_idx_ = -1;
      this->in_flight_type_ = IN_FLIGHT_NONE;
      this->tx_frame_buf_.clear();
      this->tx_frame_offset_ = 0;
      this->tx_seg_idx_ = 0;
      this->tx_in_progress_ = false;
      this->rx_reassembly_buf_.clear();
      this->expected_rx_seg_ = 0;
      this->write_queue_.clear();
      if (!this->pairing_backoff_active_) {
        this->publish_status_("Disconnected");
      }
      break;

    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == this->h_direct_tx_) {
        if (param->write.status != ESP_GATT_OK) {
          ESP_LOGW(TAG, "DirectChannel GATT write failed: status=0x%02X",
                   param->write.status);
          this->tx_in_progress_ = false;
          this->tx_frame_buf_.clear();
        } else if (this->tx_in_progress_) {
          this->send_next_tx_fragment_();
        }
      }
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      this->resolve_gatt_handles_();
      const uint16_t notify_handles[5] = {
          this->h_svc3_chal_, this->h_svc3_ack_, this->h_direct_rx_,
          this->h_svc2_ch1_, this->h_svc2_ch2_};
      for (uint16_t h : notify_handles) {
        if (h != 0) {
          esp_ble_gattc_register_for_notify(
              gattc_if, this->parent()->get_remote_bda(), h);
        }
      }
      this->node_state = esphome::esp32_ble_tracker::ClientState::ESTABLISHED;
      this->gatt_search_completed_ = true;
      this->last_action_ms_ = millis();
      if (this->ble_sec_ready_ && this->state_ == STATE_CONNECTED_WAIT_SEC) {
        this->publish_status_("Subscribing to GATT notifications");
        this->state_ = STATE_SUBSCRIBING;
        this->cccd_step_ = 0;
      } else {
        this->publish_status_("GATT ready (waiting for encryption)");
      }
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
    if (this->parent() == nullptr ||
        std::memcmp(param->ble_security.auth_cmpl.bd_addr,
                    this->parent()->get_remote_bda(), 6) != 0) {
      return;
    }
    if (this->auth_cmpl_handled_) {
      return;
    }
    this->auth_cmpl_handled_ = true;

    if (param->ble_security.auth_cmpl.success) {
      ESP_LOGI(TAG, "BLE link encrypted; subscribing to GATT notifications");
      this->pairing_failed_ = false;
      this->pairing_fail_count_ = 0;
      this->pairing_backoff_active_ = false;
      this->ble_sec_ready_ = true;
      if (this->gatt_search_completed_ &&
          this->state_ == STATE_CONNECTED_WAIT_SEC) {
        this->state_ = STATE_SUBSCRIBING;
        this->cccd_step_ = 0;
        this->last_action_ms_ = millis();
      }
    } else {
      this->handle_pairing_failure_(param->ble_security.auth_cmpl.bd_addr,
                                    param->ble_security.auth_cmpl.fail_reason);
    }
  }
}

void BdrBleComponent::loop() {
  if (this->parent() == nullptr) {
    return;
  }
  const uint32_t now = millis();

  if (this->pairing_backoff_active_) {
    if (static_cast<int32_t>(now - this->pairing_backoff_until_ms_) >= 0) {
      this->pairing_backoff_active_ = false;
      ESP_LOGI(TAG, "BLE pairing backoff elapsed; re-enabling BLE client");
      this->publish_status_("Retrying BLE connection...");
      this->parent()->set_enabled(true);
    }
    return;
  }

  if (this->state_ == STATE_DISCONNECTED) {
    return;
  }

  if (this->state_ == STATE_AUTH_FAILED) {
    if (!this->pairing_failed_ && (now - this->last_action_ms_ >= AUTH_COOLDOWN_MS)) {
      ESP_LOGI(TAG, "Auth cooldown elapsed; retrying BLE unlock");
      this->publish_status_("Unlocking BLE (Service 3)");
      this->send_zero_length_write_(this->h_svc3_trig_);
      this->state_ = STATE_SVC3_WAIT_CHALLENGE;
      this->retry_count_ = 0;
      this->last_action_ms_ = now;
    }
    return;
  }

  if (this->state_ == STATE_CONNECTED_WAIT_SEC) {
    if (now - this->last_action_ms_ >= PAIRING_TIMEOUT_MS) {
      ESP_LOGW(TAG, "BLE pairing/encryption timed out after %ums",
               static_cast<unsigned>(PAIRING_TIMEOUT_MS));
      this->handle_pairing_failure_(this->parent()->get_remote_bda(), 0x63);
    }
    return;
  }

  if (this->state_ == STATE_SUBSCRIBING) {
    if (now - this->last_action_ms_ < 250) {
      return;
    }
    this->last_action_ms_ = now;

    const uint16_t cccd_handles[5] = {
        this->h_svc3_chal_cccd_, this->h_svc3_ack_cccd_,
        this->h_direct_rx_cccd_, this->h_svc2_ch1_cccd_,
        this->h_svc2_ch2_cccd_};
    while (this->cccd_step_ < 5 && cccd_handles[this->cccd_step_] == 0) {
      this->cccd_step_++;
    }
    if (this->cccd_step_ < 5) {
      this->write_cccd_(cccd_handles[this->cccd_step_]);
      this->cccd_step_++;
    } else {
      this->publish_status_("Unlocking BLE (Service 3)");
      this->send_zero_length_write_(this->h_svc3_trig_);
      this->state_ = STATE_SVC3_WAIT_CHALLENGE;
      this->retry_count_ = 0;
    }
    return;
  }

  if (this->state_ == STATE_SVC3_WAIT_CHALLENGE ||
      this->state_ == STATE_SVC3_WAIT_RESULT) {
    if (now - this->last_action_ms_ > HANDSHAKE_TIMEOUT_MS) {
      this->last_action_ms_ = now;
      if (this->retry_count_ < 3) {
        this->retry_count_++;
        this->state_ = STATE_SVC3_WAIT_CHALLENGE;
        this->send_zero_length_write_(this->h_svc3_trig_);
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

  if (this->state_ == STATE_RUB_WAIT_CHALLENGE ||
      this->state_ == STATE_RUB_WAIT_AUTH_ACK) {
    if (now - this->last_action_ms_ > HANDSHAKE_TIMEOUT_MS) {
      this->last_action_ms_ = now;
      if (this->retry_count_ < 3) {
        this->retry_count_++;
        this->state_ = STATE_RUB_WAIT_CHALLENGE;
        const uint8_t rub_chal_req[6] = {
            0x02, 0x01, 0x00, MSG_TYPE_RUB_AUTH, RUB_FUNC_CHALLENGE_REQ, 0x03};
        this->send_direct_channel_(rub_chal_req, sizeof(rub_chal_req));
      } else {
        ESP_LOGW(TAG, "RUB v2.01 authentication timed out after 3 retries");
        this->publish_status_("CAN-BUS Auth Timeout");
        this->state_ = STATE_AUTH_FAILED;
      }
    }
    return;
  }

  if (this->state_ == STATE_READING_SNAPSHOT) {
    if (this->in_flight_type_ != IN_FLIGHT_NONE &&
        (now - this->last_req_ms_ >= REQUEST_TIMEOUT_MS)) {
      ESP_LOGD(TAG, "Snapshot 0x502B:%02X timed out; advancing",
               this->snapshot_sub_idx_);
      this->in_flight_type_ = IN_FLIGHT_NONE;
      this->snapshot_sub_idx_++;
    }
    if (this->in_flight_type_ == IN_FLIGHT_NONE &&
        (now - this->last_action_ms_ >= MIN_TX_INTERVAL_MS)) {
      if (this->snapshot_sub_idx_ <= SNAPSHOT_MAX_SUB_INDEX) {
        this->send_read_param_(this->default_node_id_, INDEX_AP_MENU_DESCRIPTOR,
                               this->snapshot_sub_idx_);
        this->in_flight_type_ = IN_FLIGHT_SNAPSHOT;
        this->in_flight_node_ = this->default_node_id_;
        this->in_flight_index_ = INDEX_AP_MENU_DESCRIPTOR;
        this->in_flight_sub_ = this->snapshot_sub_idx_;
        this->last_req_ms_ = now;
        this->last_action_ms_ = now;
      } else {
        this->log_appliance_snapshot_();
        this->publish_status_("Ready");
        this->state_ = STATE_READY;
        this->active_group_idx_ = -1;
        this->in_flight_type_ = IN_FLIGHT_NONE;
        this->consecutive_timeouts_ = 0;
        for (auto &grp : this->poll_groups_) {
          grp.last_cycle_start_ms = 0;
        }
        this->last_action_ms_ = now;
      }
    }
    return;
  }

  if (this->state_ == STATE_READY) {
    // Wait for multi-fragment TX to finish before timing out or starting a new request
    if (this->tx_in_progress_) {
      if (now - this->last_action_ms_ >= REQUEST_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Multi-fragment TX stalled; resetting TX state");
        this->tx_in_progress_ = false;
        this->tx_frame_buf_.clear();
      } else {
        return;
      }
    }

    // Check if an active request timed out
    if (this->in_flight_type_ != IN_FLIGHT_NONE &&
        (now - this->last_req_ms_ >= REQUEST_TIMEOUT_MS)) {
      InFlightType timed_out_type = this->in_flight_type_;
      this->in_flight_type_ = IN_FLIGHT_NONE;
      this->consecutive_timeouts_++;

      if (this->consecutive_timeouts_ >= MAX_CONSECUTIVE_TIMEOUTS) {
        ESP_LOGW(TAG, "%u consecutive request timeouts; re-authenticating RUB session",
                 this->consecutive_timeouts_);
        this->consecutive_timeouts_ = 0;
        this->publish_status_("Re-authenticating CAN-BUS");
        const uint8_t rub_chal_req[6] = {
            0x02, 0x01, 0x00, MSG_TYPE_RUB_AUTH, RUB_FUNC_CHALLENGE_REQ, 0x03};
        this->send_direct_channel_(rub_chal_req, sizeof(rub_chal_req));
        this->state_ = STATE_RUB_WAIT_CHALLENGE;
        this->retry_count_ = 0;
        this->last_action_ms_ = now;
        return;
      }

      if (timed_out_type == IN_FLIGHT_WRITE) {
        ESP_LOGW(TAG,
                 "Write confirmation for 0x%04X:%02X timed out; reading back value",
                 this->in_flight_index_, this->in_flight_sub_);
        this->send_read_param_(this->in_flight_node_, this->in_flight_index_,
                               this->in_flight_sub_);
        this->in_flight_type_ = IN_FLIGHT_READ;
        this->last_req_ms_ = now;
        this->last_action_ms_ = now;
        return;
      }

      if (timed_out_type == IN_FLIGHT_GET_LIST && this->active_group_idx_ >= 0 &&
          this->get_list_stage_ < 2) {
        this->get_list_stage_++;
        if (this->get_list_stage_ == 1) {
          ESP_LOGI(TAG, "GetListRequest v2.00 timed out; trying v2.01 header");
          this->get_list_use_v200_ = false;
          this->active_item_idx_ = 0;
        } else {
          ESP_LOGI(TAG,
                   "GetListRequest not supported over DirectChannel; using sequential single-read polling");
          this->get_list_stage_ = 3;
          this->active_item_idx_ = 0;
        }
      }
    }

    // Priority 1: Execute any queued parameter writes immediately
    if (!this->write_queue_.empty() && this->in_flight_type_ == IN_FLIGHT_NONE &&
        (now - this->last_action_ms_ >= MIN_TX_INTERVAL_MS)) {
      PendingWrite pw = this->write_queue_.front();
      this->write_queue_.pop_front();
      this->send_write_param_(pw.node_id, pw.index, pw.sub_index,
                              pw.data.data(), pw.data.size());
      this->in_flight_type_ = IN_FLIGHT_WRITE;
      this->in_flight_node_ = pw.node_id;
      this->in_flight_index_ = pw.index;
      this->in_flight_sub_ = pw.sub_index;
      this->last_req_ms_ = now;
      this->last_action_ms_ = now;
      return;
    }

    if (this->poll_groups_.empty()) {
      return;
    }

    // If no group is currently bursting, select the most overdue group
    if (this->active_group_idx_ < 0) {
      int best_group = -1;
      uint32_t max_overdue = 0;
      for (size_t g = 0; g < this->poll_groups_.size(); g++) {
        auto &grp = this->poll_groups_[g];
        bool has_supported = false;
        for (const auto &dp : grp.items) {
          if (!dp.unsupported) {
            has_supported = true;
            break;
          }
        }
        if (!has_supported) {
          continue;
        }
        if (grp.last_cycle_start_ms == 0) {
          best_group = static_cast<int>(g);
          break;
        }
        uint32_t elapsed = now - grp.last_cycle_start_ms;
        if (elapsed >= grp.interval_ms) {
          uint32_t overdue = elapsed - grp.interval_ms;
          if (best_group < 0 || overdue > max_overdue) {
            best_group = static_cast<int>(g);
            max_overdue = overdue;
          }
        }
      }
      if (best_group >= 0) {
        this->active_group_idx_ = best_group;
        this->active_item_idx_ = 0;
        this->poll_groups_[best_group].last_cycle_start_ms = (now == 0) ? 1 : now;
      }
    }

    // Execute batch GetListRequest or sequential read for the active group
    if (this->active_group_idx_ >= 0 && this->in_flight_type_ == IN_FLIGHT_NONE &&
        (now - this->last_action_ms_ >= MIN_TX_INTERVAL_MS)) {
      auto &grp = this->poll_groups_[this->active_group_idx_];
      while (this->active_item_idx_ < grp.items.size() &&
             grp.items[this->active_item_idx_].unsupported) {
        this->active_item_idx_++;
      }
      if (this->active_item_idx_ < grp.items.size()) {
        if (this->get_list_stage_ > 2 ||
            grp.items[this->active_item_idx_].single_read_only) {
          const auto &dp = grp.items[this->active_item_idx_++];
          this->send_read_param_(dp.node_id, dp.index, dp.sub_index);
          this->in_flight_type_ = IN_FLIGHT_READ;
          this->in_flight_node_ = dp.node_id;
          this->in_flight_index_ = dp.index;
          this->in_flight_sub_ = dp.sub_index;
          this->last_req_ms_ = now;
          this->last_action_ms_ = now;
          return;
        }

        std::vector<size_t> batch_indices;
        size_t scan_idx = this->active_item_idx_;
        while (scan_idx < grp.items.size() && batch_indices.size() < 16) {
          if (grp.items[scan_idx].single_read_only) {
            break;
          }
          if (!grp.items[scan_idx].unsupported) {
            batch_indices.push_back(scan_idx);
          }
          scan_idx++;
        }
        this->active_item_idx_ = scan_idx;

        if (batch_indices.size() > 1) {
          this->send_get_list_batch_(grp, batch_indices,
                                     this->get_list_use_v200_);
          this->in_flight_type_ = IN_FLIGHT_GET_LIST;
          this->last_req_ms_ = now;
          this->last_action_ms_ = now;
        } else if (!batch_indices.empty()) {
          const auto &dp = grp.items[batch_indices[0]];
          this->send_read_param_(dp.node_id, dp.index, dp.sub_index);
          this->in_flight_type_ = IN_FLIGHT_READ;
          this->in_flight_node_ = dp.node_id;
          this->in_flight_index_ = dp.index;
          this->in_flight_sub_ = dp.sub_index;
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
