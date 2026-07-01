/**
 * @file fake_cc1101.h
 * @brief Test control surface for the simulated CC1101 chip (native_hal).
 */
#ifndef NATIVE_HAL_FAKE_CC1101_H
#define NATIVE_HAL_FAKE_CC1101_H

#include <cstddef>
#include <cstdint>

// Reset the simulated chip to power-on defaults (empty FIFOs, no queued frames).
void fake_cc1101_reset();

// Queue a raw stage-2 on-air byte stream to be delivered by the next data-frame
// reception. Call once for the ACK frame, then once for the DATA frame.
void fake_cc1101_queue_stage2(const uint8_t *data, size_t len);

#endif // NATIVE_HAL_FAKE_CC1101_H
