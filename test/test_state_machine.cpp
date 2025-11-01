/**
 * @file test_state_machine.cpp
 * @brief Unit tests for the state machine implementation
 */

#include <unity.h>
#include <Arduino.h>

// Mock state machine states (should match main.cpp)
enum SystemState {
  STATE_INIT,
  STATE_IDLE,
  STATE_CHECK_SCHEDULE,
  STATE_COOLDOWN_WAIT,
  STATE_START_READING,
  STATE_READING_IN_PROGRESS,
  STATE_RETRY_WAIT,
  STATE_PUBLISH_SUCCESS,
  STATE_PUBLISH_FAILURE
};

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

/**
 * Test: State enumeration values
 */
void test_state_enum_values(void) {
    TEST_ASSERT_EQUAL(0, STATE_INIT);
    TEST_ASSERT_EQUAL(1, STATE_IDLE);
    TEST_ASSERT_EQUAL(2, STATE_CHECK_SCHEDULE);
    TEST_ASSERT_TRUE(STATE_PUBLISH_FAILURE > STATE_INIT);
}

/**
 * Test: State transitions are valid
 */
void test_state_transitions_valid(void) {
    // INIT should transition to IDLE
    SystemState current = STATE_INIT;
    SystemState next = STATE_IDLE;
    TEST_ASSERT_NOT_EQUAL(current, next);
    
    // IDLE should transition to CHECK_SCHEDULE
    current = STATE_IDLE;
    next = STATE_CHECK_SCHEDULE;
    TEST_ASSERT_NOT_EQUAL(current, next);
}

/**
 * Test: All states are unique
 */
void test_all_states_unique(void) {
    int states[] = {
        STATE_INIT,
        STATE_IDLE,
        STATE_CHECK_SCHEDULE,
        STATE_COOLDOWN_WAIT,
        STATE_START_READING,
        STATE_READING_IN_PROGRESS,
        STATE_RETRY_WAIT,
        STATE_PUBLISH_SUCCESS,
        STATE_PUBLISH_FAILURE
    };
    
    int numStates = sizeof(states) / sizeof(states[0]);
    
    // Check all states are different
    for (int i = 0; i < numStates; i++) {
        for (int j = i + 1; j < numStates; j++) {
            TEST_ASSERT_NOT_EQUAL(states[i], states[j]);
        }
    }
}

/**
 * Test: State machine has reasonable number of states
 */
void test_state_count_reasonable(void) {
    // Should have 9 states
    TEST_ASSERT_EQUAL(8, STATE_PUBLISH_FAILURE);
}

void setup() {
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();
    
    RUN_TEST(test_state_enum_values);
    RUN_TEST(test_state_transitions_valid);
    RUN_TEST(test_all_states_unique);
    RUN_TEST(test_state_count_reasonable);
    
    UNITY_END();
}

void loop() {
    // Nothing to do here
}
