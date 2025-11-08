/*
 * MIDI Easing Proxy
 *
 * Sits between MidiMix controller and Modulaser software to apply
 * smooth easing transitions when control values jump after preset changes.
 *
 * Copyright (c) 2025
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <map>
#include <vector>
#include <set>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <cmath>
#include <csignal>
#include <unistd.h> // For isatty()
#include "vendor/rtmidi-6.0.0/RtMidi.h"
#include "vendor/AHEasing/easing.h"
#include "led_controller.h"

// ============================================================================
// Configuration Structure
// ============================================================================

struct Config {
    std::string input_port_name = "MIDI Mix";
    std::string virtual_port_name = "Midi Easing";
    std::set<uint8_t> whitelist_controls;
    int update_rate_hz = 100;
    int easing_threshold = 3; // Used for echo detection margin
    bool quiet = false;

    // EMA Smoothing Configuration
    float default_alpha_min = 0.25f;      // Heavy smoothing for slow movements
    float default_alpha_max = 0.8f;       // Light smoothing for fast movements
    uint8_t velocity_threshold_low = 2;   // Below this: use alpha_min
    uint8_t velocity_threshold_high = 10; // Above this: use alpha_max
    bool adaptive_enabled = true;         // Use velocity-based adaptive alpha

    // Per-control EMA settings: control -> (alpha_min, alpha_max, vel_low, vel_high, adaptive)
    struct SmoothingParams {
      float alpha_min;
      float alpha_max;
      uint8_t velocity_threshold_low;
      uint8_t velocity_threshold_high;
      bool adaptive_enabled;
    };
    std::map<uint8_t, SmoothingParams> smoothing_per_control;
};

// ============================================================================
// Control State Structure
// ============================================================================

struct ControlState {
  // Common state
  uint8_t last_sent_value = 0;
  uint8_t last_modulaser_value = 0; // Track value from Modulaser echo (for latch logic)
  bool modulaser_value_known = false;
  bool is_latched = false;           // Start unlatched
  uint8_t last_controller_value = 0; // Track controller position for crossover detection
  bool controller_value_known = false; // Track if we've received a value from controller yet
  uint8_t recent_send_min = 0;       // Track minimum recently sent value for echo detection
  uint8_t recent_send_max = 0;       // Track maximum recently sent value for echo detection
  double last_send_time = 0.0;       // Timestamp of last send for echo window

  // EMA smoothing state
  float smoothed_value = 0.0f;   // Current smoothed output value
  uint8_t last_raw_value = 0;    // Last raw input value (for velocity calc)
  uint8_t target_value = 0;      // Target value to converge to
  double last_update_time = 0.0; // For time-based velocity calculation
  float current_alpha = 0.5f;    // Current alpha value (adaptive or fixed)
  bool is_smoothing = false;     // True if smoothed_value hasn't converged to target yet

  // EMA configuration (per-control, initialized from Config)
  float alpha_min = 0.25f;              // Alpha for slow movements
  float alpha_max = 0.8f;               // Alpha for fast movements
  uint8_t velocity_threshold_low = 2;   // Below this: use alpha_min
  uint8_t velocity_threshold_high = 10; // Above this: use alpha_max
  bool adaptive_enabled = true;         // Use adaptive alpha
};

// ============================================================================
// Global State
// ============================================================================

Config config;
std::map<uint8_t, ControlState> control_states;
std::mutex state_mutex;
std::atomic<bool> running(true);
std::atomic<bool> midimix_connected(false);

// Track waiting controls for quiet mode status box
struct WaitingControl {
  uint8_t modulaser_val;
  uint8_t controller_val;
};
std::map<uint8_t, WaitingControl> waiting_controls;
std::mutex waiting_mutex;
int last_box_lines = 0; // Track number of lines in previous status box

// Track last activity for "Ready" state display
struct LastActivity {
  uint8_t control = 0;
  uint8_t value = 0;
  bool has_activity = false;
};
LastActivity last_activity;

RtMidiIn *fromModulaser = nullptr;

// ============================================================================
// ANSI Color Codes for Terminal Output
// ============================================================================

namespace ANSIColor {
// Check if output is a TTY (supports colors)
const bool enabled = isatty(STDOUT_FILENO);

// Color codes (only used if TTY detected)
const char *RESET = enabled ? "\033[0m" : "";
const char *BOLD = enabled ? "\033[1m" : "";
const char *DIM = enabled ? "\033[2m" : "";

// Foreground colors
const char *RED = enabled ? "\033[31m" : "";
const char *GREEN = enabled ? "\033[32m" : "";
const char *YELLOW = enabled ? "\033[33m" : "";
const char *BLUE = enabled ? "\033[34m" : "";
const char *MAGENTA = enabled ? "\033[35m" : "";
const char *CYAN = enabled ? "\033[36m" : "";
const char *WHITE = enabled ? "\033[37m" : "";

// Bright colors
const char *BRIGHT_RED = enabled ? "\033[91m" : "";
const char *BRIGHT_GREEN = enabled ? "\033[92m" : "";
const char *BRIGHT_YELLOW = enabled ? "\033[93m" : "";
const char *BRIGHT_BLUE = enabled ? "\033[94m" : "";
const char *BRIGHT_MAGENTA = enabled ? "\033[95m" : "";
const char *BRIGHT_CYAN = enabled ? "\033[96m" : "";
} // namespace ANSIColor

// Get color for log action type
const char *getActionColor(const std::string &action) {
  if (action == "INFO")
    return ANSIColor::BRIGHT_BLUE;
  if (action == "WAITING")
    return ANSIColor::BRIGHT_YELLOW;
  if (action == "LATCH")
    return ANSIColor::BRIGHT_GREEN;
  if (action == "UNLATCH")
    return ANSIColor::YELLOW;
  if (action == "SMOOTH")
    return ANSIColor::CYAN;
  if (action == "CONVERGE")
    return ANSIColor::GREEN;
  if (action == "CONTROL")
    return ANSIColor::DIM;
  if (action == "ECHO")
    return ANSIColor::MAGENTA;
  if (action == "WHITELIST")
    return ANSIColor::BRIGHT_CYAN;
  if (action == "MAPPING")
    return ANSIColor::BLUE;
  if (action == "LED")
    return ANSIColor::DIM;
  return ANSIColor::WHITE;
}

// Get symbol for log action type
const char *getActionSymbol(const std::string &action) {
  if (action == "INFO")
    return "ℹ";
  if (action == "WAITING")
    return "⏳";
  if (action == "LATCH")
    return "✓";
  if (action == "UNLATCH")
    return "⚠";
  if (action == "SMOOTH")
    return "〰";
  if (action == "CONVERGE")
    return "✓";
  if (action == "CONTROL")
    return "→";
  if (action == "ECHO")
    return "↩";
  if (action == "WHITELIST")
    return "⚡";
  if (action == "MAPPING")
    return "⇄";
  if (action == "LED")
    return "💡";
  return "•";
}

// ============================================================================
// MIDI I/O Objects
// ============================================================================

RtMidiOut *toModulaser = nullptr;
RtMidiIn *fromMidiMix = nullptr;
RtMidiOut *toMidiMix = nullptr;  // For LED control
LEDController *ledController = nullptr;

// ============================================================================
// Utility Functions
// ============================================================================

double getCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
}

// ============================================================================
// Quiet Mode Status Box
// ============================================================================

void displayStatusBox() {
  if (!config.quiet)
    return;

  std::lock_guard<std::mutex> lock(waiting_mutex);

  // Move cursor up to overwrite previous box
  if (last_box_lines > 0) {
    // Move cursor up by number of lines in previous box
    std::cout << "\033[" << last_box_lines << "A";
    // Clear from cursor to end of screen
    std::cout << "\033[J";
  }

  int current_lines = 0;

  if (waiting_controls.empty()) {
    // No waiting controls - show ready state with last activity
    std::cout << ANSIColor::GREEN << "╔════════════════════════════════════════════╗\n";

    if (last_activity.has_activity) {
      // Show last sent control
      std::ostringstream ready_line;
      ready_line << "║ " << ANSIColor::BOLD << "LAST SENT: CC" << std::setw(2) << (int)last_activity.control << ANSIColor::RESET
                 << ANSIColor::GREEN << " → " << std::setw(3) << (int)last_activity.value;

      std::string line_str = ready_line.str();
      // Calculate padding: "║ LAST SENT: CC19 → 100" = ~22 visible chars
      int visible_chars = 22 + (last_activity.control >= 10 ? 1 : 0) + (last_activity.value >= 100 ? 1 : 0);
      int padding = 45 - visible_chars;

      std::cout << line_str << std::string(padding, ' ') << "║\n";
    } else {
      // No activity yet - show generic ready
      std::cout << "║ " << ANSIColor::BOLD << "STATUS: Ready" << ANSIColor::RESET << ANSIColor::GREEN;
      std::cout << std::string(45 - 15, ' ') << "║\n";
    }

    std::cout << "╚════════════════════════════════════════════╝" << ANSIColor::RESET << "\n";
    current_lines = 3;
  } else {
    // Show waiting controls (3 + number of controls lines)
    std::cout << ANSIColor::YELLOW << "╔════════════════════════════════════════════╗\n";
    current_lines = 1;

    for (const auto &[control, info] : waiting_controls) {
      int diff = std::abs((int)info.modulaser_val - (int)info.controller_val);

      std::ostringstream line;
      line << "║ " << ANSIColor::BOLD << "WAITING: CC" << std::setw(2) << (int)control << ANSIColor::RESET << ANSIColor::YELLOW
           << " → M:" << std::setw(3) << (int)info.modulaser_val << " | C:" << std::setw(3) << (int)info.controller_val << " | Δ"
           << std::setw(2) << diff;

      std::string line_str = line.str();
      // Calculate padding (accounting for ANSI codes which don't display)
      // Base length: "║ WAITING: CC19 → M:45  | C:82  | Δ37" = ~35 chars visible
      int visible_chars = 35 + (control >= 10 ? 1 : 0) + (diff >= 10 ? 1 : 0);
      int padding = 45 - visible_chars;

      std::cout << line_str << std::string(padding, ' ') << "║\n";
      current_lines++;
    }

    std::cout << "╚════════════════════════════════════════════╝" << ANSIColor::RESET << "\n";
    current_lines++;
  }

  last_box_lines = current_lines;
  std::cout.flush();
}

void updateWaitingControl(uint8_t control, uint8_t modulaser_val, uint8_t controller_val) {
  {
    std::lock_guard<std::mutex> lock(waiting_mutex);
    waiting_controls[control] = {modulaser_val, controller_val};
  }
  displayStatusBox();
}

void clearWaitingControl(uint8_t control) {
  {
    std::lock_guard<std::mutex> lock(waiting_mutex);
    waiting_controls.erase(control);
  }
  displayStatusBox();
}

// ============================================================================
// Logging Functions
// ============================================================================

// Unified logging function with timestamp and action tag
void log(const std::string &action, int control, const std::string &message) {
  if (!config.quiet) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", std::localtime(&time));

    // Timestamp in dim gray
    std::cout << ANSIColor::DIM << "[" << buffer << "." << std::setfill('0') << std::setw(3) << ms.count() << "]" << ANSIColor::RESET
              << " ";

    // Action tag with color and symbol
    const char *color = getActionColor(action);
    const char *symbol = getActionSymbol(action);
    std::cout << color << symbol << " " << action << ANSIColor::RESET;

    // Control number in bold white
    if (control >= 0) {
      std::cout << " " << ANSIColor::BOLD << "CC" << control << ANSIColor::RESET;
    }

    // Message
    if (!message.empty()) {
      std::cout << " " << message;
    }

    std::cout << std::endl;
  }
}

// Convenience wrapper for basic message logging
void logMessage(const std::string &source, uint8_t control, uint8_t value) {
  std::ostringstream msg;
  msg << source << " -> " << (int)value;
  log("CONTROL", control, msg.str());
}

// Convenience wrapper for echo messages
void logEcho(uint8_t control, uint8_t value, uint8_t range_min, uint8_t range_max, double time_ago_ms) {
  std::ostringstream msg;
  msg << "Modulaser -> " << (int)value << " (ignoring echo in range " << (int)range_min << "-" << (int)range_max << ", " << (int)time_ago_ms
      << "ms ago)";
  log("ECHO", control, msg.str());
}

void logLatch(uint8_t control, const std::string &reason) {
  log("LATCH", control, reason);
  clearWaitingControl(control); // Remove from waiting status in quiet mode
}

void logWaiting(uint8_t control, uint8_t modulaser_val, uint8_t controller_val) {
  std::ostringstream msg;
  int diff = std::abs((int)modulaser_val - (int)controller_val);
  msg << "waiting for crossover → " << ANSIColor::BRIGHT_MAGENTA << "Modulaser:" << (int)modulaser_val << ANSIColor::RESET << " | "
      << ANSIColor::BRIGHT_CYAN << "Controller:" << (int)controller_val << ANSIColor::RESET << " (Δ" << diff << ")";
  log("WAITING", control, msg.str());
  updateWaitingControl(control, modulaser_val, controller_val); // Update status box in quiet mode
}

void logWhitelist(uint8_t control, uint8_t value) {
  std::ostringstream msg;
  msg << "bypassing easing, sending " << (int)value;
  log("WHITELIST", control, msg.str());
}

void logUnlatch(uint8_t control, uint8_t modulaser_val, uint8_t controller_val, int diff) {
  std::ostringstream msg;
  msg << ANSIColor::BRIGHT_MAGENTA << "Modulaser:" << (int)modulaser_val << ANSIColor::RESET << " far from " << ANSIColor::BRIGHT_CYAN
      << "Controller:" << (int)controller_val << ANSIColor::RESET << " (Δ" << diff << ")";
  log("UNLATCH", control, msg.str());
}

void logLED(uint8_t note, bool on, const std::string &reason) {
  std::ostringstream msg;
  msg << "Note " << (int)note << " -> " << (on ? "ON" : "OFF") << " (" << reason << ")";
  log("LED", -1, msg.str());
}

void logInfo(const std::string &message) { log("INFO", -1, message); }

void logMapping(uint8_t control, uint8_t input_value, uint8_t output_value, const std::string &description) {
  std::ostringstream msg;
  msg << "MidiMix -> " << (int)input_value << " -> " << (int)output_value << " (" << description << ")";
  log("MAPPING", control, msg.str());
}

uint8_t mapValue(uint8_t value, uint8_t in_min, uint8_t in_max, uint8_t out_min, uint8_t out_max) {
    return out_min + (value - in_min) * (out_max - out_min) / (in_max - in_min);
}

// ============================================================================
// EMA Smoothing Functions (New System)
// ============================================================================

// Logging for EMA smoothing (optional debug info)
void logSmoothing(uint8_t control, uint8_t raw, uint8_t smoothed, float alpha, int velocity) {
  std::ostringstream msg;
  msg << "raw=" << (int)raw << " -> smoothed=" << (int)smoothed << " (alpha=" << std::fixed << std::setprecision(2) << alpha
      << ", vel=" << velocity << ")";
  log("SMOOTH", control, msg.str());
}

// Apply EMA smoothing with adaptive alpha based on velocity
uint8_t applyEMASmoothing(ControlState &state, uint8_t raw_value, uint8_t control, const Config &config) {
  // Calculate velocity (rate of change)
  int velocity = std::abs((int)raw_value - (int)state.last_raw_value);

  // Determine alpha (adaptive or fixed)
  float alpha = state.current_alpha; // Default/fixed
  if (state.adaptive_enabled) {
    if (velocity < state.velocity_threshold_low) {
      // Slow movement: Heavy smoothing
      alpha = state.alpha_min;
    } else if (velocity > state.velocity_threshold_high) {
      // Fast movement: Light smoothing (more responsive)
      alpha = state.alpha_max;
    } else {
      // Medium movement: Linear interpolation
      float t = (float)(velocity - state.velocity_threshold_low) / (state.velocity_threshold_high - state.velocity_threshold_low);
      alpha = state.alpha_min + t * (state.alpha_max - state.alpha_min);
    }
  }

  // Initialize smoothed_value on first use
  if (state.smoothed_value == 0.0f && state.last_raw_value == 0) {
    state.smoothed_value = raw_value;
  }

  // Set target value (what we want to converge to)
  state.target_value = raw_value;

  // Apply EMA: smoothed = α × raw + (1-α) × smoothed_prev
  state.smoothed_value = alpha * raw_value + (1.0f - alpha) * state.smoothed_value;
  state.current_alpha = alpha; // Store for debugging
  state.last_raw_value = raw_value;
  state.last_update_time = getCurrentTimeMs();

  // Round to nearest integer
  uint8_t result = (uint8_t)(state.smoothed_value + 0.5f);

  // Check if we need to continue smoothing (smoothed hasn't reached target yet)
  if (result != state.target_value) {
    if (!state.is_smoothing) {
      // Starting new smoothing
      state.is_smoothing = true;
      if (!config.quiet) {
        std::ostringstream msg;
        msg << "START convergence to " << (int)state.target_value;
        log("SMOOTH", control, msg.str());
      }
    } else {
      state.is_smoothing = true; // Keep smoothing
    }
  } else {
    state.is_smoothing = false;
  }

  // Debug logging - shows raw input, smoothed output, alpha, and velocity
  if (!config.quiet) {
    logSmoothing(control, raw_value, result, alpha, velocity);
  }

  return result;
}

// ============================================================================
// MIDI Send Function
// ============================================================================

void sendMidiCC(uint8_t control, uint8_t value) {
    if (toModulaser) {
        std::vector<unsigned char> message;
        message.push_back(0xB0);  // Control Change on channel 1
        message.push_back(control);
        message.push_back(value);
        toModulaser->sendMessage(&message);

        // Update last activity for status box
        {
          std::lock_guard<std::mutex> lock(waiting_mutex);
          last_activity.control = control;
          last_activity.value = value;
          last_activity.has_activity = true;
        }

        // Update send tracking for echo detection
        ControlState& state = control_states[control];
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
}

// ============================================================================
// MIDI Callbacks
// ============================================================================

void modulaserCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
    if (message->size() < 3) return;

    uint8_t status = (*message)[0];
    uint8_t data1 = (*message)[1];
    uint8_t data2 = (*message)[2];

    // Handle Note On/Off messages - use for LED control
    if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90) {
      // We don't log basic Note On/Off from Modulaser anymore to reduce verbosity
      // Only log the LED changes which are more relevant

      // Use Modulaser's Note On messages to control MidiMix LEDs
      // Velocity > 100 = LED on (active preset), velocity <= 100 = LED off (available preset)
      if (((status & 0xF0) == 0x90) && ledController) {
        bool shouldBeOn = data2 > 100;
        ledController->setNoteLED(data1, shouldBeOn);
        logLED(data1, shouldBeOn, shouldBeOn ? "active" : "available");
      }

        return;
    }

    // Only handle Control Change messages (0xB0-0xBF)
    if ((status & 0xF0) != 0xB0) return;

    uint8_t control = data1;
    uint8_t value = data2;

    // Ignore echo messages for CC61 and CC62 - they don't use easing state
    // Processing these floods the system and adds latency
    if (control == 61 || control == 62) {
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex);

    // Retrieve current control state
    ControlState& state = control_states[control];

    if (state.is_smoothing) {
      state.last_modulaser_value = value;
      state.modulaser_value_known = true;
      // Don't log to reduce noise during smoothing
      return;
    }

    // Check if this Modulaser value is an echo of something we recently sent
    // We track a window of recently sent values (min to max) and ignore echoes within this range
    // The window expires after 500ms of no activity (echoes shouldn't lag more than that)
    double now_ms = getCurrentTimeMs();
    double echo_window_ms = 500.0;

    if (state.last_send_time > 0 && (now_ms - state.last_send_time) < echo_window_ms) {
      // Within echo window - check if value is in our recent send range
      int margin = config.easing_threshold;
      uint8_t range_min = (state.recent_send_min > margin) ? state.recent_send_min - margin : 0;
      uint8_t range_max = (state.recent_send_max + margin <= 127) ? state.recent_send_max + margin : 127;

      if (value >= range_min && value <= range_max) {
        // This is likely an echo - ignore it
        state.last_modulaser_value = value;
        state.modulaser_value_known = true;
        // logEcho(control, value, range_min, range_max, now_ms - state.last_send_time);
        return;
      }
    } else if (state.last_send_time > 0) {
      // Echo window expired - reset the range
      state.recent_send_min = 0;
      state.recent_send_max = 0;
    }

    // Check if this new Modulaser value is far from the last controller value
    // If so, unlatch to prevent jumps when controller next moves
    // This applies even on the FIRST Modulaser message (e.g., when Modulaser sends all controls on connect)
    // Only check if we've received a controller value - otherwise just accept Modulaser's state
    if (state.controller_value_known) {
        int diff = std::abs((int)value - (int)state.last_controller_value);
        if (diff >= config.easing_threshold) {
            // Modulaser value has changed significantly, unlatch
            if (state.is_latched) {
                state.is_latched = false;
                logUnlatch(control, value, state.last_controller_value, diff);
            }
        } else {
            // Values are close, ensure we're latched
            if (!state.is_latched) {
                state.is_latched = true;
                logLatch(control, "Modulaser and controller values are close");
            }
        }
    }

    state.last_modulaser_value = value;
    state.modulaser_value_known = true;

    logMessage("Modulaser", control, value);
}

void midimixCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
    if (message->size() < 3) return;

    uint8_t status = (*message)[0];
    uint8_t data1 = (*message)[1];
    uint8_t data2 = (*message)[2];

    // Handle Note On/Off messages (0x80-0x9F) - pass through immediately
    if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90) {
      // We don't log basic Note On/Off from MidiMix to reduce verbosity
      // Pass through immediately without easing
      // LED control will be handled by Modulaser's response
      if (toModulaser) {
        toModulaser->sendMessage(message);
      }
        return;
    }

    // Only handle Control Change messages (0xB0-0xBF)
    if ((status & 0xF0) != 0xB0) return;

    uint8_t control = data1;
    uint8_t value = data2;

    // Apply transformations and send immediately (bypass all easing logic)
    // These controls need instant response, so they're handled before mutex locking
    if (control == 61) {
        // CC61: Speed parameter - map 0-127 to 0-31
        // Reason: Modulaser's speed goes to 800% at full range, which is never needed
        // This keeps it in a more usable 0-25% range
        uint8_t mapped_value = mapValue(value, 0, 127, 0, 31);
        sendMidiCC(control, mapped_value);
        logMapping(control, value, mapped_value, "speed 0-31");
        return;
    }

    if (control == 62) {
        // CC62: Crossfader - invert the value (127 - value)
        // Reason: Physical slider orientation feels backwards
        // Top position (0) = crossfade left, Bottom position (127) = crossfade right
        uint8_t inverted_value = 127 - value;
        sendMidiCC(control, inverted_value);
        logMapping(control, value, inverted_value, "inverted");
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex);

    // Log the message
    if (!config.quiet) {
      logMessage("MidiMix", control, value);
    }

    ControlState& state = control_states[control];
    uint8_t previous_controller_value = state.last_controller_value;

    // Check whitelist - bypass smoothing and latch logic
    if (config.whitelist_controls.count(control)) {
        // Only send if value actually changed
        if (value != state.last_sent_value) {
          logWhitelist(control, value);
          sendMidiCC(control, value);
          state.last_sent_value = value;
        }
        // Always track controller position (even for whitelisted controls)
        state.last_controller_value = value;
        state.controller_value_known = true;
        return;
    }

    // Handle first controller movement - initialize position without latching
    if (!state.controller_value_known) {
        state.last_controller_value = value;
        state.controller_value_known = true;

        // If we don't know Modulaser's value yet, send immediately and latch
        if (!state.modulaser_value_known) {
            sendMidiCC(control, value);
            state.last_sent_value = value;
            state.is_latched = true;  // Latch immediately on first move
            return;
        }

        // If we DO know Modulaser's value, check if we're close enough to latch
        int diff = std::abs((int)value - (int)state.last_modulaser_value);
        if (diff < config.easing_threshold) {
            // Close enough - latch immediately
            state.is_latched = true;
            logLatch(control, "first movement close to Modulaser value");
            // Fall through to normal sending logic
        } else {
            // Not close - wait for crossover
            state.is_latched = false;
            logWaiting(control, state.last_modulaser_value, value);
            return;
        }
    } else {
        // Update controller position for subsequent movements
        state.last_controller_value = value;
    }

    // ============================================================================
    // LATCH/PICKUP MODE LOGIC
    // ============================================================================
    // When not latched, don't send values until controller crosses Modulaser value
    // This prevents jumps when controller and GUI are out of sync
    // ============================================================================

    if (!state.is_latched) {
        // Not latched - check if controller has crossed the Modulaser value
        uint8_t modulaser_val = state.last_modulaser_value;

        // Check if we've crossed over the Modulaser value
        // This means: previous value was on one side, current value is on the other side (or equal)
        bool crossed = false;

        if (previous_controller_value < modulaser_val && value >= modulaser_val) {
            // Crossed from below
            crossed = true;
        } else if (previous_controller_value > modulaser_val && value <= modulaser_val) {
            // Crossed from above
            crossed = true;
        }

        if (crossed) {
            // Controller has crossed Modulaser value - LATCH and start sending
            state.is_latched = true;
            std::ostringstream latch_msg;
            latch_msg << "controller crossed " << ANSIColor::BRIGHT_MAGENTA << "Modulaser:" << (int)modulaser_val << ANSIColor::RESET
                      << " (was " << (int)previous_controller_value << " → now " << (int)value << ")";
            logLatch(control, latch_msg.str());
            // Fall through to normal sending logic below
        } else {
            // Haven't crossed yet - don't send anything
            logWaiting(control, modulaser_val, value);
            return;  // Don't send anything
        }
    }

    // ============================================================================
    // EMA SMOOTHING (when latched)
    // ============================================================================

    uint8_t smoothed = applyEMASmoothing(state, value, control, config);

    // Only send if value changed (avoid duplicates)
    if (smoothed != state.last_sent_value) {
      sendMidiCC(control, smoothed);
      state.last_sent_value = smoothed;
    }
}

// ============================================================================
// Smoothing Thread (handles EMA convergence)
// ============================================================================

void easingThreadFunc() {
  while (running) {
    std::lock_guard<std::mutex> lock(state_mutex);

    for (auto &[control, state] : control_states) {
      // Continue applying EMA to converge smoothed_value toward target_value
      if (state.is_smoothing) {
        float alpha = state.alpha_min; // Use minimum alpha for convergence (smoothest)

        // Apply EMA: smoothed = α × target + (1-α) × smoothed_prev
        state.smoothed_value = alpha * state.target_value + (1.0f - alpha) * state.smoothed_value;

        // Round to nearest integer
        uint8_t result = (uint8_t)(state.smoothed_value + 0.5f);

        // Debug: Log convergence progress
        // if (!config.quiet) {
        //     std::ostringstream msg;
        //     msg << "converging: target=" << (int)state.target_value
        //         << " smoothed=" << (int)result
        //         << " (alpha=" << std::fixed << std::setprecision(2) << alpha << ")";
        //     log("CONVERGE", control, msg.str());
        // }

        // Only send if value changed (avoid duplicates)
        if (result != state.last_sent_value) {
          sendMidiCC(control, result);
          state.last_sent_value = result;
        }

        // Check if converged (smoothed has reached target)
        if (result == state.target_value) {
          state.is_smoothing = false;
          if (!config.quiet) {
            log("CONVERGE", control, "complete");
          }
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / config.update_rate_hz));
  }
}

// ============================================================================
// MidiMix Connection Polling
// ============================================================================

void pollForMidiMix() {
    while (running && !midimix_connected) {
        try {
            RtMidiIn *midiin = new RtMidiIn();
            RtMidiOut *midiout = new RtMidiOut();
            unsigned int nPorts = midiin->getPortCount();

            for (unsigned int i = 0; i < nPorts; i++) {
                std::string portName = midiin->getPortName(i);
                if (portName.find(config.input_port_name) != std::string::npos) {
                  std::ostringstream msg;
                  msg << "Found " << config.input_port_name << " on port " << i;
                  logInfo(msg.str());

                  // Setup MIDI input from MidiMix
                  fromMidiMix = midiin;
                  fromMidiMix->openPort(i);
                  fromMidiMix->setCallback(&midimixCallback, nullptr);
                  fromMidiMix->ignoreTypes(false, false, false);

                  // Setup MIDI output to MidiMix for LED control
                  toMidiMix = midiout;
                  toMidiMix->openPort(i);

                  // Initialize LED controller
                  if (ledController) {
                    ledController->setMidiOut(toMidiMix);
                    ledController->runLightShow();
                  }

                    midimix_connected = true;
                    logInfo("Connected to " + config.input_port_name);
                    return;
                }
            }

            delete midiin;
            delete midiout;
        } catch (RtMidiError &error) {
            std::cerr << "[ERROR] MidiMix polling error: " << error.getMessage() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ============================================================================
// Configuration Parser
// ============================================================================

void trim(std::string& s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
}

bool parseConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[WARNING] Could not open " << filename
                  << ", using defaults" << std::endl;
        return false;
    }

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        // Section header
        if (line[0] == '[' && line[line.length()-1] == ']') {
            current_section = line.substr(1, line.length()-2);
            continue;
        }

        // Key-value pair
        size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        trim(key);
        trim(value);

        // Parse based on section
        if (current_section == "MIDI") {
            if (key == "input_port_name") config.input_port_name = value;
            else if (key == "virtual_port_name") config.virtual_port_name = value;
        }
        else if (current_section == "Settings") {
            if (key == "whitelist_controls") {
                std::stringstream ss(value);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    trim(item);
                    config.whitelist_controls.insert(std::stoi(item));
                }
            }
            else if (key == "easing_threshold") config.easing_threshold = std::stoi(value);
            else if (key == "update_rate_hz") config.update_rate_hz = std::stoi(value);
        } else if (current_section == "Smoothing") {
          if (key == "default_alpha_min")
            config.default_alpha_min = std::stof(value);
          else if (key == "default_alpha_max")
            config.default_alpha_max = std::stof(value);
          else if (key == "velocity_threshold_low")
            config.velocity_threshold_low = std::stoi(value);
          else if (key == "velocity_threshold_high")
            config.velocity_threshold_high = std::stoi(value);
          else if (key == "adaptive_enabled")
            config.adaptive_enabled = (value == "true" || value == "1");
        } else if (current_section == "Smoothing_per_control") {
          uint8_t control = std::stoi(key);

          // Parse value - can be either:
          // 1. Single float (fixed alpha): "0.5"
          // 2. Comma-separated (alpha_min, alpha_max, vel_low, vel_high): "0.2,0.8,2,10"
          std::stringstream ss(value);
          std::string item;
          std::vector<std::string> values;
          while (std::getline(ss, item, ',')) {
            trim(item);
            values.push_back(item);
          }

          Config::SmoothingParams params;
          if (values.size() == 1) {
            // Fixed alpha mode
            float alpha = std::stof(values[0]);
            params.alpha_min = alpha;
            params.alpha_max = alpha;
            params.velocity_threshold_low = 0;
            params.velocity_threshold_high = 127;
            params.adaptive_enabled = false;
          } else if (values.size() == 4) {
            // Full adaptive mode
            params.alpha_min = std::stof(values[0]);
            params.alpha_max = std::stof(values[1]);
            params.velocity_threshold_low = std::stoi(values[2]);
            params.velocity_threshold_high = std::stoi(values[3]);
            params.adaptive_enabled = true;
          } else {
            std::cerr << "[WARNING] Invalid smoothing config for CC" << (int)control << std::endl;
            continue;
          }

          config.smoothing_per_control[control] = params;
        }
        // Ignore legacy [DEFAULTS] and [Easing_durations] sections
    }

    return true;
}

// Initialize control states with smoothing parameters from config
void initializeControlStates() {
  std::lock_guard<std::mutex> lock(state_mutex);

  // Initialize all 128 possible MIDI CC controls
  for (int i = 0; i < 128; i++) {
    uint8_t control = i;
    ControlState &state = control_states[control];

    // Check if there's a per-control override
    if (config.smoothing_per_control.count(control)) {
      const auto &params = config.smoothing_per_control[control];
      state.alpha_min = params.alpha_min;
      state.alpha_max = params.alpha_max;
      state.velocity_threshold_low = params.velocity_threshold_low;
      state.velocity_threshold_high = params.velocity_threshold_high;
      state.adaptive_enabled = params.adaptive_enabled;
      state.current_alpha = params.alpha_min; // Start with min alpha
    } else {
      // Use global defaults
      state.alpha_min = config.default_alpha_min;
      state.alpha_max = config.default_alpha_max;
      state.velocity_threshold_low = config.velocity_threshold_low;
      state.velocity_threshold_high = config.velocity_threshold_high;
      state.adaptive_enabled = config.adaptive_enabled;
      state.current_alpha = config.default_alpha_min; // Start with min alpha
    }
  }
}

void printConfig() {
    std::cout << "\n=== Configuration ===" << std::endl;
    std::cout << "Input Port: " << config.input_port_name << std::endl;
    std::cout << "Virtual Port: " << config.virtual_port_name << std::endl;
    std::cout << "Update Rate: " << config.update_rate_hz << " Hz" << std::endl;
    std::cout << "EMA Alpha Range: " << config.default_alpha_min << " - " << config.default_alpha_max << std::endl;
    std::cout << "Velocity Thresholds: " << (int)config.velocity_threshold_low << " - " << (int)config.velocity_threshold_high << std::endl;
    std::cout << "Adaptive Enabled: " << (config.adaptive_enabled ? "yes" : "no") << std::endl;

    if (!config.whitelist_controls.empty()) {
        std::cout << "Whitelisted Controls: ";
        for (auto control : config.whitelist_controls) {
            std::cout << (int)control << " ";
        }
        std::cout << std::endl;
    }

    std::cout << "=====================\n" << std::endl;
}

// ============================================================================
// Signal Handler
// ============================================================================

void signalHandler(int signum) {
  logInfo("\nShutting down...");

  // Turn off all LEDs before shutdown
  if (ledController) {
    ledController->allOff();
  }

    running = false;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
    std::cout << "MIDI Easing Proxy v2.0 (C++)" << std::endl;
    std::cout << "============================\n" << std::endl;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-q" || arg == "--quiet") {
            config.quiet = true;
        }
    }

    // Parse configuration
    parseConfig("config.ini");
    initializeControlStates(); // Initialize control states with smoothing params
    printConfig();

    // Setup signal handler
    signal(SIGINT, signalHandler);

    try {
        // Create LED controller
        ledController = new LEDController();

        // Create virtual MIDI ports
        logInfo("Creating virtual MIDI port: " + config.virtual_port_name);

        fromModulaser = new RtMidiIn();
        toModulaser = new RtMidiOut();

        fromModulaser->openVirtualPort(config.virtual_port_name);
        toModulaser->openVirtualPort(config.virtual_port_name);

        fromModulaser->setCallback(&modulaserCallback, nullptr);
        fromModulaser->ignoreTypes(false, false, false);

        logInfo("Virtual port created successfully");

        // Start easing thread
        std::thread easingThread(easingThreadFunc);

        // Start MidiMix polling thread
        logInfo("Polling for " + config.input_port_name + "...");
        std::thread pollingThread(pollForMidiMix);

        logInfo("MIDI Easing Proxy is running");
        logInfo("Press Ctrl+C to exit\n");

        // Show initial status box in quiet mode
        if (config.quiet) {
          displayStatusBox();
        }

        // Wait for threads to complete
        pollingThread.join();
        easingThread.join();

    } catch (RtMidiError &error) {
        std::cerr << "[ERROR] " << error.getMessage() << std::endl;
        return 1;
    }

    // Cleanup
    if (ledController) {
        ledController->allOff();
        delete ledController;
    }
    if (fromModulaser) {
        fromModulaser->closePort();
        delete fromModulaser;
    }
    if (toModulaser) {
        toModulaser->closePort();
        delete toModulaser;
    }
    if (fromMidiMix) {
        fromMidiMix->closePort();
        delete fromMidiMix;
    }
    if (toMidiMix) {
        toMidiMix->closePort();
        delete toMidiMix;
    }

    logInfo("Shutdown complete");
    return 0;
}
