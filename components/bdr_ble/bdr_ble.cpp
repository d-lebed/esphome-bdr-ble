#include "bdr_ble.h"

#include <cstdio>
#include <cstring>

#ifdef USE_ESP32
#if __has_include("bta/bta_gatt_api.h") && __has_include("bta/bta_api.h")
#include "bta/bta_api.h"
#include "bta/bta_gatt_api.h"
#else
extern "C" void BTA_GATTC_WriteCharValue(uint16_t conn_id, uint16_t handle,
                                         uint8_t write_type, uint16_t len,
                                         uint8_t *p_value, uint8_t auth_req);
#endif
#endif

namespace esphome::bdr_ble {

void BdrBleComponent::setup() {
  this->transport_.reserve_buffers();
  this->registry_.reserve();
  ESP_LOGI(TAG, "BDR Thermea BLE component initialized (%zu poll groups)",
           this->registry_.groups().size());
}

void BdrBleComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "BDR Thermea BLE Boiler Controller:");
  ESP_LOGCONFIG(TAG, "  Default Node ID: 0x%02X", this->default_node_id_);
  ESP_LOGCONFIG(TAG, "  Auth Level: %u", this->auth_level_);
  ESP_LOGCONFIG(TAG, "  Log Snapshot: %s",
                this->log_snapshot_ ? "true" : "false");
  if (this->has_pin_) {
    ESP_LOGCONFIG(TAG, "  PIN: ****%02u (Passkey pairing mode)",
                  static_cast<unsigned>(this->pin_ % 100));
  } else {
    ESP_LOGCONFIG(TAG, "  PIN: none (GTW-IoT plain BLE mode)");
  }
  const auto &groups = this->registry_.groups();
  ESP_LOGCONFIG(TAG, "  Poll Groups: %zu", groups.size());
  for (size_t i = 0; i < groups.size(); i++) {
    ESP_LOGCONFIG(TAG, "    Group %zu: interval=%ums, datapoints=%zu", i,
                  static_cast<unsigned>(groups[i].interval_ms),
                  groups[i].items.size());
  }
}

void BdrBleComponent::reset_active_transaction_() {
  this->transport_.reset();
  this->active_req_ = {};
}

void BdrBleComponent::reset_session_state_(bool clear_writes) {
  this->reset_active_transaction_();
  this->active_group_idx_ = -1;
  this->active_item_idx_ = 0;
  this->cccd_substate_ = CCCD_IDLE;
  this->cccd_manual_write_sent_ = false;
  this->cccd_step_ = 0;
  this->retry_count_ = 0;
  this->consecutive_timeouts_ = 0;
  this->cooldown_until_ms_ = 0;
  this->upgraded_nodes_mask_ = 0;
  this->pending_upgrade_nodes_mask_ = 0;
  if (clear_writes) {
    this->write_queue_.clear();
  }
}

#ifdef USE_SENSOR
void BdrBleComponent::register_sensor(sensor::Sensor *sens, uint16_t index,
                                      uint8_t sub_index, uint8_t data_type,
                                      float scale, uint32_t interval_ms,
                                      uint8_t node_id) {
  bool is_counter = (sens != nullptr &&
                     sens->get_state_class() ==
                         sensor::StateClass::STATE_CLASS_TOTAL_INCREASING);
  auto &dp = this->registry_.get_or_create_datapoint(
      interval_ms, this->resolve_node_id_(node_id), index, sub_index,
      static_cast<BdrDataType>(data_type), scale, is_counter);
  dp.sensors.push_back(sens);
}
#endif

#ifdef USE_BINARY_SENSOR
void BdrBleComponent::register_binary_sensor(
    binary_sensor::BinarySensor *bsens, uint16_t index, uint8_t sub_index,
    uint8_t data_type, uint32_t interval_ms, uint8_t node_id) {
  auto &dp = this->registry_.get_or_create_datapoint(
      interval_ms, this->resolve_node_id_(node_id), index, sub_index,
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
  auto &dp = this->registry_.get_or_create_datapoint(
      interval_ms, this->resolve_node_id_(node_id), index, sub_index,
      static_cast<BdrDataType>(data_type), 1.0f);
  dp.text_sensors.push_back(sens);
}
#endif

#if defined(USE_TEXT_SENSOR) || defined(USE_SELECT)
void BdrBleComponent::register_enum_table(uint16_t index, uint8_t sub_index,
                                          const EnumEntry *table, size_t len,
                                          uint8_t node_id) {
  this->registry_.set_enum_table(this->resolve_node_id_(node_id), index,
                                 sub_index, table, len);
}
#endif

#ifdef USE_NUMBER
void BdrBleComponent::register_number(number::Number *num, uint16_t index,
                                      uint8_t sub_index, uint8_t data_type,
                                      float scale, uint32_t interval_ms,
                                      uint8_t node_id) {
  auto &dp = this->registry_.get_or_create_datapoint(
      interval_ms, this->resolve_node_id_(node_id), index, sub_index,
      static_cast<BdrDataType>(data_type), scale);
  dp.numbers.push_back(num);
}
#endif

#ifdef USE_SELECT
void BdrBleComponent::register_select(select::Select *sel, uint16_t index,
                                      uint8_t sub_index, uint32_t interval_ms,
                                      uint8_t node_id) {
  auto &dp = this->registry_.get_or_create_datapoint(
      interval_ms, this->resolve_node_id_(node_id), index, sub_index,
      BDR_TYPE_ENUM, 1.0f);
  dp.selects.push_back(sel);
}
#endif

#ifdef USE_TEXT
void BdrBleComponent::register_text(text::Text *txt, uint16_t index,
                                    uint8_t sub_index, uint32_t interval_ms,
                                    uint8_t node_id) {
  auto &dp = this->registry_.get_or_create_datapoint(
      interval_ms, this->resolve_node_id_(node_id), index, sub_index,
      BDR_TYPE_VISIBLE_STRING, 1.0f);
  if (txt != nullptr && txt->traits.get_max_length() > 0 &&
      txt->traits.get_max_length() <= 255) {
    dp.max_len = static_cast<uint8_t>(txt->traits.get_max_length());
  }
  dp.texts.push_back(txt);
}
#endif

bool BdrBleComponent::queue_write_datapoint(uint8_t node_id, uint16_t index,
                                            uint8_t sub_index,
                                            const std::vector<uint8_t> &data) {
  if (this->state_ == STATE_DISCONNECTED || this->state_ == STATE_AUTH_FAILED ||
      this->pairing_failed_) {
    ESP_LOGW(TAG, "Cannot write 0x%04X:%02X: BLE session not connected (state=%u)",
             index, sub_index, static_cast<unsigned>(this->state_));
    return false;
  }
  uint8_t resolved_node = this->resolve_node_id_(node_id);
  for (auto &w : this->write_queue_) {
    if (w.node_id == resolved_node && w.index == index &&
        w.sub_index == sub_index) {
      w.data = data;
      ESP_LOGI(TAG, "Updated queued write for 0x%04X:%02X (%zu bytes)", index,
               sub_index, data.size());
      return true;
    }
  }
  if (this->write_queue_.size() >= MAX_WRITE_QUEUE_SIZE) {
    ESP_LOGW(TAG, "Write queue full (%zu items); dropping oldest pending write",
             this->write_queue_.size());
    this->write_queue_.pop_front();
  }
  PendingWrite pw;
  pw.node_id = resolved_node;
  pw.index = index;
  pw.sub_index = sub_index;
  pw.data = data;
  this->write_queue_.push_back(pw);
  ESP_LOGI(TAG, "Queued write for 0x%04X:%02X (%zu bytes)", index, sub_index,
           data.size());
  return true;
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
    this->h_direct_rx_cccd_ =
        (rx_cccd != 0) ? rx_cccd : static_cast<uint16_t>(rx + 1);
    this->h_svc2_ch1_ = ch1;
    this->h_svc2_ch1_cccd_ = ch1_cccd;
    this->h_svc2_ch2_ = ch2;
    this->h_svc2_ch2_cccd_ = ch2_cccd;

    // HIGH-09: Assign Service 3 handles directly so gateways without Service 3 (e.g. GTW-IoT)
    // get 0 instead of keeping the default EHC-08 handle 31.
    this->h_svc3_trig_ = s3_trig;
    this->h_svc3_chal_ = s3_chal;
    this->h_svc3_chal_cccd_ =
        (s3_chal != 0)
            ? ((s3_chal_cccd != 0) ? s3_chal_cccd
                                   : static_cast<uint16_t>(s3_chal + 1))
            : 0;
    this->h_svc3_resp_ = s3_resp;
    this->h_svc3_ack_ = s3_ack;
    this->h_svc3_ack_cccd_ =
        (s3_ack != 0)
            ? ((s3_ack_cccd != 0) ? s3_ack_cccd
                                  : static_cast<uint16_t>(s3_ack + 1))
            : 0;

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

  size_t idx = (this->pairing_fail_count_ < 4) ? this->pairing_fail_count_ : 3;
  uint32_t wait_s = PAIRING_BACKOFF_SECONDS[idx];
  if (this->pairing_fail_count_ < 255) {
    this->pairing_fail_count_++;
  }

  const uint32_t now = millis();
  this->pairing_failed_ = true;
  this->pairing_backoff_active_ = true;
  this->pairing_backoff_until_ms_ = now + wait_s * 1000UL;
  this->disconnected_since_ms_ = (now == 0) ? 1 : now;
  this->state_ = STATE_DISCONNECTED;
  this->reset_session_state_(true);
  if (!this->stale_published_) {
    this->registry_.publish_stale_nan();
    this->stale_published_ = true;
  }

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
#if ESPHOME_LOG_LEVEL < ESPHOME_LOG_LEVEL_DEBUG
  if (!info_level) {
    return;
  }
#endif
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
  if (this->parent() == nullptr || cccd_handle == 0) {
    return;
  }
  uint8_t val[2] = {0x01, 0x00};
  esp_ble_gattc_write_char_descr(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
      cccd_handle, sizeof(val), val, ESP_GATT_WRITE_TYPE_RSP,
      ESP_GATT_AUTH_REQ_NONE);
}

void BdrBleComponent::send_zero_length_write_(uint16_t char_handle) {
  if (this->parent() == nullptr || char_handle == 0) {
    return;
  }
  ESP_LOGI(TAG, "Triggering Service 3 challenge (0-byte write on handle %u)",
           char_handle);
#ifdef USE_ESP32
  esp_err_t err = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
      char_handle, 0, nullptr, ESP_GATT_WRITE_TYPE_RSP,
      ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    uint16_t bta_conn_id = static_cast<uint16_t>(
        (this->parent()->get_conn_id() << 8) |
        (this->parent()->get_gattc_if() & 0xFF));
    uint8_t dummy = 0;
    BTA_GATTC_WriteCharValue(bta_conn_id, char_handle, 2 /* GATT_WRITE */, 0,
                             &dummy, 0);
  }
#endif
}

