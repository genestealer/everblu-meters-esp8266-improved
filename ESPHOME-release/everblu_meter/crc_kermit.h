#ifndef CRC_KERMIT_H
#define CRC_KERMIT_H

#include <stddef.h>
#include <stdint.h>

uint16_t crc_kermit(const uint8_t *input_ptr, size_t num_bytes);

#endif // CRC_KERMIT_H