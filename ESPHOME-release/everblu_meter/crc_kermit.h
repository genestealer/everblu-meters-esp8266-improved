/**
 * @file crc_kermit.h
 * @brief CRC-16/KERMIT checksum used by the RADIAN meter protocol
 *
 * Computes the CRC-16/KERMIT (reflected, init 0x0000) checksum used to
 * validate RADIAN protocol frames exchanged with Everblu Cyble meters.
 */

#ifndef CRC_KERMIT_H
#define CRC_KERMIT_H

#include <stddef.h>
#include <stdint.h>

uint16_t crc_kermit(const uint8_t *input_ptr, size_t num_bytes);

#endif // CRC_KERMIT_H