bool BdrBleComponent::send_direct_channel_(const uint8_t *payload, size_t len) {
  if (this->parent() == nullptr) {
    return false;
  }
  uint8_t pkt[1 + DIRECT_TX_MAX_CHUNK_DATA];
  size_t pkt_len = 0;
  if (!this->transport_.begin_tx(payload, len, pkt, pkt_len)) {
    ESP_LOGW(TAG,
             "DirectChannel TX rejected (tx_in_progress=%d, len=%zu)",
             static_cast<int>(this->transport_.is_tx_in_progress()), len);
    return false;
  }

  const uint32_t now = millis();
  this->last_action_ms_ = now;
  if (this->active_req_.kind != RequestKind::NONE) {
    this->active_req_.last_activity_ms = now;
  }

  esp_err_t err = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
      this->h_direct_tx_, static_cast<uint16_t>(pkt_len), pkt,
      ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "DirectChannel write_char failed: %d", err);
    this->transport_.reset_tx();
    return false;
  }
  return true;
}

void BdrBleComponent::send_next_tx_fragment_() {
  if (this->parent() == nullptr) {
    this->transport_.reset_tx();
    return;
  }
  uint8_t pkt[1 + DIRECT_TX_MAX_CHUNK_DATA];
  size_t pkt_len = 0;
  if (!this->transport_.next_tx_fragment(pkt, pkt_len)) {
    return;
  }

  const uint32_t now = millis();
  this->last_action_ms_ = now;
  if (this->active_req_.kind != RequestKind::NONE) {
    this->active_req_.last_activity_ms = now;
  }

  esp_err_t err = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
      this->h_direct_tx_, static_cast<uint16_t>(pkt_len), pkt,
      ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "DirectChannel fragment write_char failed: %d", err);
    this->transport_.reset_tx();
  }
}

bool BdrBleComponent::send_read_param_(uint8_t node_id, uint16_t index,
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
  return this->send_direct_channel_(req, sizeof(req));
}

bool BdrBleComponent::send_write_param_(uint8_t node_id, uint16_t index,
                                        uint8_t sub_index, const uint8_t *data,
                                        size_t data_len) {
  if (data == nullptr || data_len == 0 || data_len > 255) {
    return false;
  }
  uint8_t req[10 + 255];
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
  return this->send_direct_channel_(req, 10 + data_len);
}

bool BdrBleComponent::send_get_list_batch_(const PollGroup &grp,
                                           const size_t *item_indices,
                                           size_t count, bool use_v200_header) {
  if (item_indices == nullptr || count == 0 || count > GET_LIST_MAX_ITEMS) {
    return false;
  }
  uint8_t req[7 + GET_LIST_MAX_ITEMS * 5];
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
  return this->send_direct_channel_(req, 7 + count * 5);
}

bool BdrBleComponent::send_node_channel_upgrade_(uint8_t node_id) {
  const uint8_t req[8] = {
      0x02,
      0x00,
      0x00,
      MSG_TYPE_RUB_AUTH,
      RUB_FUNC_CHANNEL_UPGRADE,
      0x00,
      node_id,
      this->auth_level_,
  };
  ESP_LOGI(TAG, "Sending CAN-IP channel authorization upgrade for node 0x%02X (level=%u)",
           node_id, this->auth_level_);
  return this->send_direct_channel_(req, sizeof(req));
}

bool BdrBleComponent::send_rub_keepalive_() {
  const uint8_t req[7] = {
      0x02,
      0x00,
      0x00,
      MSG_TYPE_RUB_AUTH,
      RUB_FUNC_KEEPALIVE,
      0x00,
      0xFF,
  };
  ESP_LOGV(TAG, "Sending CAN-IP RUB session keep-alive (0x05)");
  return this->send_direct_channel_(req, sizeof(req));
}

