/*
 * Unit and Integration Tests for MIDI Easing Proxy
 *
 * Tests the latch/unlatch logic and easing behavior without requiring
 * actual MIDI hardware by directly calling the processing functions.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

// Test framework
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

#define TEST(name) \
    void name(); \
    struct name##_registrar { \
        name##_registrar() { test_registry.push_back({#name, name}); } \
    } name##_instance; \
    void name()

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                      << " Expected " << (expected) << " but got " << (actual) << std::endl; \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                      << " Expected true but got false" << std::endl; \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                      << " Expected false but got true" << std::endl; \
            tests_failed++; \
            return; \
        } \
    } while(0)

struct TestCase {
    std::string name;
    void (*func)();
};
std::vector<TestCase> test_registry;

// ============================================================================
// Mock structures matching the main program
// ============================================================================

struct Config {
    int easing_threshold = 3;
    int update_rate_hz = 100;
    uint32_t default_easing_duration = 5000;
    bool quiet = true;  // Suppress output during tests
};

struct ControlState {
    uint8_t last_midimix_value = 0;
    uint8_t last_modulaser_value = 0;
    uint8_t current_target = 0;
    double easing_start_time = 0.0;
    uint32_t easing_duration = 5000;
    bool is_easing = false;
    uint8_t last_sent_value = 0;
    bool modulaser_value_known = false;
    bool is_latched = false;  // Match the real code's default
    uint8_t last_controller_value = 0;
    bool controller_value_known = false;  // Track if we've received a controller value
    uint8_t recent_send_min = 0;
    uint8_t recent_send_max = 0;
    double last_send_time = 0.0;
};

Config test_config;
ControlState test_state;
std::vector<std::pair<uint8_t, uint8_t>> sent_messages;  // (control, value) pairs

double getCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
}

void sendMidiCC(uint8_t control, uint8_t value) {
    sent_messages.push_back({control, value});

    // Update send tracking for echo detection
    ControlState& state = test_state;
    double now_ms = getCurrentTimeMs();

    // Reset range if this is a new send window (>500ms since last send)
    if (state.last_send_time == 0 || (now_ms - state.last_send_time) > 500.0) {
        state.recent_send_min = value;
        state.recent_send_max = value;
    } else {
        // Expand range to include this value
        if (value < state.recent_send_min) state.recent_send_min = value;
        if (value > state.recent_send_max) state.recent_send_max = value;
    }

    state.last_send_time = now_ms;
}

// ============================================================================
// Test helper functions
// ============================================================================

void resetTestState() {
    test_state = ControlState();
    sent_messages.clear();
    test_config = Config();
}

// Simulate processing a Modulaser CC message
void simulateModulaserMessage(uint8_t control, uint8_t value) {
    ControlState& state = test_state;

    // If currently easing, ignore echoes
    if (state.is_easing) {
        state.last_modulaser_value = value;
        state.modulaser_value_known = true;
        return;
    }

    // Check if this is an echo of something we recently sent
    double now_ms = getCurrentTimeMs();
    double echo_window_ms = 500.0;

    if (state.last_send_time > 0 && (now_ms - state.last_send_time) < echo_window_ms) {
        // Within echo window - check if value is in our recent send range
        int margin = test_config.easing_threshold;
        uint8_t range_min = (state.recent_send_min > margin) ? state.recent_send_min - margin : 0;
        uint8_t range_max = (state.recent_send_max + margin <= 127) ? state.recent_send_max + margin : 127;

        if (value >= range_min && value <= range_max) {
            // This is likely an echo - ignore it
            state.last_modulaser_value = value;
            state.modulaser_value_known = true;
            if (!test_config.quiet) {
                std::cout << "[ECHO] Ignoring (in range " << (int)range_min
                          << "-" << (int)range_max << ", " << (int)(now_ms - state.last_send_time)
                          << "ms ago)" << std::endl;
            }
            return;
        }
    } else if (state.last_send_time > 0) {
        // Echo window expired - reset the range
        state.recent_send_min = 0;
        state.recent_send_max = 0;
    }

    // Check if this new Modulaser value is far from the last controller value
    // This applies even on the FIRST Modulaser message (when modulaser_value_known is false)
    if (state.controller_value_known) {
        int diff = std::abs((int)value - (int)state.last_controller_value);
        if (diff >= test_config.easing_threshold) {
            // Modulaser value has changed significantly, unlatch
            if (state.is_latched) {
                state.is_latched = false;
                if (!test_config.quiet) {
                    std::cout << "[UNLATCH] CC" << (int)control
                              << " - Modulaser value " << (int)value
                              << " differs from controller " << (int)state.last_controller_value
                              << " by " << diff << std::endl;
                }
            }
        } else {
            // Values are close, ensure we're latched
            if (!state.is_latched) {
                state.is_latched = true;
                if (!test_config.quiet) {
                    std::cout << "[LATCH] CC" << (int)control
                              << " - Modulaser and controller values are close" << std::endl;
                }
            }
        }
    }

    state.last_modulaser_value = value;
    state.modulaser_value_known = true;
}

// Simulate processing a MidiMix CC message (simplified version)
bool simulateMidiMixMessage(uint8_t control, uint8_t value) {
    ControlState& state = test_state;
    uint8_t previous_controller_value = state.last_controller_value;
    state.last_midimix_value = value;

    // Handle first controller movement - initialize position without latching
    if (!state.controller_value_known) {
        state.last_controller_value = value;
        state.controller_value_known = true;

        // If we don't know Modulaser's value yet, send immediately and latch
        if (!state.modulaser_value_known) {
            sendMidiCC(control, value);
            state.last_sent_value = value;
            state.is_latched = true;  // Latch immediately on first move
            return true;
        }

        // If we DO know Modulaser's value, check if we're close enough to latch
        int diff = std::abs((int)value - (int)state.last_modulaser_value);
        if (diff < test_config.easing_threshold) {
            // Close enough - latch immediately
            state.is_latched = true;
            if (!test_config.quiet) {
                std::cout << "[LATCH] First movement close to Modulaser value" << std::endl;
            }
            // Fall through to normal sending logic
        } else {
            // Not close - wait for crossover
            state.is_latched = false;
            if (!test_config.quiet) {
                std::cout << "[WAITING] First movement not close to Modulaser" << std::endl;
            }
            return false;
        }
    } else {
        // Update controller position for subsequent movements
        state.last_controller_value = value;
    }

    // LATCH/PICKUP MODE LOGIC
    if (!state.is_latched) {
        // Not latched - check if controller has crossed the Modulaser value
        uint8_t modulaser_val = state.last_modulaser_value;
        bool crossed = false;

        if (previous_controller_value < modulaser_val && value >= modulaser_val) {
            crossed = true;  // Crossed from below
        } else if (previous_controller_value > modulaser_val && value <= modulaser_val) {
            crossed = true;  // Crossed from above
        }

        if (crossed) {
            state.is_latched = true;
            if (!test_config.quiet) {
                std::cout << "[LATCH] CC" << (int)control
                          << " - Controller crossed Modulaser value" << std::endl;
            }
            // Fall through to send logic
        } else {
            // Haven't crossed yet - don't send anything
            if (!test_config.quiet) {
                std::cout << "[WAITING] CC" << (int)control
                          << " - Waiting for crossover (Modulaser: " << (int)modulaser_val
                          << ", Controller: " << (int)value << ")" << std::endl;
            }
            return false;  // Didn't send
        }
    }

    // NORMAL EASING LOGIC (when latched)
    if (state.is_easing) {
        // Already easing - just update the target
        state.current_target = value;
        return false;  // Didn't send directly (easing will send)
    } else {
        // Check threshold
        int diff = std::abs((int)value - (int)state.last_modulaser_value);
        if (diff < test_config.easing_threshold) {
            // Below threshold, send immediately
            sendMidiCC(control, value);
            state.last_modulaser_value = value;
            state.last_sent_value = value;
            return true;
        } else {
            // Start NEW easing
            state.current_target = value;
            state.easing_start_time = getCurrentTimeMs();
            state.is_easing = true;
            return false;  // Didn't send directly (easing will send)
        }
    }
}

// ============================================================================
// Unit Tests: Latch/Unlatch Logic
// ============================================================================

TEST(test_initial_state_is_unlatched) {
    resetTestState();
    ASSERT_FALSE(test_state.is_latched);
    ASSERT_FALSE(test_state.controller_value_known);
    tests_passed++;
}

TEST(test_first_controller_message_sends_immediately) {
    resetTestState();

    bool sent = simulateMidiMixMessage(19, 50);

    ASSERT_TRUE(sent);
    ASSERT_EQ(1, sent_messages.size());
    ASSERT_EQ(19, sent_messages[0].first);
    ASSERT_EQ(50, sent_messages[0].second);
    tests_passed++;
}

TEST(test_unlatch_when_modulaser_differs_from_controller) {
    resetTestState();

    // Controller sends 50
    simulateMidiMixMessage(19, 50);
    ASSERT_TRUE(test_state.is_latched);

    // Modulaser sends 40 (diff = 10, > threshold of 3)
    simulateModulaserMessage(19, 40);

    ASSERT_FALSE(test_state.is_latched);
    tests_passed++;
}

TEST(test_stay_latched_when_modulaser_close_to_controller) {
    resetTestState();

    // Controller sends 50
    simulateMidiMixMessage(19, 50);
    ASSERT_TRUE(test_state.is_latched);

    // Modulaser sends 51 (diff = 1, < threshold of 3)
    simulateModulaserMessage(19, 51);

    ASSERT_TRUE(test_state.is_latched);
    tests_passed++;
}

TEST(test_wait_for_crossover_when_unlatched) {
    resetTestState();

    // Controller at 50
    simulateMidiMixMessage(19, 50);

    // Modulaser at 40 (unlatches)
    simulateModulaserMessage(19, 40);
    ASSERT_FALSE(test_state.is_latched);

    sent_messages.clear();

    // Controller moves to 45 (hasn't crossed 40 yet from above)
    bool sent = simulateMidiMixMessage(19, 45);

    ASSERT_FALSE(sent);
    ASSERT_EQ(0, sent_messages.size());
    ASSERT_FALSE(test_state.is_latched);
    tests_passed++;
}

TEST(test_latch_on_crossover_from_above) {
    resetTestState();

    // Setup: Controller at 50, Modulaser at 40 (unlatched)
    simulateMidiMixMessage(19, 50);
    simulateModulaserMessage(19, 40);
    ASSERT_FALSE(test_state.is_latched);

    sent_messages.clear();

    // Controller crosses from 45 to 38 (crosses 40 from above)
    simulateMidiMixMessage(19, 45);
    simulateMidiMixMessage(19, 38);

    ASSERT_TRUE(test_state.is_latched);
    tests_passed++;
}

TEST(test_latch_on_crossover_from_below) {
    resetTestState();

    // Setup: Controller at 30, Modulaser at 40 (unlatched)
    simulateMidiMixMessage(19, 30);
    simulateModulaserMessage(19, 40);
    ASSERT_FALSE(test_state.is_latched);

    sent_messages.clear();

    // Controller crosses from 35 to 42 (crosses 40 from below)
    simulateMidiMixMessage(19, 35);
    simulateMidiMixMessage(19, 42);

    ASSERT_TRUE(test_state.is_latched);
    tests_passed++;
}

TEST(test_latch_on_exact_crossover) {
    resetTestState();

    // Setup: Controller at 30, Modulaser at 40 (unlatched)
    simulateMidiMixMessage(19, 30);
    simulateModulaserMessage(19, 40);
    ASSERT_FALSE(test_state.is_latched);

    // Controller hits exact Modulaser value
    simulateMidiMixMessage(19, 40);

    ASSERT_TRUE(test_state.is_latched);
    tests_passed++;
}

// ============================================================================
// Integration Tests: Easing with Echoes
// ============================================================================

TEST(test_large_jump_starts_easing) {
    resetTestState();

    // Initial state: Controller and Modulaser at 50
    simulateMidiMixMessage(19, 50);
    simulateModulaserMessage(19, 50);

    sent_messages.clear();

    // Controller jumps to 30 (diff = 20, > threshold)
    bool sent = simulateMidiMixMessage(19, 30);

    ASSERT_FALSE(sent);  // Should start easing, not send directly
    ASSERT_TRUE(test_state.is_easing);
    ASSERT_EQ(30, test_state.current_target);
    tests_passed++;
}

TEST(test_small_move_sends_immediately) {
    resetTestState();

    // Initial state: Controller and Modulaser at 50
    simulateMidiMixMessage(19, 50);
    simulateModulaserMessage(19, 50);

    sent_messages.clear();

    // Controller moves to 51 (diff = 1, < threshold)
    bool sent = simulateMidiMixMessage(19, 51);

    ASSERT_TRUE(sent);
    ASSERT_FALSE(test_state.is_easing);
    ASSERT_EQ(1, sent_messages.size());
    ASSERT_EQ(51, sent_messages[0].second);
    tests_passed++;
}

TEST(test_echo_during_easing_should_not_unlatch) {
    resetTestState();

    // Setup: Easing from 40 to 36
    simulateMidiMixMessage(19, 40);
    simulateModulaserMessage(19, 40);
    simulateMidiMixMessage(19, 36);  // Starts easing
    ASSERT_TRUE(test_state.is_easing);
    ASSERT_TRUE(test_state.is_latched);

    // Modulaser echoes back 33 (the eased value, differs by 3 from target 36)
    // This SHOULD NOT cause unlatch because it's just an echo
    simulateModulaserMessage(19, 33);

    // DESIRED BEHAVIOR: Should stay latched (echo should be ignored)
    ASSERT_TRUE(test_state.is_latched);  // This is what we WANT
    tests_passed++;
}

TEST(test_rapid_controller_movement_with_lagging_echoes) {
    resetTestState();
    test_config.quiet = false;  // Show debug output for this test

    std::cout << "\n=== Simulating rapid controller movement ===" << std::endl;

    // Initial position
    simulateMidiMixMessage(19, 41);
    simulateModulaserMessage(19, 41);

    // Rapid controller movements (from your log)
    std::cout << "Controller: 41 -> 39 -> 38 -> 36" << std::endl;
    simulateMidiMixMessage(19, 39);
    simulateMidiMixMessage(19, 38);

    // Lagging echo arrives
    std::cout << "Modulaser echo: 40 (lagging)" << std::endl;
    simulateModulaserMessage(19, 40);

    simulateMidiMixMessage(19, 36);  // Final position

    std::cout << "Controller: 36" << std::endl;
    std::cout << "Easing state: " << (test_state.is_easing ? "true" : "false") << std::endl;
    std::cout << "Latched: " << (test_state.is_latched ? "true" : "false") << std::endl;

    // Later, another echo arrives during easing
    std::cout << "Modulaser echo: 33 (eased value)" << std::endl;
    simulateModulaserMessage(19, 33);

    std::cout << "Latched after echo: " << (test_state.is_latched ? "true" : "false") << std::endl;

    ASSERT_TRUE(test_state.is_latched);

    test_config.quiet = true;
    tests_passed++;
}

TEST(test_rapid_small_movements_with_lagging_echoes) {
    resetTestState();
    test_config.quiet = false;

    std::cout << "\n=== Simulating CC24 scenario (rapid small movements) ===" << std::endl;

    // Start at 49 (after easing completes)
    simulateMidiMixMessage(24, 49);
    simulateModulaserMessage(24, 49);
    ASSERT_EQ(49, test_state.last_sent_value);

    // Rapid small movements: 50, 51, 52, 53, 54
    std::cout << "Controller: 50, 51, 52, 53, 54" << std::endl;
    simulateMidiMixMessage(24, 50);  // Sends immediately (diff=1)
    simulateMidiMixMessage(24, 51);  // Sends immediately
    simulateMidiMixMessage(24, 52);  // Sends immediately
    simulateMidiMixMessage(24, 53);  // Sends immediately
    simulateMidiMixMessage(24, 54);  // Sends immediately
    ASSERT_EQ(54, test_state.last_sent_value);
    ASSERT_EQ(54, test_state.last_controller_value);
    ASSERT_TRUE(test_state.is_latched);

    // Lagging echoes arrive: 50, 51
    std::cout << "Modulaser echoes: 50, 51 (lagging)" << std::endl;
    simulateModulaserMessage(24, 50);  // Should be ignored (diff from last_sent 54 is 4, but close to earlier sends)
    ASSERT_TRUE(test_state.is_latched);  // Should stay latched!

    simulateModulaserMessage(24, 51);  // Should be ignored
    ASSERT_TRUE(test_state.is_latched);  // Should stay latched!

    std::cout << "Final latched state: " << (test_state.is_latched ? "true" : "false") << std::endl;

    test_config.quiet = true;
    tests_passed++;
}

// ============================================================================
// Test Runner
// ============================================================================

int main() {
    std::cout << "MIDI Easing Proxy - Test Suite" << std::endl;
    std::cout << "==============================\n" << std::endl;

    for (const auto& test : test_registry) {
        tests_run++;
        std::cout << "Running: " << test.name << "... ";
        test.func();
        if (tests_failed == tests_run - tests_passed) {
            std::cout << "PASS" << std::endl;
        }
    }

    std::cout << "\n==============================" << std::endl;
    std::cout << "Tests run: " << tests_run << std::endl;
    std::cout << "Passed: " << tests_passed << std::endl;
    std::cout << "Failed: " << tests_failed << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
