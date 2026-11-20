#include "bdr_transport.h"

#include <cstring>

namespace esphome::bdr_ble {

void DirectChannelTransport::reserve_buffers() {
  this->tx_frame_buf_.reserve(128);
  this->rx_reassembly_buf_.reserve(512);
}

void DirectChannelTransport::reset_tx() {
  this->tx_frame_buf_.clear();
  this->tx_frame_offset_ = 0;
  this->tx_seg_idx_ = 0;
  this->tx_in_progress_ = false;
}

void DirectChannelTransport::reset_rx() {
  this->rx_reassembly_buf_.clear();
  this->expected_rx_seg_ = 0;
  this->last_rx_seg_ms_ = 0;
}

void DirectChannelTransport::reset() {
  this->reset_tx();
  this->reset_rx();
}

bool DirectChannelTransport::begin_tx(
    const uint8_t *payload, size_t len,
    uint8_t (&out_pkt)[1 + DIRECT_TX_MAX_CHUNK_DATA], size_t &out_pkt_len) {
  out_pkt_len = 0;
  if (this->tx_in_progress_ || payload == nullptr || len == 0 ||
      len > MAX_PAYLOAD_LEN) {
    return false;
  }
  wrap_direct_channel_frame_into(payload, len, this->tx_frame_buf_);
  this->tx_frame_offset_ = 0;
  this->tx_seg_idx_ = 0;
  this->tx_in_progress_ = true;
  return this->next_tx_fragment(out_pkt, out_pkt_len);
}

bool DirectChannelTransport::next_tx_fragment(
    uint8_t (&out_pkt)[1 + DIRECT_TX_MAX_CHUNK_DATA], size_t &out_pkt_len) {
  out_pkt_len = 0;
  if (!this->tx_in_progress_ ||
      this->tx_frame_offset_ >= this->tx_frame_buf_.size()) {
    this->reset_tx();
    return false;
  }

  size_t rem = this->tx_frame_buf_.size() - this->tx_frame_offset_;
  bool is_last = (rem <= DIRECT_TX_MAX_CHUNK_DATA);
  size_t chunk_len = is_last ? rem : DIRECT_TX_MAX_CHUNK_DATA;

  out_pkt[0] = is_last ? SEGMENT_FINAL : this->tx_seg_idx_++;
  std::memcpy(&out_pkt[1], this->tx_frame_buf_.data() + this->tx_frame_offset_,
              chunk_len);
  this->tx_frame_offset_ += chunk_len;
  out_pkt_len = chunk_len + 1;
  return true;
}

RxSegmentStatus DirectChannelTransport::feed_rx_segment(
    const uint8_t *data, size_t len, uint32_t now_ms,
    const uint8_t *&out_payload, size_t &out_payload_len, uint16_t &out_rx_crc,
    uint16_t &out_calc_crc, bool &out_discarded_stale) {
  out_payload = nullptr;
  out_payload_len = 0;
  out_rx_crc = 0;
  out_calc_crc = 0;
  out_discarded_stale = false;

  if (data == nullptr || len < 2) {
    return RxSegmentStatus::ERROR_TOO_SHORT;
  }

  if (this->expected_rx_seg_ > 0 && !this->rx_reassembly_buf_.empty() &&
      (now_ms - this->last_rx_seg_ms_ > RX_STALE_TIMEOUT_MS)) {
    out_discarded_stale = true;
    this->rx_reassembly_buf_.clear();
    this->expected_rx_seg_ = 0;
  }

  const uint8_t seg = data[0];
  if (seg == 0x00) {
    this->rx_reassembly_buf_.assign(data + 1, data + len);
    this->expected_rx_seg_ = 1;
    this->last_rx_seg_ms_ = now_ms;
    return RxSegmentStatus::INCOMPLETE;
  }

  if (seg < SEGMENT_FINAL) {
    if (seg == this->expected_rx_seg_ && !this->rx_reassembly_buf_.empty() &&
        this->rx_reassembly_buf_.size() + (len - 1) <= MAX_RX_REASSEMBLY_LEN) {
      this->rx_reassembly_buf_.insert(this->rx_reassembly_buf_.end(), data + 1,
                                      data + len);
      this->expected_rx_seg_++;
      this->last_rx_seg_ms_ = now_ms;
      return RxSegmentStatus::INCOMPLETE;
    }
    this->reset_rx();
    return RxSegmentStatus::ERROR_OUT_OF_ORDER;
  }

  // seg == SEGMENT_FINAL (0xFF): final segment (or single-segment frame)
  if (this->expected_rx_seg_ > 0 && !this->rx_reassembly_buf_.empty()) {
    if (this->rx_reassembly_buf_.size() + (len - 1) > MAX_RX_REASSEMBLY_LEN) {
      this->reset_rx();
      return RxSegmentStatus::ERROR_OUT_OF_ORDER;
    }
    this->rx_reassembly_buf_.insert(this->rx_reassembly_buf_.end(), data + 1,
                                    data + len);
  } else {
    this->rx_reassembly_buf_.assign(data + 1, data + len);
  }

  this->expected_rx_seg_ = 0;
  this->last_rx_seg_ms_ = now_ms;

  const uint8_t *frame_ptr = this->rx_reassembly_buf_.data();
  const size_t frame_len = this->rx_reassembly_buf_.size();
  if (frame_len < 4) {
    this->reset_rx();
    return RxSegmentStatus::ERROR_TOO_SHORT;
  }

  out_rx_crc = (static_cast<uint16_t>(frame_ptr[frame_len - 2]) << 8) |
               frame_ptr[frame_len - 1];
  out_calc_crc = crc16_ccitt(frame_ptr, frame_len - 2);
  if (out_rx_crc != out_calc_crc || frame_ptr[0] != DIRECT_CHANNEL_CAN) {
    this->reset_rx();
    return RxSegmentStatus::ERROR_CRC;
  }

  out_payload = frame_ptr + 1;
  out_payload_len = frame_len - 3;
  return RxSegmentStatus::FRAME_READY;
}

}  // namespace esphome::bdr_ble
