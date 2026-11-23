#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bdr_protocol.h"

namespace esphome::bdr_ble {

enum class RxSegmentStatus : uint8_t {
  INCOMPLETE = 0,
  FRAME_READY = 1,
  ERROR_OUT_OF_ORDER = 2,
  ERROR_CRC = 3,
  ERROR_TOO_SHORT = 4,
};

/// Encapsulates BDR DirectChannel (Handles 18 / 20) TX 20-byte segmentation
/// and RX multi-segment reassembly + CRC-16/MODBUS verification.
class DirectChannelTransport {
 public:
  static constexpr size_t MAX_PAYLOAD_LEN = 512;
  static constexpr size_t MAX_RX_REASSEMBLY_LEN = 4096;
  static constexpr uint32_t RX_STALE_TIMEOUT_MS = 1000;

  void reserve_buffers();
  void reset();
  void reset_tx();
  void reset_rx();

  bool is_tx_in_progress() const { return this->tx_in_progress_; }
  bool is_rx_busy(uint32_t now_ms) const {
    return this->expected_rx_seg_ > 0 && !this->rx_reassembly_buf_.empty() &&
           (now_ms - this->last_rx_seg_ms_ <= RX_STALE_TIMEOUT_MS);
  }
  size_t rx_buffered_bytes() const { return this->rx_reassembly_buf_.size(); }
  uint8_t expected_rx_segment() const { return this->expected_rx_seg_; }

  /// Prepare a new DirectChannel frame for transmission and output its first <=20B BLE packet.
  /// Returns false if a transmission is already in progress or payload length is invalid.
  bool begin_tx(const uint8_t *payload, size_t len,
                uint8_t (&out_pkt)[1 + DIRECT_TX_MAX_CHUNK_DATA],
                size_t &out_pkt_len);

  /// Populate the next <=20B BLE fragment for an in-progress transmission.
  /// Returns true if a fragment was produced, or false if transmission is now complete.
  bool next_tx_fragment(uint8_t (&out_pkt)[1 + DIRECT_TX_MAX_CHUNK_DATA],
                        size_t &out_pkt_len);

  /// Feed an incoming BLE notification from DirectChannel RX (Handle 20).
  /// When RxSegmentStatus::FRAME_READY is returned, `out_payload` and `out_payload_len`
  /// reference the validated inner CAN-IP / RUB payload.
  RxSegmentStatus feed_rx_segment(const uint8_t *data, size_t len,
                                  uint32_t now_ms, const uint8_t *&out_payload,
                                  size_t &out_payload_len,
                                  uint16_t &out_rx_crc,
                                  uint16_t &out_calc_crc,
                                  bool &out_discarded_stale);

 protected:
  std::vector<uint8_t> tx_frame_buf_;
  size_t tx_frame_offset_{0};
  uint8_t tx_seg_idx_{0};
  bool tx_in_progress_{false};

  std::vector<uint8_t> rx_reassembly_buf_;
  uint8_t expected_rx_seg_{0};
  uint32_t last_rx_seg_ms_{0};
};

}  // namespace esphome::bdr_ble
