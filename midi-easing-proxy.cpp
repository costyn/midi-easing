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
#include "vendor/rtmidi-6.0.0/RtMidi.h"
#include "vendor/AHEasing/easing.h"

// ============================================================================
// Configuration Structure
// ============================================================================

struct Config {
    std::string input_port_name = "MIDI Mix";
    std::string virtual_port_name = "Midi Easing";
    std::set<uint8_t> whitelist_controls;
    int easing_threshold = 3;
    int update_rate_hz = 100;
    uint32_t default_easing_duration = 5000;
    std::map<uint8_t, uint32_t> easing_durations;
    bool quiet = false;
};

// ============================================================================
// Control State Structure
// ============================================================================

struct ControlState {
    uint8_t last_midimix_value = 0;
    uint8_t last_modulaser_value = 0;
    uint8_t current_target = 0;
    double easing_start_time = 0.0;
    uint32_t easing_duration = 5000;
    bool is_easing = false;
    uint8_t last_sent_value = 0;
    bool modulaser_value_known = false;
    bool is_latched = true;  // Start latched, will unlatch when Modulaser sends different value
    uint8_t last_controller_value = 0;  // Track controller position for crossover detection
};

// ============================================================================
// Global State
// ============================================================================

Config config;
std::map<uint8_t, ControlState> control_states;
std::mutex state_mutex;
std::atomic<bool> running(true);
std::atomic<bool> midimix_connected(false);

RtMidiIn *fromModulaser = nullptr;
RtMidiOut *toModulaser = nullptr;
RtMidiIn *fromMidiMix = nullptr;

// ============================================================================
// Utility Functions
// ============================================================================

double getCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
}

void logMessage(const std::string& source, uint8_t control, uint8_t value) {
    if (!config.quiet) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        char buffer[100];
        std::strftime(buffer, sizeof(buffer), "%H:%M:%S", std::localtime(&time));

        std::cout << "[" << buffer << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
                  << source << " -> CC" << (int)control << ": " << (int)value << std::endl;
    }
}

void logEasingStart(uint8_t control, uint8_t from, uint8_t to, uint32_t duration) {
    if (!config.quiet) {
        std::cout << "[EASING] Starting for CC" << (int)control
                  << ": " << (int)from << " -> " << (int)to
                  << " over " << duration << "ms" << std::endl;
    }
}

void logEasingStop(uint8_t control) {
    if (!config.quiet) {
        std::cout << "[EASING] Completed for CC" << (int)control << std::endl;
    }
}