void BdrBleComponent::begin_subscribing_() {
  this->publish_status_("Subscribing to GATT notifications");
  this->state_ = STATE_SUBSCRIBING;
  this->cccd_step_ = 0;
  this->cccd_substate_ = CCCD_IDLE;
  this->cccd_manual_write_sent_ = false;
  this->advance_subscribing_();
}

void BdrBleComponent::advance_subscribing_() {
  if (this->parent() == nullptr) {
    return;
  }
  const uint16_t char_handles[5] = {
      this->h_svc3_chal_, this->h_svc3_ack_, this->h_direct_rx_,
      this->h_svc2_ch1_, this->h_svc2_ch2_};
  const uint16_t cccd_handles[5] = {
      this->h_svc3_chal_cccd_, this->h_svc3_ack_cccd_,
      this->h_direct_rx_cccd_, this->h_svc2_ch1_cccd_,
      this->h_svc2_ch2_cccd_};

  while (this->cccd_step_ < 5 &&
         (char_handles[this->cccd_step_] == 0 ||
          cccd_handles[this->cccd_step_] == 0)) {
    this->cccd_step_++;
  }

  if (this->cccd_step_ < 5) {
    this->cccd_substate_ = CCCD_WAIT_REG_NOTIFY;
    this->cccd_manual_write_sent_ = false;
    this->last_action_ms_ = millis();
    esp_err_t err =
        this->parent()->register_for_notify(char_handles[this->cccd_step_]);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "register_for_notify(handle=%u) failed: %d; writing CCCD directly",
               char_handles[this->cccd_step_], err);
      this->cccd_substate_ = CCCD_WAIT_WRITE_DESCR;
      this->cccd_manual_write_sent_ = true;
      this->write_cccd_(cccd_handles[this->cccd_step_]);
    }
    return;
  }

  // CRITICAL-03: All notify registrations and CCCD descriptor writes are complete!
  // Now mark the node ESTABLISHED so esp32_ble_client releases GATT services cleanly.
  this->cccd_substate_ = CCCD_IDLE;
  this->node_state = esphome::esp32_ble_tracker::ClientState::ESTABLISHED;
  this->start_unlock_sequence_();
}

void BdrBleComponent::start_unlock_sequence_() {
  this->reset_active_transaction_();
  this->retry_count_ = 0;
  this->last_action_ms_ = millis();

  // HIGH-09: Skip Service 3 unlock if the gateway does not expose Service 3 (e.g. GTW-IoT)
  if (this->h_svc3_trig_ == 0 || this->h_svc3_chal_ == 0 ||
      this->h_svc3_resp_ == 0 || this->h_svc3_ack_ == 0) {
    ESP_LOGI(TAG, "Gateway has no Service 3 characteristics; proceeding directly to RUB v2.01 auth");
    this->start_rub_auth_();
    return;
  }

  this->publish_status_("Unlocking BLE (Service 3)");
  this->state_ = STATE_SVC3_WAIT_CHALLENGE;
  this->send_zero_length_write_(this->h_svc3_trig_);
}

void BdrBleComponent::start_rub_auth_(const char *status_msg) {
  this->reset_active_transaction_();
  this->upgraded_nodes_mask_ = 0;
  this->pending_upgrade_nodes_mask_ = 0;
  this->publish_status_(status_msg);
  this->state_ = STATE_RUB_WAIT_CHALLENGE;
  this->retry_count_ = 0;
  this->last_action_ms_ = millis();
  const uint8_t rub_chal_req[6] = {
      0x02, 0x01, 0x00, MSG_TYPE_RUB_AUTH, RUB_FUNC_CHALLENGE_REQ, 0x03};
  this->send_direct_channel_(rub_chal_req, sizeof(rub_chal_req));
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
        this->disconnected_since_ms_ = 0;
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
        this->h_svc3_trig_ = this->has_pin_ ? HANDLE_SVC3_TRIG : 0;
        this->h_svc3_chal_ = this->has_pin_ ? HANDLE_SVC3_CHAL : 0;
        this->h_svc3_chal_cccd_ = this->has_pin_ ? HANDLE_SVC3_CHAL_CCCD : 0;
        this->h_svc3_resp_ = this->has_pin_ ? HANDLE_SVC3_RESP : 0;
        this->h_svc3_ack_ = this->has_pin_ ? HANDLE_SVC3_ACK : 0;
        this->h_svc3_ack_cccd_ = this->has_pin_ ? HANDLE_SVC3_ACK_CCCD : 0;
        this->get_list_stage_ = 0;
        this->get_list_use_v200_ = true;
        this->negotiated_mtu_ = 23;
        this->reset_session_state_(false);
        this->registry_.on_connect();
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

    case ESP_GATTC_DISCONNECT_EVT: {
      const uint32_t now = millis();
      this->state_ = STATE_DISCONNECTED;
      this->gatt_search_completed_ = false;
      this->ble_sec_ready_ = false;
      this->disconnected_since_ms_ = (now == 0) ? 1 : now;
      this->reset_session_state_(true);
      if (!this->pairing_backoff_active_) {
        this->publish_status_("Disconnected");
      }
      break;
    }

    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == this->h_direct_tx_) {
        if (param->write.status != ESP_GATT_OK) {
          ESP_LOGW(TAG, "DirectChannel GATT write failed: status=0x%02X",
                   param->write.status);
          this->transport_.reset_tx();
        } else if (this->transport_.is_tx_in_progress()) {
          this->send_next_tx_fragment_();
        }
      }
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT:
      // Resolve handles while GATT DB is active, but hold ClientState::CONNECTED
      // until encrypted notify registrations finish (CRITICAL-03).
      this->resolve_gatt_handles_();
      this->node_state = esphome::esp32_ble_tracker::ClientState::CONNECTED;
      this->gatt_search_completed_ = true;
      this->last_action_ms_ = millis();
      if (this->ble_sec_ready_ && this->state_ == STATE_CONNECTED_WAIT_SEC) {
        this->begin_subscribing_();
      } else {
        this->publish_status_("GATT ready (waiting for encryption)");
      }
      break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (this->state_ == STATE_SUBSCRIBING &&
          this->cccd_substate_ == CCCD_WAIT_REG_NOTIFY) {
        this->cccd_substate_ = CCCD_WAIT_WRITE_DESCR;
        this->last_action_ms_ = millis();
      }
      break;

    case ESP_GATTC_WRITE_DESCR_EVT:
      if (this->state_ == STATE_SUBSCRIBING &&
          (this->cccd_substate_ == CCCD_WAIT_WRITE_DESCR ||
           this->cccd_substate_ == CCCD_WAIT_REG_NOTIFY)) {
        if (param->write.status != ESP_GATT_OK) {
          ESP_LOGW(TAG, "CCCD descriptor write returned status=0x%02X on handle %u",
                   param->write.status, param->write.handle);
        }
        this->cccd_step_++;
        this->advance_subscribing_();
      }
      break;

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
        this->begin_subscribing_();
      }
    } else {
      this->handle_pairing_failure_(param->ble_security.auth_cmpl.bd_addr,
                                    param->ble_security.auth_cmpl.fail_reason);
    }
  }
}

