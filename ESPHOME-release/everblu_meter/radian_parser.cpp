#include "radian_parser.h"

#include <string.h>

uint16_t radian_crc_kermit(const uint8_t *input_ptr, size_t num_bytes)
{
  uint16_t crc = 0x0000;
  for (size_t i = 0; i < num_bytes; i++)
  {
    crc ^= input_ptr[i];
    for (int j = 0; j < 8; j++)
    {
      if (crc & 0x0001)
      {
        crc = (crc >> 1) ^ 0x8408;
      }
      else
      {
        crc >>= 1;
      }
    }
  }

  uint16_t low_byte = (crc & 0xFF00) >> 8;
  uint16_t high_byte = (crc & 0x00FF) << 8;
  return (uint16_t)(low_byte | high_byte);
}

bool radian_validate_crc(const uint8_t *decoded_buffer, size_t size)
{
  if (decoded_buffer == NULL || size < 4)
  {
    return false;
  }

  const uint8_t length_field = decoded_buffer[0];
  size_t expected_len = length_field ? length_field : size;

  if (expected_len > size)
  {
    // Keep compatibility with frames that advertise a longer length than payload.
    return true;
  }

  if (expected_len < 4)
  {
    return false;
  }

  const size_t crc_offset = expected_len - 2;
  if (crc_offset + 1 >= size)
  {
    return true;
  }

  if (expected_len <= 3)
  {
    return false;
  }

  const uint16_t received_crc = ((uint16_t)decoded_buffer[crc_offset] << 8) |
                                (uint16_t)decoded_buffer[crc_offset + 1];
  const uint16_t computed_crc = radian_crc_kermit(&decoded_buffer[1], expected_len - 3);
  return computed_crc == received_crc;
}

bool radian_parse_primary_data(const uint8_t *decoded_buffer, size_t size, struct radian_primary_data *out)
{
  if (out == NULL)
  {
    return false;
  }

  memset(out, 0, sizeof(*out));

  if (decoded_buffer == NULL || size < 30)
  {
    return false;
  }

  out->volume = ((uint32_t)decoded_buffer[18]) |
                ((uint32_t)decoded_buffer[19] << 8) |
                ((uint32_t)decoded_buffer[20] << 16) |
                ((uint32_t)decoded_buffer[21] << 24);

  if (out->volume == 0 || out->volume == 0xFFFFFFFFUL)
  {
    memset(out, 0, sizeof(*out));
    return false;
  }

  if (size >= 49)
  {
    out->reads_counter = decoded_buffer[48];
    out->battery_left = decoded_buffer[31];
    out->time_start = decoded_buffer[44];
    out->time_end = decoded_buffer[45];

    if (out->time_start > 23 || out->time_end > 23)
    {
      memset(out, 0, sizeof(*out));
      return false;
    }

    if (out->battery_left == 0xFF || out->reads_counter == 0xFF)
    {
      memset(out, 0, sizeof(*out));
      return false;
    }
  }

  out->history_available = size >= 118;
  return true;
}