uint8_t mapValue(uint8_t value, uint8_t in_min, uint8_t in_max, uint8_t out_min, uint8_t out_max) {
    return out_min + (value - in_min) * (out_max - out_min) / (in_max - in_min);
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

    // Handle Note On/Off messages - pass through to MidiMix
    if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90) {
        if (!config.quiet) {
            std::string msg_type = ((status & 0xF0) == 0x90) ? "Note On" : "Note Off";
            std::cout << "[Modulaser] " << msg_type << " note " << (int)data1
                      << " velocity " << (int)data2 << std::endl;
        }
        // Note: We don't send these back to MidiMix since it doesn't need feedback
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

    if (state.is_easing) {
      state.last_modulaser_value = value;
      state.modulaser_value_known = true;
      // Don't log to reduce noise during easing
      return;
    }

    // Check if this new Modulaser value is far from the last controller value
    // If so, unlatch to prevent jumps when controller next moves
    if (state.modulaser_value_known && state.last_controller_value != 0) {
        int diff = std::abs((int)value - (int)state.last_controller_value);
        if (diff >= config.easing_threshold) {
            // Modulaser value has changed significantly, unlatch
            if (state.is_latched) {
                state.is_latched = false;
                if (!config.quiet) {
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
                if (!config.quiet) {
                    std::cout << "[LATCH] CC" << (int)control
                              << " - Modulaser and controller values are close" << std::endl;
                }
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
        if (!config.quiet) {
            std::string msg_type = ((status & 0xF0) == 0x90) ? "Note On" : "Note Off";
            std::cout << "[MidiMix] " << msg_type << " note " << (int)data1
                      << " velocity " << (int)data2 << std::endl;
        }
        // Pass through immediately without easing
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

        // Send immediately WITHOUT logging to minimize latency
        sendMidiCC(control, mapped_value);

        if (!config.quiet) {
            std::cout << "[MidiMix] CC61: " << (int)value
                      << " -> " << (int)mapped_value << " (mapped 0-31)" << std::endl;
        }
        return;
    }

    if (control == 62) {
        // CC62: Crossfader - invert the value (127 - value)
        // Reason: Physical slider orientation feels backwards
        // Top position (0) = crossfade left, Bottom position (127) = crossfade right
        uint8_t inverted_value = 127 - value;

        // Send immediately - no state tracking, no duplicate checking for maximum responsiveness
        sendMidiCC(control, inverted_value);

        if (!config.quiet) {
            std::cout << "[MidiMix] CC62: " << (int)value
                      << " -> " << (int)inverted_value << std::endl;
        }
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex);

    // Log the message
    if (!config.quiet) {
        logMessage("MidiMix", control, value);
    }

    ControlState& state = control_states[control];
    uint8_t previous_controller_value = state.last_controller_value;
    state.last_midimix_value = value;
    state.last_controller_value = value;  // Always track controller position

    // Get easing duration for this control
    if (config.easing_durations.count(control)) {
        state.easing_duration = config.easing_durations[control];
    } else {
        state.easing_duration = config.default_easing_duration;
    }

    // Check whitelist - bypass easing and latch logic
    if (config.whitelist_controls.count(control)) {
        // Only send if value actually changed
        if (value != state.last_sent_value) {
            if (!config.quiet) {
                std::cout << "[WHITELIST] CC" << (int)control
                          << " bypassing easing, sending " << (int)value << std::endl;
            }
            sendMidiCC(control, value);
            state.last_modulaser_value = value;
            state.last_sent_value = value;
        }
        return;
    }

    // If we don't know Modulaser's value yet, send immediately and latch
    if (!state.modulaser_value_known) {
        sendMidiCC(control, value);
        state.last_sent_value = value;
        state.is_latched = true;
        return;
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
            if (!config.quiet) {
                std::cout << "[LATCH] CC" << (int)control
                          << " - Controller crossed Modulaser value " << (int)modulaser_val
                          << " (was " << (int)previous_controller_value
                          << ", now " << (int)value << ")" << std::endl;
            }
            // Fall through to normal sending logic below
        } else {
            // Haven't crossed yet - don't send anything
            if (!config.quiet) {
                std::cout << "[WAITING] CC" << (int)control
                          << " - Waiting for crossover (Modulaser: " << (int)modulaser_val
                          << ", Controller: " << (int)value << ")" << std::endl;
            }
            return;  // Don't send anything
        }
    }

    // ============================================================================
    // NORMAL EASING LOGIC (when latched)
    // ============================================================================

    // Check if already easing
    if (state.is_easing) {
        // Already easing - just update the target, don't restart
        state.current_target = value;
        if (!config.quiet) {
            std::cout << "[EASING] Updated target for CC" << (int)control
                      << " to " << (int)value << std::endl;
        }
    } else {
        // Check threshold
        int diff = std::abs((int)value - (int)state.last_modulaser_value);
        if (diff < config.easing_threshold) {
            // Below threshold, send immediately
            sendMidiCC(control, value);
            state.last_modulaser_value = value;
            state.last_sent_value = value;
        } else {
            // Start NEW easing
            state.current_target = value;
            state.easing_start_time = getCurrentTimeMs();
            state.is_easing = true;
            logEasingStart(control, state.last_modulaser_value, value, state.easing_duration);
        }
    }
}

// ============================================================================
// Easing Thread
// ============================================================================

void easingThreadFunc() {
    while (running) {
        double now_ms = getCurrentTimeMs();

        std::lock_guard<std::mutex> lock(state_mutex);

        for (auto& [control, state] : control_states) {
            if (state.is_easing) {
                double elapsed = now_ms - state.easing_start_time;
                double t = std::min(1.0, elapsed / state.easing_duration);

                // Use SineEaseInOut easing function from AHEasing
                double eased_t = SineEaseInOut(t);

                // Calculate eased value
                double from = state.last_modulaser_value;
                double to = state.current_target;
                uint8_t eased_value = from + eased_t * (to - from);

                // Only send if value changed (avoid duplicates)
                if (eased_value != state.last_sent_value) {
                    sendMidiCC(control, eased_value);
                    state.last_sent_value = eased_value;
                }

                // Check if easing complete
                if (t >= 1.0) {
                    state.is_easing = false;
                    state.last_modulaser_value = state.current_target;
                    logEasingStop(control);
                }
            }
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1000 / config.update_rate_hz)
        );
    }
}

// ============================================================================
// MidiMix Connection Polling
// ============================================================================

void pollForMidiMix() {
    while (running && !midimix_connected) {
        try {
            RtMidiIn *midiin = new RtMidiIn();
            unsigned int nPorts = midiin->getPortCount();

            for (unsigned int i = 0; i < nPorts; i++) {
                std::string portName = midiin->getPortName(i);
                if (portName.find(config.input_port_name) != std::string::npos) {
                    std::cout << "[INFO] Found " << config.input_port_name
                              << " on port " << i << std::endl;

                    fromMidiMix = midiin;
                    fromMidiMix->openPort(i);
                    fromMidiMix->setCallback(&midimixCallback, nullptr);
                    fromMidiMix->ignoreTypes(false, false, false);

                    midimix_connected = true;
                    std::cout << "[INFO] Connected to " << config.input_port_name << std::endl;
                    return;
                }
            }

            delete midiin;
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
        }
        else if (current_section == "DEFAULTS") {
            if (key == "default_easing_duration") {
                config.default_easing_duration = std::stoi(value);
            }
        }
        else if (current_section == "Easing_durations") {
            uint8_t control = std::stoi(key);
            uint32_t duration = std::stoi(value);
            config.easing_durations[control] = duration;
        }
    }

    return true;
}

void printConfig() {
    std::cout << "\n=== Configuration ===" << std::endl;
    std::cout << "Input Port: " << config.input_port_name << std::endl;
    std::cout << "Virtual Port: " << config.virtual_port_name << std::endl;
    std::cout << "Easing Threshold: " << config.easing_threshold << std::endl;
    std::cout << "Update Rate: " << config.update_rate_hz << " Hz" << std::endl;
    std::cout << "Default Duration: " << config.default_easing_duration << " ms" << std::endl;

    if (!config.whitelist_controls.empty()) {
        std::cout << "Whitelisted Controls: ";
        for (auto control : config.whitelist_controls) {
            std::cout << (int)control << " ";
        }
        std::cout << std::endl;
    }

    if (!config.easing_durations.empty()) {
        std::cout << "Custom Durations:" << std::endl;
        for (const auto& [control, duration] : config.easing_durations) {
            std::cout << "  CC" << (int)control << ": " << duration << " ms" << std::endl;
        }
    }
    std::cout << "=====================\n" << std::endl;
}

// ============================================================================
// Signal Handler
// ============================================================================

void signalHandler(int signum) {
    std::cout << "\n[INFO] Shutting down..." << std::endl;
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
    printConfig();

    // Setup signal handler
    signal(SIGINT, signalHandler);

    try {
        // Create virtual MIDI ports
        std::cout << "[INFO] Creating virtual MIDI port: " << config.virtual_port_name << std::endl;

        fromModulaser = new RtMidiIn();
        toModulaser = new RtMidiOut();

        fromModulaser->openVirtualPort(config.virtual_port_name);
        toModulaser->openVirtualPort(config.virtual_port_name);

        fromModulaser->setCallback(&modulaserCallback, nullptr);
        fromModulaser->ignoreTypes(false, false, false);

        std::cout << "[INFO] Virtual port created successfully" << std::endl;

        // Start easing thread
        std::thread easingThread(easingThreadFunc);

        // Start MidiMix polling thread
        std::cout << "[INFO] Polling for " << config.input_port_name << "..." << std::endl;
        std::thread pollingThread(pollForMidiMix);

        std::cout << "\n[READY] MIDI Easing Proxy is running" << std::endl;
        std::cout << "[READY] Press Ctrl+C to exit\n" << std::endl;

        // Wait for threads to complete
        pollingThread.join();
        easingThread.join();

    } catch (RtMidiError &error) {
        std::cerr << "[ERROR] " << error.getMessage() << std::endl;
        return 1;
    }

    // Cleanup
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

    std::cout << "[INFO] Shutdown complete" << std::endl;
    return 0;
}