void BdrBleComponent::handle_notify_(uint16_t handle, const uint8_t *data,
                                     uint16_t len) {
  if (this->h_svc3_chal_ != 0 && handle == this->h_svc3_chal_ && len >= 8) {
    if (this->state_ != STATE_SVC3_WAIT_CHALLENGE &&
        this->state_ != STATE_SVC3_WAIT_RESULT) {
      return;
    }
    this->log_rx_hex_("SVC3[CHAL]", data, len, true);
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
                             this->parent()->get_conn_id(), this->h_svc3_resp_,
                             sizeof(resp), resp, ESP_GATT_WRITE_TYPE_RSP,
                             ESP_GATT_AUTH_REQ_NONE);
    this->state_ = STATE_SVC3_WAIT_RESULT;
    this->last_action_ms_ = millis();
    return;
  }

  if (this->h_svc3_ack_ != 0 && handle == this->h_svc3_ack_ && len >= 1) {
    if (this->state_ != STATE_SVC3_WAIT_RESULT &&
        this->state_ != STATE_SVC3_WAIT_CHALLENGE) {
      return;
    }
    this->log_rx_hex_("SVC3[ACK]", data, len, true);
    if (data[0] == 0x01) {
      this->start_rub_auth_("BLE Unlocked -> RUB Auth");
    } else {
      this->publish_status_("Service 3 Auth Rejected");
      this->state_ = STATE_AUTH_FAILED;
      this->last_action_ms_ = millis();
    }
    return;
  }

  // HIGH-06: BDR Error Management characteristic (5b5dcf8d)
  if (this->h_svc2_ch2_ != 0 && handle == this->h_svc2_ch2_ && len >= 4) {
    ESP_LOGW(TAG, "BLE Error Management notify: [%u, %u, %u, %u]", data[0],
             data[1], data[2], data[3]);
    // [3, 0, 0, 4] = "BLE services locked" -> reset active transport/request and re-unlock
    if (data[0] == 3 && data[1] == 0 && data[2] == 0 && data[3] == 4) {
      this->publish_status_("BLE Services Locked -> Re-unlocking");
      this->start_unlock_sequence_();
      return;
    }
    // [1, 0, 1, 3] = RUB timeout, [2, 0, 1, 3] = RUB error frame, [0, 0, 3, 2] = BLE segment/CRC error
    if ((data[0] == 1 && data[1] == 0 && data[2] == 1 && data[3] == 3) ||
        (data[0] == 2 && data[1] == 0 && data[2] == 1 && data[3] == 3) ||
        (data[0] == 0 && data[1] == 0 && data[2] == 3 && data[3] == 2)) {
      ESP_LOGW(TAG, "Aborting in-flight request due to gateway transport/RUB error notify");
      this->reset_active_transaction_();
      this->cooldown_until_ms_ = millis() + POST_TIMEOUT_COOLDOWN_MS;
    }
    return;
  }

  if (handle == this->h_direct_rx_ && len >= 2) {
    this->handle_direct_segment_(data, len);
  }
}

void BdrBleComponent::handle_direct_segment_(const uint8_t *data,
                                             uint16_t len) {
  const uint32_t now = millis();
  const uint8_t *payload = nullptr;
  size_t payload_len = 0;
  uint16_t rx_crc = 0, calc_crc = 0;
  bool discarded_stale = false;

  RxSegmentStatus st = this->transport_.feed_rx_segment(
      data, len, now, payload, payload_len, rx_crc, calc_crc, discarded_stale);
  if (discarded_stale) {
    ESP_LOGW(TAG, "Discarded stale partial RX reassembly buffer");
  }

  switch (st) {
    case RxSegmentStatus::INCOMPLETE:
      this->last_action_ms_ = now;
      if (this->active_req_.kind != RequestKind::NONE) {
        this->active_req_.last_activity_ms = now;
      }
      break;

    case RxSegmentStatus::FRAME_READY:
      this->last_action_ms_ = now;
      if (this->active_req_.kind != RequestKind::NONE) {
        this->active_req_.last_activity_ms = now;
      }
      this->handle_direct_payload_(payload, payload_len);
      break;

    case RxSegmentStatus::ERROR_OUT_OF_ORDER:
      ESP_LOGW(TAG, "Out-of-order or oversized DirectChannel RX segment 0x%02X; resetting RX buffer",
               data[0]);
      break;

    case RxSegmentStatus::ERROR_CRC:
      ESP_LOGW(TAG, "DirectChannel CRC/channel mismatch (rx=0x%04X calc=0x%04X)",
               rx_crc, calc_crc);
      break;

    case RxSegmentStatus::ERROR_TOO_SHORT:
      break;
  }
}

