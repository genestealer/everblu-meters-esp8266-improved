#include <Arduino.h>
#include <unity.h>
#include "utils.h"

void setUp() {}
void tearDown() {}

void test_crc_kermit_standard_vector() {
  const unsigned char s[] = "123456789";
  uint16_t crc = crc_kermit(s, 9);
  TEST_ASSERT_EQUAL_HEX16(0x2189, crc);
}

void test_crc_kermit_empty_buffer() {
  const unsigned char s[] = "";
  uint16_t crc = crc_kermit(s, 0);
  TEST_ASSERT_EQUAL_HEX16(0x0000, crc);
}

void setup() {
  delay(2000); // allow serial to settle
  UNITY_BEGIN();
  RUN_TEST(test_crc_kermit_standard_vector);
  RUN_TEST(test_crc_kermit_empty_buffer);
  UNITY_END();
}

void loop() {
  // not used in unit tests
}
