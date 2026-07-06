/**
 * @file radian_decoder.cpp
 * @brief Standalone 4-bit-per-bit serial decoder for the RADIAN RF protocol.
 *
 * This is a platform-neutral extraction of the decode_4bitpbit_serial()
 * function from cc1101.cpp.  It deliberately has no Arduino / ESP8266
 * dependencies so it can be compiled and tested on a native host.
 *
 * Algorithm summary
 * -----------------
 * The meter transmits every data bit four times (4× oversampling):
 *   logical '1' → 0xF0 on air  (1111 0000 in binary, MSB first)
 *   logical '0' → 0x0F on air  (0000 1111 in binary)
 * Each byte is framed: 1 start bit (0) + 8 data bits (LSB first) + 3 stop
 * bits (1).  The decoder counts consecutive same-polarity samples, divides
 * by four to recover the original bit count, strips start/stop bits, and
 * reconstructs bytes MSB-first.
 */

#include "radian_decoder.h"

#include <string.h>
#include <stdint.h>

uint8_t radian_decode_4bitpbit(const uint8_t *rx_buf, int rx_len,
                                uint8_t *decoded, int decoded_max)
{
    if (!rx_buf || rx_len <= 0 || !decoded || decoded_max <= 0)
        return 0;

    memset(decoded, 0, (size_t)decoded_max);

    uint16_t i, j, k;
    uint8_t  bit_cnt        = 0;
    int8_t   bit_cnt_flush  = 0;
    uint8_t  bit_pol        = (rx_buf[0] & 0x80);
    uint8_t  dest_bit_cnt   = 0;
    uint8_t  dest_byte_cnt  = 0;
    uint8_t  framing_errors = 0;
    uint8_t  cur_byte;

    for (i = 0; i < (uint16_t)rx_len; i++)
    {
        cur_byte = rx_buf[i];

        for (j = 0; j < 8; j++)
        {
            if ((cur_byte & 0x80) == bit_pol)
            {
                /* Same polarity as previous sample — extend run */
                bit_cnt++;
            }
            else if (bit_cnt == 1)
            {
                /* Single-sample glitch — polarity hasn't really changed;
                 * restore correct polarity and adjust counts */
                bit_pol     = cur_byte & 0x80;
                bit_cnt     = (uint8_t)(bit_cnt_flush + 1);
            }
            else
            {
                /* Genuine polarity transition — decode accumulated run */
                bit_cnt_flush = (int8_t)bit_cnt;
                bit_cnt       = (uint8_t)((bit_cnt + 2) / 4);   /* round to nearest */
                bit_cnt_flush = (int8_t)(bit_cnt_flush - (int8_t)(bit_cnt * 4));

                for (k = 0; k < (uint16_t)bit_cnt; k++)
                {
                    if (dest_bit_cnt < 8)
                    {
                        /* Data bit: shift in from MSB */
                        if (dest_byte_cnt >= (uint8_t)decoded_max)
                            return dest_byte_cnt;

                        decoded[dest_byte_cnt] >>= 1;
                        decoded[dest_byte_cnt] |= bit_pol; /* MSB ← polarity */
                    }

                    dest_bit_cnt++;

                    /* Bit 10 (0-indexed from start bit) should be a stop bit
                     * (polarity 1).  If it's 0, a framing error occurred. */
                    if (dest_bit_cnt == 10 && !bit_pol)
                    {
                        if (framing_errors < 255)
                            framing_errors++;
                        dest_bit_cnt = 0;
                        dest_byte_cnt++;
                        if (dest_byte_cnt >= (uint8_t)decoded_max)
                            return dest_byte_cnt;
                        continue;
                    }

                    /* Bits 11+ with polarity 0 mark the start of the next byte */
                    if (dest_bit_cnt >= 11 && !bit_pol)
                    {
                        dest_bit_cnt = 0;
                        dest_byte_cnt++;
                        if (dest_byte_cnt >= (uint8_t)decoded_max)
                            return dest_byte_cnt;
                    }
                }

                bit_pol = cur_byte & 0x80;
                bit_cnt = 1;
            }

            cur_byte <<= 1;
        }
    }

    /* Reject frames with too many framing errors (> half decoded byte count) */
    if (dest_byte_cnt > 0 && framing_errors > (dest_byte_cnt / 2))
        return 0;

    return dest_byte_cnt;
}
