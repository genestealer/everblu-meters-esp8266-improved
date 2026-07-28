/**
 * @file test_hex_dump.cpp
 * @brief Bounds tests for the hex/binary dump helpers in core/utils.cpp
 *
 * These functions format into a fixed stack buffer. They are only reachable
 * with diagnostics enabled, so a defect here never shows up in normal use, but
 * cc1101.cpp calls show_in_hex_one_line() with a whole received frame. The
 * tests below drive every mode with buffers large enough to run past the end of
 * that stack buffer if the wrapping is ever removed.
 *
 * The native build compiles with -fstack-protector-all, so an overrun aborts
 * the test binary rather than silently corrupting the stack.
 *
 * Test registration lives in test_runner.cpp.
 */

#include <unity.h>

#include <stdint.h>
#include <vector>

#include "core/utils.h"

namespace
{
    /// A buffer far longer than any single formatted line can hold.
    std::vector<uint8_t> makeBuffer(size_t len)
    {
        std::vector<uint8_t> buffer(len);
        for (size_t i = 0; i < len; i++)
        {
            buffer[i] = (uint8_t)(i * 31 + 7);
        }
        return buffer;
    }
}

void test_hex_dump_handles_a_full_radian_frame(void)
{
    // 124 bytes is the real frame length, and at three characters each that is
    // 372 characters: more than the formatter's line buffer holds.
    std::vector<uint8_t> frame = makeBuffer(124);

    show_in_hex(frame.data(), frame.size());
    show_in_hex_array(frame.data(), frame.size());
    show_in_hex_one_line(frame.data(), frame.size());
    show_in_hex_one_line_GET(frame.data(), frame.size());

    TEST_PASS();
}

void test_hex_dump_handles_an_oversized_buffer(void)
{
    // cc1101.cpp passes whatever length the radio reported, so the formatter
    // must stay in bounds for a buffer of any size.
    std::vector<uint8_t> big = makeBuffer(1024);

    show_in_hex(big.data(), big.size());
    show_in_hex_array(big.data(), big.size());
    show_in_hex_one_line(big.data(), big.size());
    show_in_hex_one_line_GET(big.data(), big.size());

    TEST_PASS();
}

void test_hex_dump_handles_empty_and_null(void)
{
    const uint8_t one[1] = {0xA5};

    show_in_hex(one, 0);
    show_in_hex_array(one, 0);
    show_in_hex_one_line(one, 0);
    show_in_hex_one_line_GET(one, 0);

    show_in_hex(nullptr, 0);
    show_in_hex_one_line(nullptr, 16);

    TEST_PASS();
}

void test_hex_dump_handles_exact_line_boundaries(void)
{
    // Lengths either side of the internal 16-byte wrap and of the point where
    // the line buffer fills, which is where an off-by-one would land.
    static const size_t lengths[] = {1, 15, 16, 17, 31, 32, 33, 84, 85, 86, 127, 128, 129};

    for (size_t len : lengths)
    {
        std::vector<uint8_t> buffer = makeBuffer(len);
        show_in_hex(buffer.data(), len);
        show_in_hex_array(buffer.data(), len);
        show_in_hex_one_line(buffer.data(), len);
        show_in_hex_one_line_GET(buffer.data(), len);
    }

    TEST_PASS();
}

void test_binary_dump_handles_a_full_radian_frame(void)
{
    // Nine characters per byte, so this overflows even sooner than hex.
    std::vector<uint8_t> frame = makeBuffer(124);

    show_in_bin(frame.data(), frame.size());
    show_in_bin(nullptr, 8);

    TEST_PASS();
}