void BdrBleComponent::handle_direct_payload_(const uint8_t *payload,
                                             size_t len) {
  if (payload == nullptr || len < 6) {
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
    this->log_rx_hex_("RUB_CHAL", payload, static_cast<uint16_t>(len), true);
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

  // RUB v2.01 Auth Ack (also used by CAN-IP v2.0 as ACK for Node Channel Upgrade 0x04 & Keep-Alive 0x05):
  // 02 xx 00 02 03 xx <node_or_FF> <level> ...
  if (msg_type == MSG_TYPE_RUB_AUTH && func == RUB_FUNC_AUTH_ACK && len >= 8) {
    if (this->state_ == STATE_READY &&
        (this->active_req_.kind == RequestKind::NODE_UPGRADE ||
         this->active_req_.kind == RequestKind::KEEPALIVE)) {
      if (this->active_req_.kind == RequestKind::NODE_UPGRADE) {
        ESP_LOGI(TAG,
                 "CAN-IP channel upgrade ACK for node 0x%02X (granted level=%u)",
                 this->active_req_.node_id, payload[7]);
      }
      this->active_req_ = {};
      this->consecutive_timeouts_ = 0;
      return;
    }
    if (this->state_ != STATE_RUB_WAIT_AUTH_ACK &&
        this->state_ != STATE_RUB_WAIT_CHALLENGE) {
      return;
    }
    this->log_rx_hex_("RUB_ACK", payload, static_cast<uint16_t>(len), true);
    uint8_t granted = payload[7];
    if (granted > 0) {
      this->reset_active_transaction_();
      this->consecutive_timeouts_ = 0;
      this->mark_node_upgraded_(this->default_node_id_);
      // HIGH-05: Proactively schedule channel upgrades for any configured secondary CAN nodes
      if (this->auth_level_ >= 3) {
        this->pending_upgrade_nodes_mask_ =
            this->registry_.get_configured_nodes_mask() &
            ~(1UL << this->default_node_id_);
      }
      this->stale_published_ = false;
      this->last_action_ms_ = millis();

      // If we already read the 0x502B snapshot on this connection, resume STATE_READY directly
      if (this->snapshot_sub_idx_ > SNAPSHOT_MAX_SUB_INDEX) {
        this->publish_status_("Ready");
        this->state_ = STATE_READY;
        return;
      }

      char msg[64];
      snprintf(msg, sizeof(msg), "Connected (auth level %u, reading snapshot)",
               granted);
      this->publish_status_(msg);
      this->state_ = STATE_READING_SNAPSHOT;
      this->snapshot_sub_idx_ = 1;
    } else {
      this->publish_status_("CAN-BUS Auth Rejected");
      this->state_ = STATE_AUTH_FAILED;
      this->last_action_ms_ = millis();
    }
    return;
  }

  // Per-Node Channel Upgrade ACK / Keep-Alive ACK
  if (msg_type == MSG_TYPE_RUB_AUTH &&
      (func == RUB_FUNC_CHANNEL_UPGRADE || func == RUB_FUNC_KEEPALIVE)) {
    if (this->active_req_.kind == RequestKind::NODE_UPGRADE ||
        this->active_req_.kind == RequestKind::KEEPALIVE) {
      this->active_req_ = {};
      this->consecutive_timeouts_ = 0;
    }
    return;
  }

  // Single Read Positive Ack: 02 01 00 00 01 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx> <Data...>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_READ_POS_ACK && len >= 10) {
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    bool was_keepalive = false;
    if ((this->active_req_.kind == RequestKind::READ ||
         this->active_req_.kind == RequestKind::SNAPSHOT ||
         this->active_req_.kind == RequestKind::KEEPALIVE) &&
        this->active_req_.node_id == node_id &&
        this->active_req_.index == index &&
        this->active_req_.sub_index == sub_index) {
      was_keepalive = (this->active_req_.kind == RequestKind::KEEPALIVE);
      this->active_req_ = {};
      this->consecutive_timeouts_ = 0;
    }
    this->stale_published_ = false;
    if (!was_keepalive) {
      this->handle_single_read_ack_(node_id, index, sub_index, payload + 10,
                                    len - 10);
    }
    return;
  }

  // Single Read Negative Ack (SDO Abort): 02 01 00 00 02 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx> <AbortCode(4)>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_READ_NEG_ACK && len >= 10) {
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    bool matched_req = false;
    bool was_keepalive = false;
    if ((this->active_req_.kind == RequestKind::READ ||
         this->active_req_.kind == RequestKind::SNAPSHOT ||
         this->active_req_.kind == RequestKind::KEEPALIVE) &&
        this->active_req_.node_id == node_id &&
        this->active_req_.index == index &&
        this->active_req_.sub_index == sub_index) {
      matched_req = true;
      was_keepalive = (this->active_req_.kind == RequestKind::KEEPALIVE);
      this->active_req_ = {};
      this->consecutive_timeouts_ = 0;
    }
    if (!matched_req || was_keepalive) {
      return;
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
      return;
    }

    SdoAbortCategory cat = classify_sdo_abort(abort_code);
    if (cat == SdoAbortCategory::AUTH_OR_ACCESS_DENIED) {
      this->handle_access_denied_(node_id, index, sub_index, FUNC_READ_NEG_ACK,
                                  abort_code, false);
    } else if (abort_code == 0 ||
               cat == SdoAbortCategory::PERMANENT_UNSUPPORTED ||
               cat == SdoAbortCategory::GET_LIST_INCOMPATIBLE) {
      this->registry_.mark_datapoint_unsupported(
          node_id, index, sub_index, FUNC_READ_NEG_ACK, abort_code);
    } else {
      ESP_LOGW(TAG, "Transient SDO read abort on 0x%04X:%02X: 0x%08X (%s)",
               index, sub_index, static_cast<unsigned>(abort_code),
               sdo_abort_code_str(abort_code));
    }
    return;
  }

  // Write Confirmed Positive Ack: 02 01 00 00 05 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_WRITE_POS_ACK && len >= 10) {
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    if (this->active_req_.kind != RequestKind::WRITE ||
        this->active_req_.node_id != node_id ||
        this->active_req_.index != index ||
        this->active_req_.sub_index != sub_index) {
      return;
    }
    this->active_req_ = {};
    this->consecutive_timeouts_ = 0;
    ESP_LOGI(TAG, "Write ACK for 0x%04X:%02X; reading back value", index,
             sub_index);
    const uint32_t now = millis();
    if (this->send_read_param_(node_id, index, sub_index)) {
      this->active_req_.kind = RequestKind::READ;
      this->active_req_.node_id = node_id;
      this->active_req_.index = index;
      this->active_req_.sub_index = sub_index;
      this->active_req_.started_ms = now;
      this->active_req_.last_activity_ms = now;
    }
    return;
  }

  // Write Confirmed Negative Ack (SDO Abort): 02 01 00 00 06 03 <NodeId> <Idx_HI> <Idx_LO> <SubIdx> <AbortCode(4)>
  if (msg_type == MSG_TYPE_SDO && func == FUNC_WRITE_NEG_ACK && len >= 10) {
    uint8_t node_id = payload[6];
    uint16_t index =
        (static_cast<uint16_t>(payload[7]) << 8) | payload[8];
    uint8_t sub_index = payload[9];
    if (this->active_req_.kind != RequestKind::WRITE ||
        this->active_req_.node_id != node_id ||
        this->active_req_.index != index ||
        this->active_req_.sub_index != sub_index) {
      return;
    }
    uint32_t abort_code = 0;
    if (len >= 14) {
      abort_code = static_cast<uint32_t>(payload[10]) |
                   (static_cast<uint32_t>(payload[11]) << 8) |
                   (static_cast<uint32_t>(payload[12]) << 16) |
                   (static_cast<uint32_t>(payload[13]) << 24);
    }
    this->active_req_ = {};
    this->consecutive_timeouts_ = 0;
    ESP_LOGW(TAG,
             "Write NACK for 0x%04X:%02X (SDO abort=0x%08X: %s); reading back hardware state",
             index, sub_index, static_cast<unsigned>(abort_code),
             sdo_abort_code_str(abort_code));
    const uint32_t now = millis();
    if (this->send_read_param_(node_id, index, sub_index)) {
      this->active_req_.kind = RequestKind::READ;
      this->active_req_.node_id = node_id;
      this->active_req_.index = index;
      this->active_req_.sub_index = sub_index;
      this->active_req_.started_ms = now;
      this->active_req_.last_activity_ms = now;
    }
    return;
  }

  // CRITICAL-01: Correlate Batch GetListResponse (02 xx 00 00 09 xx <Count> <Items...>) against active_req_
  if (msg_type == MSG_TYPE_SDO && func == FUNC_GET_LIST_RESP && len >= 7) {
    uint8_t count = payload[6];
    uint8_t first_node = (len >= 15) ? payload[8] : 0;
    uint16_t first_idx =
        (len >= 15)
            ? ((static_cast<uint16_t>(payload[9]) << 8) | payload[10])
            : 0;
    uint8_t first_sub = (len >= 15) ? payload[11] : 0;

    bool matches_active =
        (this->active_req_.kind == RequestKind::GET_LIST) &&
        (count == this->active_req_.expected_list_count) &&
        (count == 0 ||
         (len >= 15 && first_node == this->active_req_.first_list_node &&
          first_idx == this->active_req_.first_list_index &&
          first_sub == this->active_req_.first_list_sub));

    if (!matches_active) {
      ESP_LOGW(TAG,
               "Discarding stale/unmatched GetListResponse (count=%u, first=0x%04X:%02X)",
               count, first_idx, first_sub);
      return;
    }

    this->active_req_ = {};
    this->consecutive_timeouts_ = 0;
    this->get_list_stage_ = 2;  // Confirmed working!
    this->stale_published_ = false;
    this->handle_get_list_response_(payload, len);
  }
}

void BdrBleComponent::handle_single_read_ack_(uint8_t node_id, uint16_t index,
                                              uint8_t sub_index,
                                              const uint8_t *data,
                                              size_t data_len) {
  if (index == INDEX_AP_MENU_DESCRIPTOR) {
    this->registry_.handle_snapshot_descriptor(node_id, sub_index, data,
                                               data_len);
    if (this->state_ == STATE_READING_SNAPSHOT &&
        sub_index == this->snapshot_sub_idx_) {
      this->snapshot_sub_idx_++;
    }
    return;
  }
  char tag[24];
  snprintf(tag, sizeof(tag), "0x%04X:%02X", index, sub_index);
  this->log_rx_hex_(tag, data, static_cast<uint16_t>(data_len), false);
  this->registry_.dispatch_datapoint_value(node_id, index, sub_index, data,
                                           data_len);
}

