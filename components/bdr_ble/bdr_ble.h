#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_TEXT
#include "esphome/components/text/text.h"
#endif

#include <esp_gattc_api.h>
#include <esp_gap_ble_api.h>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "bdr_protocol.h"
#include "bdr_registry.h"
#include "bdr_transport.h"

namespace esphome::bdr_ble {

struct PendingWrite {
  uint8_t node_id{0x01};
  uint16_t index{0};
  uint8_t sub_index{0};
  std::vector<uint8_t> data;
};

struct PendingReadback {
  bool pending{false};
  uint8_t node_id{0x01};
  uint16_t index{0};
  uint8_t sub_index{0};
};

enum class RequestKind : uint8_t {
  NONE = 0,
  SNAPSHOT,
  READ,
  WRITE,
  GET_LIST,
};

struct ActiveRequest {
  RequestKind kind{RequestKind::NONE};
  uint8_t node_id{0};
  uint16_t index{0};
  uint8_t sub_index{0};
  uint8_t expected_list_count{0};
  uint8_t first_list_node{0};
  uint16_t first_list_index{0};
  uint8_t first_list_sub{0};
  uint32_t started_ms{0};
  uint32_t last_activity_ms{0};
};

class BdrBleComponent : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void set_default_node_id(uint8_t node_id) { this->default_node_id_ = node_id; }
  uint8_t get_default_node_id() const { return this->default_node_id_; }
  void set_auth_level(uint8_t auth_level) { this->auth_level_ = auth_level; }
  void set_log_snapshot(bool log_snapshot) { this->log_snapshot_ = log_snapshot; }
  void add_snapshot_node(uint8_t node_id) {
    for (uint8_t existing : this->configured_snapshot_nodes_) {
      if (existing == node_id) {
        return;
      }
    }
    this->configured_snapshot_nodes_.push_back(node_id);
  }
  void set_pin(uint32_t pin) {
    this->pin_ = pin;
    this->has_pin_ = true;
  }

#ifdef USE_SENSOR
  void register_sensor(sensor::Sensor *sens, uint16_t index, uint8_t sub_index,
                       uint8_t data_type, float scale, uint32_t interval_ms,
                       uint8_t node_id = 0xFF);
#endif

#ifdef USE_BINARY_SENSOR
  void register_binary_sensor(binary_sensor::BinarySensor *bsens, uint16_t index,
                              uint8_t sub_index, uint8_t data_type,
                              uint32_t interval_ms, uint8_t node_id = 0xFF);
#endif

#ifdef USE_TEXT_SENSOR
  void set_status_text_sensor(text_sensor::TextSensor *sens) { this->status_text_ = sens; }
  void register_text_sensor(text_sensor::TextSensor *sens, uint16_t index, uint8_t sub_index,
                            uint8_t data_type, uint32_t interval_ms,
                            uint8_t node_id = 0xFF);
#endif

#if defined(USE_TEXT_SENSOR) || defined(USE_BDR_BLE_SELECT)
  void register_enum_table(uint16_t index, uint8_t sub_index,
                           const EnumEntry *table, size_t len,
                           uint8_t node_id = 0xFF);
#endif

#ifdef USE_BDR_BLE_NUMBER
  void register_number(BdrBleNumber *num, uint16_t index, uint8_t sub_index,
                       uint8_t data_type, float scale, uint32_t interval_ms,
                       uint8_t node_id = 0xFF);
#endif

#ifdef USE_BDR_BLE_SELECT
  void register_select(BdrBleSelect *sel, uint16_t index, uint8_t sub_index,
                       uint32_t interval_ms, uint8_t node_id = 0xFF);
#endif

#ifdef USE_TEXT
  void register_text(text::Text *txt, uint16_t index, uint8_t sub_index,
                     uint32_t interval_ms, uint8_t node_id = 0xFF);
#endif

  bool queue_write_datapoint(uint8_t node_id, uint16_t index, uint8_t sub_index,
                             const std::vector<uint8_t> &data);

  uint8_t get_datapoint_wire_size(uint8_t node_id, uint16_t index,
                                  uint8_t sub_index) const {
    uint8_t resolved_node =
        (node_id == 0xFF) ? this->default_node_id_ : node_id;
    return this->registry_.get_datapoint_wire_size(resolved_node, index,
                                                   sub_index);
  }

  bool check_datapoint_bounds(uint8_t node_id, uint16_t index,
                              uint8_t sub_index, uint32_t raw_val,
                              uint32_t &out_min, uint32_t &out_max) const {
    uint8_t resolved_node =
        (node_id == 0xFF) ? this->default_node_id_ : node_id;
    return this->registry_.check_datapoint_bounds(resolved_node, index,
                                                  sub_index, raw_val, out_min,
                                                  out_max);
  }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void gap_event_handler(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param) override;

 protected:
  enum State : uint8_t {
    STATE_DISCONNECTED = 0,
    STATE_CONNECTED_WAIT_SEC,
    STATE_SUBSCRIBING,
    STATE_SVC3_WAIT_CHALLENGE,
    STATE_SVC3_WAIT_RESULT,
    STATE_RUB_WAIT_CHALLENGE,
    STATE_RUB_WAIT_AUTH_ACK,
    STATE_READING_SNAPSHOT,
    STATE_READY,
    STATE_AUTH_FAILED,
  };

