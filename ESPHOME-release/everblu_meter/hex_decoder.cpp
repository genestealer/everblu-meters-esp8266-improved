/**
 * @file hex_decoder.cpp
 * @brief Development tool: decode a RADIAN hex frame dump to meter values.
 *
 * Usage
 * -----
 * Build with PlatformIO:
 *   pio run -e hex_decoder
 *
 * Run the resulting binary and paste one or more hex lines at the prompt.
 * Each line may be:
 *   a) A bare hex dump:       7C 11 00 45 20 0A 50 54 ...
 *   b) A full log line:       [boot+263s][D][everblu_meter] 7C 11 00 45 ...
 *
 * The tool tries two decode strategies for each input:
 *   1. Direct parse  — treats the bytes as already-decoded RADIAN frame data
 *      (this is what the firmware logs during the frequency scan when
 *       [D][everblu_meter] hex dumps appear).
 *   2. 4× oversampled decode — runs radian_decode_4bitpbit() first (for raw
 *      CC1101 FIFO bytes captured at 9.6 kbps).
 *
 * Type 'quit' or press Ctrl+D / Ctrl+Z (EOF) to exit.
 */

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "radian_decoder.h"
#include "radian_parser.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Extract space-separated two-digit hex tokens from a log line. */
static bool parse_hex_line(const std::string &line, std::vector<uint8_t> &out)
{
    out.clear();
    std::stringstream ss(line);
    std::string tok;

    while (ss >> tok)
    {
        // Accept only 1- or 2-character tokens that are entirely hex digits.
        if (tok.size() == 0 || tok.size() > 2)
            continue;

        bool all_hex = true;
        for (char c : tok)
        {
            if (!isxdigit(static_cast<unsigned char>(c)))
            {
                all_hex = false;
                break;
            }
        }
        if (!all_hex)
            continue;

        char   *end = nullptr;
        long    val = std::strtol(tok.c_str(), &end, 16);
        if (end && *end == '\0' && val >= 0 && val <= 255)
            out.push_back(static_cast<uint8_t>(val));
    }

    return !out.empty();
}

static void print_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n       ");
    }
    printf("\n");
}

static void print_separator()
{
    printf("%-60s\n", "------------------------------------------------------------");
}

// ---------------------------------------------------------------------------
// Decode and display one candidate buffer
// ---------------------------------------------------------------------------

static void display_parse_result(const uint8_t *buf, size_t len, const char *label)
{
    printf("\n  [%s -- %zu bytes]\n", label, len);
    printf("  Bytes : ");
    print_bytes(buf, len);

    // ---- CRC check --------------------------------------------------------
    bool crc_ok = radian_validate_crc(buf, len);
    if (len >= 4)
    {
        uint8_t length_field = buf[0];
        size_t  expected_len = length_field ? (size_t)length_field : len;
        if (expected_len > len)
    printf("  CRC   : INDETERMINATE -- length field (0x%02X=%u) > buffer (%zu bytes); "
                   "cannot verify trailer\n",
                   length_field, length_field, len);
        else
            printf("  CRC   : %s\n", crc_ok ? "VALID ✓" : "INVALID ✗");
    }
    else
    {
        printf("  CRC   : SKIP -- too short (< 4 bytes)\n");
    }

    // ---- Primary data parse -----------------------------------------------
    struct radian_primary_data data;
    bool parsed = radian_parse_primary_data(buf, len, &data);

    if (!parsed)
    {
        if (len < 30)
            printf("  Parse : FAIL -- need at least 30 bytes, have %zu\n", len);
        else
            printf("  Parse : FAIL -- volume field is 0, 0xFFFFFFFF, or exceeds "
                   "plausibility limit (>1 billion L)\n");
        return;
    }

    printf("  Parse : OK\n");
    printf("  +---------------------------------------------\n");
    printf("  | Volume         : %10u L  (%.3f m3)\n",
           data.volume, data.volume / 1000.0);

    if (len >= 49)
    {
        printf("  | Reads counter  : %u\n",         data.reads_counter);
        printf("  | Battery left   : %u months\n",  data.battery_left);
        printf("  | Wake window    : %02u:00 - %02u:00 UTC\n",
               data.time_start, data.time_end);
    }
    else
    {
        printf("  | Reads / battery / wake window: N/A (need 49+ bytes, have %zu)\n",
               len);
    }

    printf("  | History data   : %s\n",
           data.history_available ? "available (>=118 bytes)" : "not available");
    printf("  +---------------------------------------------\n");
}

// ---------------------------------------------------------------------------
// Process one input line
// ---------------------------------------------------------------------------

static void process_line(const std::string &line)
{
    std::vector<uint8_t> bytes;
    if (!parse_hex_line(line, bytes))
    {
        fprintf(stderr, "  [!] No hex bytes found in input.\n");
        return;
    }

    printf("\nInput  : %zu bytes\n", bytes.size());

    // Strategy 1: treat bytes as already-decoded RADIAN frame data.
    // This is the format logged by [D][everblu_meter] during a frequency scan
    // (the firmware dumps meter_data[] after decode_4bitpbit_serial).
    display_parse_result(bytes.data(), bytes.size(),
                         "Strategy 1: direct -- bytes are decoded frame data");

    // Strategy 2: treat bytes as a raw 4x oversampled CC1101 FIFO stream and
    // run the RADIAN bitstream decoder first.
    {
        const int     MAX_DECODED = 200;
        uint8_t       decoded_buf[MAX_DECODED];
        memset(decoded_buf, 0, sizeof(decoded_buf));

        uint8_t decoded_len = radian_decode_4bitpbit(
            bytes.data(), static_cast<int>(bytes.size()),
            decoded_buf, MAX_DECODED);

        if (decoded_len == 0)
        {
            printf("\n  [Strategy 2: 4x oversampled decode]\n");
            printf("  Result: 0 bytes decoded -- input does not appear to be an "
                   "oversampled stream (or too short / too many framing errors).\n");
        }
        else
        {
            display_parse_result(decoded_buf, decoded_len,
                                 "Strategy 2: 4x oversampled bitstream decoded");
        }
    }

    printf("\n");
    print_separator();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main()
{
    printf("==========================================================\n");
    printf("   EverBlu RADIAN Hex Frame Decoder -- Development Tool  \n");
    printf("==========================================================\n");
    printf("\n");
    printf("Paste a hex dump from the serial log (bare bytes or full log line).\n");
    printf("Example:\n");
    printf("  7C 11 00 45 20 0A 50 54 00 00 00 00 00 00 08 58 ...\n");
    printf("  [boot+263s][D][everblu_meter] 7C 11 00 45 20 ...\n");
    printf("\nType 'quit' or send EOF (Ctrl+D / Ctrl+Z) to exit.\n");
    print_separator();

    std::string line;
    while (true)
    {
        printf("\n> ");
        fflush(stdout);

        if (!std::getline(std::cin, line))
            break; /* EOF */

        // Trim leading whitespace
        size_t start = 0;
        while (start < line.size() && isspace(static_cast<unsigned char>(line[start])))
            start++;
        std::string trimmed = line.substr(start);

        if (trimmed.empty())
            continue;

        if (trimmed == "quit" || trimmed == "exit" || trimmed == "q")
            break;

        process_line(trimmed);
    }

    printf("\nBye.\n");
    return 0;
}
