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
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif

#include <esp_gattc_api.h>
#include <esp_gap_ble_api.h>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "bdr_protocol.h"

namespace esphome::bdr_ble {

struct PendingWrite {
  uint8_t node_id{0x01};
  uint16_t index{0};
  uint8_t sub_index{0};
  std::vector<uint8_t> data;
};

struct DatapointRegistration {
  uint8_t node_id{0x01};
  uint16_t index{0};
  uint8_t sub_index{0};
  BdrDataType data_type{BDR_TYPE_U16};
  float scale{1.0f};
  bool unsupported{false};
#ifdef USE_SENSOR
  std::vector<sensor::Sensor *> sensors;
#endif
#ifdef USE_BINARY_SENSOR
  std::vector<binary_sensor::BinarySensor *> binary_sensors;
#endif
#ifdef USE_TEXT_SENSOR
  std::vector<text_sensor::TextSensor *> text_sensors;
#endif
#ifdef USE_NUMBER
  std::vector<number::Number *> numbers;
#endif
#ifdef USE_SELECT
  std::vector<select::Select *> selects;
#endif
};

struct PollGroup {
  uint32_t interval_ms{30000};
  uint32_t last_cycle_start_ms{0};
  std::vector<DatapointRegistration> items;
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

#if defined(USE_TEXT_SENSOR) || defined(USE_SELECT)
  void register_enum_translation(uint16_t index, uint8_t sub_index, int value,
                                 const char *description);
#endif

#ifdef USE_NUMBER
  void register_number(number::Number *num, uint16_t index, uint8_t sub_index,
                       uint8_t data_type, float scale, uint32_t interval_ms,
                       uint8_t node_id = 0xFF);
#endif

#ifdef USE_SELECT
  void register_select(select::Select *sel, uint16_t index, uint8_t sub_index,
                       uint32_t interval_ms, uint8_t node_id = 0xFF);
#endif

  void queue_write_datapoint(uint8_t node_id, uint16_t index, uint8_t sub_index,
                             const std::vector<uint8_t> &data);

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

  enum InFlightType : uint8_t {
    IN_FLIGHT_NONE = 0,
    IN_FLIGHT_SNAPSHOT,
    IN_FLIGHT_READ,
    IN_FLIGHT_WRITE,
    IN_FLIGHT_GET_LIST,
  };

  DatapointRegistration &get_or_create_datapoint_(uint32_t interval_ms, uint8_t node_id,
                                                  uint16_t index, uint8_t sub_index,
                                                  BdrDataType data_type, float scale);

  void publish_status_(const char *msg);
  void log_rx_hex_(const char *prefix, const uint8_t *data, uint16_t len,
                   bool info_level = false);

  void write_cccd_(uint16_t cccd_handle);
  void send_zero_length_write_(uint16_t char_handle);
  void send_direct_channel_(const uint8_t *payload, size_t len);
  void send_read_param_(uint8_t node_id, uint16_t index, uint8_t sub_index);
  void send_write_param_(uint8_t node_id, uint16_t index, uint8_t sub_index,
                         const uint8_t *data, size_t data_len);
  void send_get_list_batch_(const PollGroup &grp,
                            const std::vector<size_t> &item_indices,
                            bool use_v200_header);

  // Implemented in bdr_ble_parser.cpp
  void handle_notify_(uint16_t handle, const uint8_t *data, uint16_t len);
  void handle_direct_segment_(const uint8_t *data, uint16_t len);
  void handle_direct_payload_(const uint8_t *payload, size_t len);
  void handle_single_read_ack_(uint8_t node_id, uint16_t index, uint8_t sub_index,
                               const uint8_t *data, size_t data_len);
  void handle_get_list_response_(const uint8_t *payload, size_t len);
  void dispatch_datapoint_value_(uint8_t node_id, uint16_t index, uint8_t sub_index,
                                 const uint8_t *data, size_t data_len);
  void mark_datapoint_unsupported_(uint8_t node_id, uint16_t index,
                                   uint8_t sub_index, uint8_t status_code);
  void handle_snapshot_descriptor_(uint8_t sub_index, const uint8_t *data,
                                   size_t data_len);
  void log_appliance_snapshot_();

  State state_{STATE_DISCONNECTED};
  uint8_t default_node_id_{0x01};
  uint8_t auth_level_{4};
  uint32_t pin_{0};
  bool has_pin_{false};
  bool pairing_failed_{false};
  uint32_t last_action_ms_{0};
  uint8_t cccd_step_{0};
  uint8_t retry_count_{0};
  uint8_t consecutive_timeouts_{0};
  uint8_t snapshot_sub_idx_{1};
  std::vector<uint32_t> discovered_datapoints_;

  std::vector<PollGroup> poll_groups_;
  std::deque<PendingWrite> write_queue_;
  int active_group_idx_{-1};
  size_t active_item_idx_{0};
  InFlightType in_flight_type_{IN_FLIGHT_NONE};
  uint8_t in_flight_node_{0x01};
  uint16_t in_flight_index_{0};
  uint8_t in_flight_sub_{0};
  uint32_t last_req_ms_{0};
  uint8_t get_list_stage_{0};  // 0=try 02 01..03, 1=try 02 00..00, 2=confirmed, 3=fallback to single 0x00
  uint16_t negotiated_mtu_{23};

  std::vector<uint8_t> rx_reassembly_buf_;
  uint8_t expected_rx_seg_{0};
  uint32_t last_rx_seg_ms_{0};

#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *status_text_{nullptr};
#endif
#if defined(USE_TEXT_SENSOR) || defined(USE_SELECT)
  std::map<uint64_t, std::string> enum_translations_;
#endif
};

}  // namespace esphome::bdr_ble