  enum CccdSubState : uint8_t {
    CCCD_IDLE = 0,
    CCCD_WAIT_REG_NOTIFY,
    CCCD_WAIT_WRITE_DESCR,
  };

  uint8_t resolve_node_id_(uint8_t node_id) const {
    return (node_id == 0xFF) ? this->default_node_id_ : node_id;
  }

  void reset_session_state_(bool clear_writes);
  void reset_active_transaction_();
  void build_snapshot_nodes_();
  void advance_snapshot_step_(bool skip_remaining_for_node = false);
  void publish_status_(const char *msg);
  void resolve_gatt_handles_();
  void handle_pairing_failure_(const uint8_t *bda, uint8_t reason);
  void log_rx_hex_(const char *prefix, const uint8_t *data, uint16_t len,
                   bool info_level = false);

  void begin_subscribing_();
  void advance_subscribing_();
  void start_unlock_sequence_();
  void start_rub_auth_(const char *status_msg = "Unlocking CAN-BUS (RUB v2.01)");

  void write_cccd_(uint16_t cccd_handle);
  void send_zero_length_write_(uint16_t char_handle);
  bool send_direct_channel_(const uint8_t *payload, size_t len);
  void send_next_tx_fragment_();
  bool send_read_param_(uint8_t node_id, uint16_t index, uint8_t sub_index);
  bool send_write_param_(uint8_t node_id, uint16_t index, uint8_t sub_index,
                         const uint8_t *data, size_t data_len);
  bool send_get_list_batch_(const PollGroup &grp, const size_t *item_indices,
                            size_t count);

  void handle_notify_(uint16_t handle, const uint8_t *data, uint16_t len);
  void handle_direct_segment_(const uint8_t *data, uint16_t len);
  void handle_direct_payload_(const uint8_t *payload, size_t len);
  void handle_single_read_ack_(uint8_t node_id, uint16_t index, uint8_t sub_index,
                               const uint8_t *data, size_t data_len);
  void handle_get_list_response_(const uint8_t *payload, size_t len);
  void handle_access_denied_(uint8_t node_id, uint16_t index, uint8_t sub_index,
                             uint8_t status_code, uint32_t abort_code,
                             bool from_get_list);

  State state_{STATE_DISCONNECTED};
  CccdSubState cccd_substate_{CCCD_IDLE};
  bool cccd_manual_write_sent_{false};
  uint8_t default_node_id_{0x01};
  uint8_t auth_level_{4};
  bool log_snapshot_{true};
  uint32_t pin_{0};
  bool has_pin_{false};
  bool pairing_failed_{false};
  bool auth_cmpl_handled_{false};
  bool gatt_search_completed_{false};
  bool ble_sec_ready_{false};
  uint8_t pairing_fail_count_{0};
  bool pairing_backoff_active_{false};
  uint32_t pairing_backoff_until_ms_{0};
  uint32_t disconnected_since_ms_{0};
  bool stale_published_{false};
  uint32_t last_action_ms_{0};
  uint32_t cooldown_until_ms_{0};
  uint8_t cccd_step_{0};
  uint8_t retry_count_{0};
  uint8_t consecutive_timeouts_{0};
  std::vector<uint8_t> configured_snapshot_nodes_;
  std::vector<uint8_t> active_snapshot_nodes_;
  size_t snapshot_node_cursor_{0};
  uint8_t snapshot_sub_idx_{1};
  bool snapshot_node_count_probed_{false};
  bool snapshot_completed_{false};

  uint16_t h_direct_tx_{HANDLE_DIRECT_TX};
  uint16_t h_direct_rx_{HANDLE_DIRECT_RX};
  uint16_t h_direct_rx_cccd_{HANDLE_DIRECT_RX_CCCD};
  uint16_t h_svc2_ch1_{HANDLE_SVC2_CH1};
  uint16_t h_svc2_ch1_cccd_{HANDLE_SVC2_CH1_CCCD};
  uint16_t h_svc2_ch2_{HANDLE_SVC2_CH2};
  uint16_t h_svc2_ch2_cccd_{HANDLE_SVC2_CH2_CCCD};
  uint16_t h_svc3_trig_{HANDLE_SVC3_TRIG};
  uint16_t h_svc3_chal_{HANDLE_SVC3_CHAL};
  uint16_t h_svc3_chal_cccd_{HANDLE_SVC3_CHAL_CCCD};
  uint16_t h_svc3_resp_{HANDLE_SVC3_RESP};
  uint16_t h_svc3_ack_{HANDLE_SVC3_ACK};
  uint16_t h_svc3_ack_cccd_{HANDLE_SVC3_ACK_CCCD};

  DirectChannelTransport transport_;
  BdrDatapointRegistry registry_;
  std::deque<PendingWrite> write_queue_;
  PendingReadback pending_readback_;
  bool pending_rub_auth_tx_{false};
  uint8_t pending_rub_auth_req_[16]{};
  ActiveRequest active_req_;
  int active_group_idx_{-1};
  size_t active_item_idx_{0};
  uint8_t get_list_stage_{0};  // 0=initial v2.00 try, 1=retry v2.00 once, 2=confirmed, 3=fallback to single 0x00
  uint16_t negotiated_mtu_{23};

#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *status_text_{nullptr};
#endif
};

}  // namespace esphome::bdr_ble
