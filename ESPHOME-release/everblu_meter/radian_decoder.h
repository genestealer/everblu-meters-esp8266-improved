/**
 * @file radian_decoder.h
 * @brief Standalone 4-bit-per-bit serial decoder for the RADIAN RF protocol.
 *
 * Extracted from cc1101.cpp so it can be compiled and tested on a native
 * host without any Arduino / ESP8266 hardware dependencies.
 *
 * IMPORTANT LICENSING NOTICE:
 * The RADIAN protocol implementation shall not be distributed nor used for
 * commercial products. It is exposed only to demonstrate CC1101 capability
 * to read water meter indexes. There is no warranty on this software.
 */

#ifndef RADIAN_DECODER_H
#define RADIAN_DECODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a RADIAN 4-bit-per-bit oversampled bitstream into raw bytes.
 *
 * The CC1101 is configured to receive at 9.6 kbps so that every logical bit
 * transmitted at 2.4 kbps is represented by four consecutive samples.  Each
 * logical '1' arrives as 0xF0 (four 1s then four 0s) and each logical '0' as
 * 0x0F.  Each byte in the stream is framed with 1 start bit (0) and 3 stop
 * bits (111), LSB-first.
 *
 * @param rx_buf      Raw bytes read from the CC1101 RX FIFO (oversampled stream).
 * @param rx_len      Number of bytes in rx_buf.
 * @param decoded     Caller-supplied output buffer.  Must be at least
 *                    rx_len / 4 bytes; 200 bytes is always sufficient.
 * @param decoded_max Size of decoded buffer.
 * @return Number of decoded bytes written, or 0 if the frame quality is too
 *         low (too many framing errors relative to decoded byte count).
 */
uint8_t radian_decode_4bitpbit(const uint8_t *rx_buf, int rx_len,
                                uint8_t *decoded, int decoded_max);

#ifdef __cplusplus
}
#endif

#endif /* RADIAN_DECODER_H */
