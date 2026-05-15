#ifndef METER_CODE_PARSER_H
#define METER_CODE_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace everblu
{
    namespace core
    {

        // Parse METER_CODE in format "YY-serial" or "YY-serial-NNN".
        // Returns true when format and numeric bounds are valid.
        inline bool parseMeterCode(const char *code, uint8_t *out_year, uint32_t *out_serial)
        {
            if (code == nullptr)
            {
                return false;
            }

            const size_t len = strlen(code);
            if (len < 4)
            {
                return false;
            }

            if (code[0] < '0' || code[0] > '9' || code[1] < '0' || code[1] > '9' || code[2] != '-')
            {
                return false;
            }

            const uint8_t year = (uint8_t)((code[0] - '0') * 10 + (code[1] - '0'));
            uint32_t serial = 0;
            size_t serial_digits = 0;
            size_t i = 3;
            while (i < len && code[i] >= '0' && code[i] <= '9')
            {
                serial = serial * 10 + (uint32_t)(code[i] - '0');
                serial_digits++;
                i++;
            }

            if (serial_digits == 0 || serial_digits > 8)
            {
                return false;
            }

            if (i < len)
            {
                if (code[i] != '-' || (i + 4) != len)
                {
                    return false;
                }
                for (size_t j = i + 1; j < len; j++)
                {
                    if (code[j] < '0' || code[j] > '9')
                    {
                        return false;
                    }
                }
            }

            if (serial == 0 || serial > 0xFFFFFFUL)
            {
                return false;
            }

            if (out_year != nullptr)
            {
                *out_year = year;
            }
            if (out_serial != nullptr)
            {
                *out_serial = serial;
            }

            return true;
        }

    } // namespace core
} // namespace everblu

#endif // METER_CODE_PARSER_H