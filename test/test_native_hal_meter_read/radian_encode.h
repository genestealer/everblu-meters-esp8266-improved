/**
 * @file radian_encode.h
 * @brief Reverse of decode_4bitpbit_serial(): builds the raw 4x-oversampled
 *        on-air sample stream that a meter would place in the CC1101 RX FIFO.
 *
 * Encoding (inverse of the firmware decoder):
 *   - Per decoded byte: start bit(0) + 8 data bits LSB-first + 2 stop bits(1).
 *   - Byte 0's start bit is OMITTED: on the air it is consumed by the stage-2
 *     sync word 0xFFF0 (trailing 0000), so the FIFO stream begins at byte 0's
 *     first data bit. All later bytes include their start bit.
 *   - Each logical bit is oversampled x4 (4 identical samples).
 *   - Samples are packed MSB-first into bytes (so logical "1,0" -> 0xF0).
 *   - A trailing phantom start bit(0) is appended so the decoder flushes the
 *     final byte, followed by idle-high (0xFF) padding up to target_raw_len.
 *     The stop(1)->phantom(0)->idle(1) transitions flush byte N-1 without
 *     producing any extra decoded bytes (idle high never transitions again).
 *
 * This lives only in the native_hal test; it is not firmware code.
 */
#ifndef NATIVE_HAL_RADIAN_ENCODE_H
#define NATIVE_HAL_RADIAN_ENCODE_H

#include <cstddef>
#include <cstdint>

namespace nativehal
{

class SampleWriter
{
public:
  SampleWriter(uint8_t *out, size_t cap) : out_(out), cap_(cap), byte_pos_(0), bit_in_byte_(0), cur_(0) {}

  // Append one raw sample (0 or 1), packed MSB-first.
  void push_sample(uint8_t s)
  {
    cur_ = static_cast<uint8_t>((cur_ << 1) | (s & 0x01));
    if (++bit_in_byte_ == 8)
    {
      if (byte_pos_ < cap_)
        out_[byte_pos_] = cur_;
      byte_pos_++;
      bit_in_byte_ = 0;
      cur_ = 0;
    }
  }

  // Append one logical bit oversampled x4.
  void push_bit(uint8_t b)
  {
    for (int i = 0; i < 4; ++i)
      push_sample(b);
  }

  // Number of complete raw bytes emitted so far.
  size_t bytes() const { return byte_pos_; }

  // Flush any partial byte (left-aligned) and return total byte length.
  size_t finish()
  {
    if (bit_in_byte_ != 0)
    {
      uint8_t v = static_cast<uint8_t>(cur_ << (8 - bit_in_byte_));
      if (byte_pos_ < cap_)
        out_[byte_pos_] = v;
      byte_pos_++;
      bit_in_byte_ = 0;
      cur_ = 0;
    }
    return byte_pos_;
  }

private:
  uint8_t *out_;
  size_t cap_;
  size_t byte_pos_;
  int bit_in_byte_;
  uint8_t cur_;
};

// Returns the number of raw bytes written (== target_raw_len when padding is
// applied and fits). Writes idle-high (0xFF) padding to reach target_raw_len.
inline size_t radian_encode_onair(const uint8_t *frame, size_t frame_len,
                                  uint8_t *out, size_t out_cap, size_t target_raw_len)
{
  SampleWriter w(out, out_cap);

  for (size_t bi = 0; bi < frame_len; ++bi)
  {
    if (bi != 0)
      w.push_bit(0); // start bit (byte 0's start is consumed by sync)

    const uint8_t byte = frame[bi];
    for (int k = 0; k < 8; ++k) // 8 data bits, LSB first
      w.push_bit(static_cast<uint8_t>((byte >> k) & 0x01));

    w.push_bit(1); // stop bit 1
    w.push_bit(1); // stop bit 2
  }

  w.push_bit(0); // phantom start bit -> flushes the final byte on the next transition

  // Pad with idle-high samples (0xFF) up to the requested length. The 0->1
  // transition into this padding flushes byte N-1; the constant high that
  // follows never transitions again, so no extra bytes are decoded.
  while (w.bytes() < target_raw_len)
    w.push_sample(1);

  return w.finish();
}

} // namespace nativehal

#endif // NATIVE_HAL_RADIAN_ENCODE_H
