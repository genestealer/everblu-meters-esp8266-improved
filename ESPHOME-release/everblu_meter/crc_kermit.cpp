#include "crc_kermit.h"

static bool crc_tab_init = false;
static uint16_t crc_tab[256];

static void init_crc_tab(void)
{
    uint16_t crc;
    uint16_t c;

    for (int i = 0; i < 256; i++)
    {
        crc = 0;
        c = (uint16_t)i;

        for (int j = 0; j < 8; j++)
        {
            if ((crc ^ c) & 0x0001)
            {
                crc = (crc >> 1) ^ 0x8408;
            }
            else
            {
                crc = crc >> 1;
            }
            c = c >> 1;
        }

        crc_tab[i] = crc;
    }

    crc_tab_init = true;
}

uint16_t crc_kermit(const uint8_t *input_ptr, size_t num_bytes)
{
    if (!crc_tab_init)
    {
        init_crc_tab();
    }

    uint16_t crc = 0x0000;
    const uint8_t *ptr = input_ptr;

    for (size_t i = 0; i < num_bytes; i++)
    {
        const uint16_t short_c = (uint16_t)(0x00ff & (uint16_t)*ptr);
        const uint16_t tmp = crc ^ short_c;
        crc = (crc >> 8) ^ crc_tab[tmp & 0xff];
        ptr++;
    }

    const uint16_t low_byte = (crc & 0xff00) >> 8;
    const uint16_t high_byte = (crc & 0x00ff) << 8;
    return (uint16_t)(low_byte | high_byte);
}