void BdrBleComponent::handle_access_denied_(uint8_t node_id, uint16_t index,
                                            uint8_t sub_index,
                                            uint8_t status_code,
                                            uint32_t abort_code,
                                            bool from_get_list) {
  // HIGH-05: 1) Check if a secondary CAN node channel needs authorization upgrade (FUNC 0x04)
  if (node_id != this->default_node_id_ && !this->is_node_upgraded_(node_id) &&
      this->auth_level_ >= 3) {
    this->queue_node_upgrade_(node_id);
    this->registry_.trigger_immediate_repoll_for_node(node_id);
    this->active_group_idx_ = -1;
    ESP_LOGI(TAG,
             "Node 0x%02X returned Unsupported Access (0x06010000) on 0x%04X:%02X; "
             "queuing CAN channel upgrade and immediate re-poll",
             node_id, index, sub_index);
    return;
  }

  // HIGH-05: 2) If this datapoint previously succeeded or is listed in 0x502B at <= auth_level_,
  // the boiler's 10-second RUB auth session expired! Re-authenticate RUB instead of disabling it.
  const auto *dp = this->registry_.find_datapoint(node_id, index, sub_index);
  bool known_accessible =
      (dp != nullptr) &&
      (dp->ever_succeeded ||
       (dp->discovered_level > 1 && dp->discovered_level <= this->auth_level_));
  if (known_accessible && this->state_ == STATE_READY) {
    ESP_LOGW(TAG,
             "Datapoint 0x%04X:%02X returned Unsupported Access (0x06010000) after previously succeeding; "
             "RUB session expired -> re-authenticating",
             index, sub_index);
    this->registry_.trigger_immediate_repoll_for_node(node_id);
    this->start_rub_auth_("Re-authenticating CAN-BUS");
    return;
  }

  if (from_get_list) {
    this->registry_.mark_datapoint_single_read_only(
        node_id, index, sub_index, status_code, abort_code);
  } else {
    this->registry_.mark_datapoint_unsupported(
        node_id, index, sub_index, status_code, abort_code);
  }
}

void BdrBleComponent::handle_get_list_response_(const uint8_t *payload,
                                                size_t len) {
  uint8_t count = payload[6];
  char tag[24];
  snprintf(tag, sizeof(tag), "LIST[%u]", count);
  this->log_rx_hex_(
      tag, payload, static_cast<uint16_t>((len > 32) ? 32 : len), false);
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

    if (status == 0x01) {
      if (offset + data_len > len) {
        break;
      }
      this->registry_.dispatch_datapoint_value(node_id, index, sub_index,
                                               payload + offset, data_len);
      offset += data_len;
      continue;
    }

    // CRITICAL-01: On status != 0x01 (e.g. NegAck 0x02), CAN-IP v2.0 specifies a 4-byte LE SDO abort code,
    // though some gateway firmware revisions set lenHi:lenLo to 0 or the nominal object size.
    // Determine the actual byte stride by checking whether 4 abort bytes align with packet end / next item.
    size_t step_len = data_len;
    bool has_abort_bytes = (data_len >= 4 && offset + 4 <= len);
    if (data_len != 4 && offset + 4 <= len) {
      if (i + 1 == count && offset + 4 == len) {
        step_len = 4;
        has_abort_bytes = true;
      } else if (i + 1 < count && offset + 4 + 8 <= len) {
        const uint8_t *next4 = payload + offset + 4;
        uint16_t next4_idx =
            (static_cast<uint16_t>(next4[2]) << 8) | next4[3];
        bool next4_valid =
            (next4[0] == 0x01 || next4[0] == 0x02) && next4[5] == 0x00 &&
            (this->registry_.find_datapoint(next4[1], next4_idx, next4[4]) !=
             nullptr);
        if (next4_valid) {
          step_len = 4;
          has_abort_bytes = true;
        }
      }
    }
    if (offset + step_len > len) {
      break;
    }

    uint32_t abort_code = 0;
    if (has_abort_bytes) {
      abort_code = static_cast<uint32_t>(payload[offset + 0]) |
                   (static_cast<uint32_t>(payload[offset + 1]) << 8) |
                   (static_cast<uint32_t>(payload[offset + 2]) << 16) |
                   (static_cast<uint32_t>(payload[offset + 3]) << 24);
    }

    if (abort_code == 0) {
      this->registry_.mark_datapoint_single_read_only(
          node_id, index, sub_index, status, abort_code);
    } else {
      SdoAbortCategory cat = classify_sdo_abort(abort_code);
      switch (cat) {
        case SdoAbortCategory::PERMANENT_UNSUPPORTED:
          this->registry_.mark_datapoint_unsupported(
              node_id, index, sub_index, status, abort_code);
          break;

        case SdoAbortCategory::AUTH_OR_ACCESS_DENIED:
          this->handle_access_denied_(node_id, index, sub_index, status,
                                      abort_code, true);
          break;

        case SdoAbortCategory::GET_LIST_INCOMPATIBLE:
          this->registry_.mark_datapoint_single_read_only(
              node_id, index, sub_index, status, abort_code);
          break;

        case SdoAbortCategory::TRANSIENT_ERROR:
          ESP_LOGW(TAG,
                   "Transient SDO abort in GetList for 0x%04X:%02X (status=0x%02X, abort=0x%08X: %s); "
                   "keeping in batch for next cycle",
                   index, sub_index, status,
                   static_cast<unsigned>(abort_code),
                   sdo_abort_code_str(abort_code));
          break;
      }
    }

    offset += step_len;
    // Guard against misaligned continuation if next header does not look like a valid item header
    if (i + 1 < count && offset + 8 <= len) {
      uint8_t next_status = payload[offset + 0];
      if ((next_status != 0x01 && next_status != 0x02) ||
          payload[offset + 5] != 0x00) {
        ESP_LOGW(TAG,
                 "GetListResponse alignment lost after NegAck on 0x%04X:%02X; stopping batch parse",
                 index, sub_index);
        break;
      }
    }
  }
}

