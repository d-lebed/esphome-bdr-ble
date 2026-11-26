#include "bdr_ble_datetime.h"

#if defined(__has_include)
#if __has_include("esphome/core/controller_registry.h")
#include "esphome/core/controller_registry.h"
#define BDR_BLE_HAS_CONTROLLER_REGISTRY 1
#endif
#endif

#include <vector>

namespace esphome::bdr_ble {

void BdrBleDateTime::apply_boiler_value(const BdrDateTime &dt) {
  if (dt.is_unset || !dt.has_date) {
    const bool was_set = this->has_state();
    this->year_ = 0;
    this->month_ = 0;
    this->day_ = 0;
    this->hour_ = 0;
    this->minute_ = 0;
    this->second_ = 0;
    this->set_has_state(false);
    if (was_set) {
      this->state_callback_.call();
#if defined(BDR_BLE_HAS_CONTROLLER_REGISTRY) && defined(USE_CONTROLLER_REGISTRY)
      ControllerRegistry::notify_datetime_update(this);
#endif
    }
    return;
  }

  this->year_ = dt.year;
  this->month_ = dt.month;
  this->day_ = dt.day;
  this->hour_ = dt.hour;
  this->minute_ = dt.minute;
  this->second_ = dt.second;
  this->publish_state();
}

void BdrBleDateTime::clear() {
  if (this->parent_ == nullptr) {
    return;
  }
  const std::vector<uint8_t> zeros(6, 0x00);
  uint8_t resolved_node = (this->node_id_ == 0xFF)
                              ? this->parent_->get_default_node_id()
                              : this->node_id_;
  if (!this->parent_->queue_write_datapoint(
          resolved_node, this->index_,
          static_cast<uint8_t>(this->sub_index_), zeros)) {
    ESP_LOGW(TAG, "Clear datetime write for 0x%04X:%02X rejected (BLE not connected)",
             this->index_, static_cast<unsigned>(this->sub_index_));
  }
}

void BdrBleDateTime::control(const datetime::DateTimeCall &call) {
  if (this->parent_ == nullptr) {
    return;
  }

  const uint16_t year =
      call.get_year().value_or(this->has_state() ? this->year_ : 0);
  const uint8_t month =
      call.get_month().value_or(this->has_state() ? this->month_ : 0);
  const uint8_t day =
      call.get_day().value_or(this->has_state() ? this->day_ : 0);
  const uint8_t hour =
      call.get_hour().value_or(this->has_state() ? this->hour_ : 0);
  const uint8_t minute =
      call.get_minute().value_or(this->has_state() ? this->minute_ : 0);
  const uint8_t second =
      call.get_second().value_or(this->has_state() ? this->second_ : 0);

  std::vector<uint8_t> bytes;
  if (!encode_time_of_day(year, month, day, hour, minute, second, bytes)) {
    ESP_LOGW(TAG,
             "Rejecting invalid CiA 301 datetime write for 0x%04X:%02X "
             "(%04u-%02u-%02u %02u:%02u:%02u; valid range 1984-01-01..2163-06-05)",
             this->index_, static_cast<unsigned>(this->sub_index_),
             static_cast<unsigned>(year), static_cast<unsigned>(month),
             static_cast<unsigned>(day), static_cast<unsigned>(hour),
             static_cast<unsigned>(minute), static_cast<unsigned>(second));
    if (this->has_state()) {
      this->publish_state();
    }
    return;
  }

  uint8_t resolved_node = (this->node_id_ == 0xFF)
                              ? this->parent_->get_default_node_id()
                              : this->node_id_;
  if (!this->parent_->queue_write_datapoint(
          resolved_node, this->index_,
          static_cast<uint8_t>(this->sub_index_), bytes)) {
    ESP_LOGW(TAG, "Datetime write for 0x%04X:%02X rejected (BLE not connected)",
             this->index_, static_cast<unsigned>(this->sub_index_));
    if (this->has_state()) {
      this->publish_state();
    }
  }
}

}  // namespace esphome::bdr_ble
