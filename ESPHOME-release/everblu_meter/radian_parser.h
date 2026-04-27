#ifndef RADIAN_PARSER_H
#define RADIAN_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct radian_primary_data
{
  uint32_t volume;
  uint8_t reads_counter;
  uint8_t battery_left;
  uint8_t time_start;
  uint8_t time_end;
  bool history_available;
};

uint16_t radian_crc_kermit(const uint8_t *input_ptr, size_t num_bytes);
bool radian_validate_crc(const uint8_t *decoded_buffer, size_t size);
bool radian_parse_primary_data(const uint8_t *decoded_buffer, size_t size, struct radian_primary_data *out);

#endif // RADIAN_PARSER_H