void BdrBleComponent::loop() {
  if (this->parent() == nullptr) {
    return;
  }
  const uint32_t now = millis();

  // LOW-14: Publish NAN to numeric sensors if disconnected for >= 90 seconds
  if (this->state_ == STATE_DISCONNECTED) {
    if (!this->stale_published_ && this->disconnected_since_ms_ != 0 &&
        (now - this->disconnected_since_ms_ >= DISCONNECT_STALE_TIMEOUT_MS)) {
      ESP_LOGW(TAG, "BLE disconnected for >%us; marking numeric sensors unavailable (NAN)",
               static_cast<unsigned>(DISCONNECT_STALE_TIMEOUT_MS / 1000));
      this->registry_.publish_stale_nan();
      this->stale_published_ = true;
    }
    if (this->pairing_backoff_active_) {
      if (static_cast<int32_t>(now - this->pairing_backoff_until_ms_) >= 0) {
        this->pairing_backoff_active_ = false;
        ESP_LOGI(TAG, "BLE pairing backoff elapsed; re-enabling BLE client");
        this->publish_status_("Retrying BLE connection...");
        this->parent()->set_enabled(true);
      }
    }
    return;
  }

  if (this->state_ == STATE_AUTH_FAILED) {
    if (!this->pairing_failed_ &&
        (now - this->last_action_ms_ >= AUTH_COOLDOWN_MS)) {
      ESP_LOGI(TAG, "Auth cooldown elapsed; retrying BLE unlock");
      this->start_unlock_sequence_();
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
    const uint16_t cccd_handles[5] = {
        this->h_svc3_chal_cccd_, this->h_svc3_ack_cccd_,
        this->h_direct_rx_cccd_, this->h_svc2_ch1_cccd_,
        this->h_svc2_ch2_cccd_};
    // If BLEClientBase is in V3 cached mode, it skips writing CCCD on REG_FOR_NOTIFY_EVT;
    // issue the explicit descriptor write after 150ms if WRITE_DESCR_EVT hasn't fired yet.
    if (this->cccd_substate_ == CCCD_WAIT_WRITE_DESCR &&
        !this->cccd_manual_write_sent_ && (now - this->last_action_ms_ >= 150) &&
        this->cccd_step_ < 5) {
      this->cccd_manual_write_sent_ = true;
      this->last_action_ms_ = now;
      this->write_cccd_(cccd_handles[this->cccd_step_]);
      return;
    }
    // Per-step timeout fallback (3000 ms)
    if (now - this->last_action_ms_ >= HANDSHAKE_TIMEOUT_MS) {
      ESP_LOGW(TAG, "CCCD subscription step %u timed out; advancing",
               this->cccd_step_);
      this->cccd_step_++;
      this->advance_subscribing_();
    }
    return;
  }

  if (this->state_ == STATE_SVC3_WAIT_CHALLENGE ||
      this->state_ == STATE_SVC3_WAIT_RESULT) {
    if (now - this->last_action_ms_ > HANDSHAKE_TIMEOUT_MS) {
      this->last_action_ms_ = now;
      if (this->retry_count_ < 3 && this->h_svc3_trig_ != 0) {
        this->retry_count_++;
        this->state_ = STATE_SVC3_WAIT_CHALLENGE;
        this->send_zero_length_write_(this->h_svc3_trig_);
      } else {
        this->start_rub_auth_();
      }
    }
    return;
  }

  if (this->state_ == STATE_RUB_WAIT_CHALLENGE ||
      this->state_ == STATE_RUB_WAIT_AUTH_ACK) {
    if (now - this->last_action_ms_ > HANDSHAKE_TIMEOUT_MS) {
      this->last_action_ms_ = now;
      if (this->retry_count_ < 3 && !this->transport_.is_tx_in_progress()) {
        this->retry_count_++;
        this->state_ = STATE_RUB_WAIT_CHALLENGE;
        const uint8_t rub_chal_req[6] = {
            0x02, 0x01, 0x00, MSG_TYPE_RUB_AUTH, RUB_FUNC_CHALLENGE_REQ, 0x03};
        this->send_direct_channel_(rub_chal_req, sizeof(rub_chal_req));
      } else if (this->retry_count_ >= 3) {
        ESP_LOGW(TAG, "RUB v2.01 authentication timed out after 3 retries");
        this->publish_status_("CAN-BUS Auth Timeout");
        this->state_ = STATE_AUTH_FAILED;
      }
    }
    return;
  }

  if (this->state_ == STATE_READING_SNAPSHOT) {
    if (this->active_req_.kind != RequestKind::NONE &&
        (now - this->active_req_.last_activity_ms >= REQUEST_TIMEOUT_MS)) {
      ESP_LOGD(TAG, "Snapshot 0x502B:%02X timed out; advancing",
               this->snapshot_sub_idx_);
      this->reset_active_transaction_();
      this->snapshot_sub_idx_++;
    }
    if (this->active_req_.kind == RequestKind::NONE &&
        !this->transport_.is_tx_in_progress() &&
        (now - this->last_action_ms_ >= MIN_TX_INTERVAL_MS)) {
      if (this->snapshot_sub_idx_ <= SNAPSHOT_MAX_SUB_INDEX) {
        if (this->send_read_param_(this->default_node_id_,
                                   INDEX_AP_MENU_DESCRIPTOR,
                                   this->snapshot_sub_idx_)) {
          this->active_req_.kind = RequestKind::SNAPSHOT;
          this->active_req_.node_id = this->default_node_id_;
          this->active_req_.index = INDEX_AP_MENU_DESCRIPTOR;
          this->active_req_.sub_index = this->snapshot_sub_idx_;
          this->active_req_.started_ms = now;
          this->active_req_.last_activity_ms = now;
        }
      } else {
        if (this->log_snapshot_ && !this->snapshot_logged_once_) {
          this->registry_.begin_snapshot_logging();
        }
        this->publish_status_("Ready");
        this->state_ = STATE_READY;
        this->active_group_idx_ = -1;
        this->active_req_ = {};
        this->consecutive_timeouts_ = 0;
        this->registry_.reset_poll_timers();
        this->last_action_ms_ = now;
      }
    }
    return;
  }

  if (this->state_ == STATE_READY) {
    // MEDIUM-10: Emit at most 2 BDR_SNAPSHOT lines per loop() tick to keep loop time < 15 ms
    if (this->registry_.is_snapshot_logging_active()) {
      if (this->registry_.log_snapshot_chunk(2)) {
        this->snapshot_logged_once_ = true;
      }
    }

    // Wait for multi-fragment TX to finish before timing out or starting a new request
    if (this->transport_.is_tx_in_progress()) {
      if (now - this->last_action_ms_ >= REQUEST_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Multi-fragment TX stalled; resetting TX state");
        this->transport_.reset_tx();
      } else {
        return;
      }
    }

    const bool channel_busy = this->transport_.is_rx_busy(now);

    // Check if an active request timed out
    if (this->active_req_.kind != RequestKind::NONE && !channel_busy &&
        (now - this->active_req_.last_activity_ms >= REQUEST_TIMEOUT_MS)) {
      ActiveRequest timed_out = this->active_req_;
      this->reset_active_transaction_();
      this->cooldown_until_ms_ = now + POST_TIMEOUT_COOLDOWN_MS;

      if (timed_out.kind == RequestKind::KEEPALIVE &&
          !this->keepalive_use_sdo_fallback_) {
        ESP_LOGD(TAG,
                 "CAN-IP RUB keep-alive (0x05) not ACKed by gateway; falling back to 0x1000:00 SDO keep-alive");
        this->keepalive_use_sdo_fallback_ = true;
        return;
      }

      this->consecutive_timeouts_++;

      if (this->consecutive_timeouts_ >= MAX_CONSECUTIVE_TIMEOUTS) {
        ESP_LOGW(TAG, "%u consecutive request timeouts; re-authenticating RUB session",
                 this->consecutive_timeouts_);
        this->consecutive_timeouts_ = 0;
        this->start_rub_auth_("Re-authenticating CAN-BUS");
        return;
      }

      if (timed_out.kind == RequestKind::WRITE) {
        ESP_LOGW(TAG,
                 "Write confirmation for 0x%04X:%02X timed out; reading back value",
                 timed_out.index, timed_out.sub_index);
        if (this->send_read_param_(timed_out.node_id, timed_out.index,
                                   timed_out.sub_index)) {
          this->active_req_.kind = RequestKind::READ;
          this->active_req_.node_id = timed_out.node_id;
          this->active_req_.index = timed_out.index;
          this->active_req_.sub_index = timed_out.sub_index;
          this->active_req_.started_ms = now;
          this->active_req_.last_activity_ms = now;
        }
        return;
      }

      if (timed_out.kind == RequestKind::GET_LIST &&
          this->active_group_idx_ >= 0 && this->get_list_stage_ < 2) {
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
      return;
    }

    // Respect post-timeout / error-notify quiet cooldown before sending next frame
    if (this->cooldown_until_ms_ != 0 &&
        static_cast<int32_t>(now - this->cooldown_until_ms_) < 0) {
      return;
    }
    this->cooldown_until_ms_ = 0;

    // Priority 0: Execute pending secondary-node CAN channel upgrades (HIGH-05)
    if (this->pending_upgrade_nodes_mask_ != 0 &&
        this->active_req_.kind == RequestKind::NONE && !channel_busy &&
        (now - this->last_action_ms_ >= MIN_TX_INTERVAL_MS)) {
      uint8_t upgrade_node = 0;
      for (uint8_t n = 0; n < 32; n++) {
        if ((this->pending_upgrade_nodes_mask_ & (1UL << n)) != 0) {
          upgrade_node = n;
          this->pending_upgrade_nodes_mask_ &= ~(1UL << n);
          break;
        }
      }
      this->mark_node_upgraded_(upgrade_node);
      if (this->send_node_channel_upgrade_(upgrade_node)) {
        this->active_req_.kind = RequestKind::NODE_UPGRADE;
        this->active_req_.node_id = upgrade_node;
        this->active_req_.started_ms = now;
        this->active_req_.last_activity_ms = now;
      }
      return;
    }

    // Priority 1: Execute any queued parameter writes immediately
    if (!this->write_queue_.empty() &&
        this->active_req_.kind == RequestKind::NONE && !channel_busy &&
        (now - this->last_action_ms_ >= MIN_TX_INTERVAL_MS)) {
      PendingWrite pw = this->write_queue_.front();
      this->write_queue_.pop_front();
      if (this->send_write_param_(pw.node_id, pw.index, pw.sub_index,
                                  pw.data.data(), pw.data.size())) {
        this->active_req_.kind = RequestKind::WRITE;
        this->active_req_.node_id = pw.node_id;
        this->active_req_.index = pw.index;
        this->active_req_.sub_index = pw.sub_index;
        this->active_req_.started_ms = now;
        this->active_req_.last_activity_ms = now;
      }
      return;
    }

    auto &groups = this->registry_.groups();

    // If no group is currently bursting, select the most overdue group
    if (this->active_group_idx_ < 0 && !groups.empty()) {
      int best_group = -1;
      uint32_t max_overdue = 0;
      for (size_t g = 0; g < groups.size(); g++) {
        auto &grp = groups[g];
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
        groups[best_group].last_cycle_start_ms = (now == 0) ? 1 : now;
      }
    }

    // Execute byte-budgeted GetListRequest or sequential read for the active group
    if (this->active_group_idx_ >= 0 &&
        this->active_req_.kind == RequestKind::NONE && !channel_busy &&
        (now - this->last_action_ms_ >= MIN_TX_INTERVAL_MS)) {
      auto &grp = groups[this->active_group_idx_];
      while (this->active_item_idx_ < grp.items.size() &&
             grp.items[this->active_item_idx_].unsupported) {
        this->active_item_idx_++;
      }
      if (this->active_item_idx_ < grp.items.size()) {
        if (this->get_list_stage_ > 2 ||
            grp.items[this->active_item_idx_].single_read_only) {
          const auto &dp = grp.items[this->active_item_idx_++];
          if (this->send_read_param_(dp.node_id, dp.index, dp.sub_index)) {
            this->active_req_.kind = RequestKind::READ;
            this->active_req_.node_id = dp.node_id;
            this->active_req_.index = dp.index;
            this->active_req_.sub_index = dp.sub_index;
            this->active_req_.started_ms = now;
            this->active_req_.last_activity_ms = now;
          }
          return;
        }

        // HIGH-06 & MEDIUM-10: Stack-allocated batch array with 240-byte response budget
        size_t batch_indices[GET_LIST_MAX_ITEMS];
        size_t batch_count = 0;
        size_t estimated_rx_bytes = 7;
        size_t scan_idx = this->active_item_idx_;
        while (scan_idx < grp.items.size() &&
               batch_count < GET_LIST_MAX_ITEMS) {
          const auto &cand = grp.items[scan_idx];
          if (cand.single_read_only) {
            break;
          }
          if (!cand.unsupported) {
            size_t item_wire =
                (cand.expected_len > 0)
                    ? cand.expected_len
                    : ((cand.max_len > 0) ? cand.max_len : 4);
            size_t item_cost = 8 + item_wire;
            if (batch_count > 0 &&
                estimated_rx_bytes + item_cost > GET_LIST_MAX_RESPONSE_BYTES) {
              break;
            }
            batch_indices[batch_count++] = scan_idx;
            estimated_rx_bytes += item_cost;
          }
          scan_idx++;
        }
        this->active_item_idx_ = scan_idx;

        if (batch_count > 1) {
          const auto &first_dp = grp.items[batch_indices[0]];
          if (this->send_get_list_batch_(grp, batch_indices, batch_count,
                                         this->get_list_use_v200_)) {
            this->active_req_.kind = RequestKind::GET_LIST;
            this->active_req_.expected_list_count =
                static_cast<uint8_t>(batch_count);
            this->active_req_.first_list_node = first_dp.node_id;
            this->active_req_.first_list_index = first_dp.index;
            this->active_req_.first_list_sub = first_dp.sub_index;
            this->active_req_.started_ms = now;
            this->active_req_.last_activity_ms = now;
          }
          return;
        } else if (batch_count == 1) {
          const auto &dp = grp.items[batch_indices[0]];
          if (this->send_read_param_(dp.node_id, dp.index, dp.sub_index)) {
            this->active_req_.kind = RequestKind::READ;
            this->active_req_.node_id = dp.node_id;
            this->active_req_.index = dp.index;
            this->active_req_.sub_index = dp.sub_index;
            this->active_req_.started_ms = now;
            this->active_req_.last_activity_ms = now;
          }
          return;
        }
      } else {
        this->active_group_idx_ = -1;
      }
    }

    // Keep the boiler's 10-second RUB v2.01 CAN session timer alive when idle
    if (this->active_group_idx_ < 0 &&
        this->active_req_.kind == RequestKind::NONE && !channel_busy &&
        (now - this->last_action_ms_ >= KEEPALIVE_INTERVAL_MS)) {
      if (!this->keepalive_use_sdo_fallback_) {
        if (this->send_rub_keepalive_()) {
          this->active_req_.kind = RequestKind::KEEPALIVE;
          this->active_req_.node_id = 0xFF;
          this->active_req_.started_ms = now;
          this->active_req_.last_activity_ms = now;
        }
      } else {
        ESP_LOGV(TAG, "Sending RUB session keep-alive fallback (0x1000:00)");
        if (this->send_read_param_(this->default_node_id_,
                                   INDEX_CAN_DEVICE_TYPE, 0x00)) {
          this->active_req_.kind = RequestKind::KEEPALIVE;
          this->active_req_.node_id = this->default_node_id_;
          this->active_req_.index = INDEX_CAN_DEVICE_TYPE;
          this->active_req_.sub_index = 0x00;
          this->active_req_.started_ms = now;
          this->active_req_.last_activity_ms = now;
        }
      }
    }
  }
}

}  // namespace esphome::bdr_ble
