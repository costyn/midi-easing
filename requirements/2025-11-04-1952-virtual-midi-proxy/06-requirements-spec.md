# Requirements Specification: MIDI Easing Proxy (C++)

**Project:** Virtual MIDI Proxy with Easing
**Version:** 2.0 (C++ rewrite)
**Date:** 2025-11-04
**Status:** Ready for Implementation

---

## 1. Problem Statement

### Current Issue
When using Modulaser (laser control software) with an Akai MidiMix controller:
1. User switches to a new preset in Modulaser
2. Preset parameters have different values than physical knob positions
3. Moving a knob causes **instant jump** to the knob's physical position
4. Results in jerky, unpleasant visual animations

### Root Cause
Direct MIDI connection between controller and software means no opportunity to smooth parameter transitions.

### Solution
Create a MIDI proxy that:
- Sits between MidiMix controller and Modulaser
- Tracks current parameter values from Modulaser's echo messages
- Applies easing when large value jumps are detected
- Sends gradual interpolated values to smooth transitions

---

## 2. Functional Requirements

### FR-1: Virtual MIDI Port Creation
**Must:**
- Create virtual MIDI port named "Midi Easing" at startup
- Port must appear as both input and output device to Modulaser
- Start successfully even if Modulaser is not running
- CoreMIDI compatible (macOS)

### FR-2: Physical MIDI Controller Connection
**Must:**
- Poll for "MIDI Mix" device every 1 second until found
- Automatically connect when device becomes available
- Handle device disconnection gracefully
- Continue running if device disconnects, resume when reconnected
- Log connection status changes to console

### FR-3: Bidirectional MIDI Message Routing
**Must:**
- Receive Control Change messages from MidiMix → forward to Modulaser
- Receive echo messages from Modulaser → update internal state
- Support MIDI Control Change messages (status 0xB0-0xBF)
- Pass through Note On/Off messages without modification

### FR-4: Control Value Easing
**Must:**
- Track last known value per control number (0-127)
- Detect when new MidiMix value differs from Modulaser's current value
- Apply easing ONLY when difference >= threshold (default: 3)
- Use configurable easing function (default: SineEaseInOut from AHEasing)
- Interpolate from current_modulaser_value → new_midimix_value
- Send eased values at 100Hz (every 10ms)
- Stop easing when target reached

**Must Not:**
- Ease values that are already close to target
- Send duplicate MIDI messages (skip if integer value unchanged)
- Ease during initial startup (wait for first echo from Modulaser)

### FR-5: Whitelisted Controls (Bypass Easing)
**Must:**
- Support whitelist of control numbers that bypass easing
- Controls 61 and 62 are whitelisted by default (from config.ini)
- Whitelisted controls send immediately without delay
- Still apply transformations to whitelisted controls

### FR-6: Control Value Transformations
**Must:**
- Control 61: Map value range 0-127 → 0-31
- Control 62: Invert value (127 - value)
- Apply transformations BEFORE easing logic
- Hardcoded (not configurable)

### FR-7: Configuration File Support
**Must:**
- Read config.ini file at startup
- Support sections: [MIDI], [Settings], [DEFAULTS], [Easing_durations]
- Fail gracefully if config file missing (use defaults)
- Log configuration values at startup

**Configuration Parameters:**
```ini
[MIDI]
input_port_name = MIDI Mix          # Physical controller name
virtual_port_name = Midi Easing     # Virtual port to create

[Settings]
whitelist_controls = 61,62          # Comma-separated control numbers
easing_threshold = 3                # Minimum diff to trigger easing
update_rate_hz = 100                # Easing thread frequency

[DEFAULTS]
default_easing_duration = 5000      # Default easing time (ms)

[Easing_durations]
18 = 3000                           # Per-control duration overrides
19 = 1500
20 = 2500
```

### FR-8: Logging and Output
**Must:**
- Default: Verbose logging (all MIDI messages)
- Show: Timestamp, source (MidiMix/Modulaser), CC number, value
- Show: Easing start/stop events
- Support `-q` or `--quiet` flag to suppress detailed logging
- Quiet mode: Show only startup, errors, connection status
- Use stdout for normal output, stderr for errors

### FR-9: Startup and Shutdown
**Must:**
- Start successfully regardless of device connection order
- Create virtual port immediately at startup
- Poll for MidiMix connection in background
- Graceful shutdown on Ctrl+C (SIGINT)
- Clean up MIDI ports on exit
- Show "Ready" message when all systems operational

---

## 3. Technical Requirements

### TR-1: Language and Platform
- **Language:** C++ (C++11 or later)
- **Platform:** macOS (primary), cross-platform potential
- **Compiler:** Clang (Xcode Command Line Tools) or GCC
- **Build System:** Simple Makefile

### TR-2: Dependencies
- **RtMidi:** C++ MIDI library (https://github.com/thestk/rtmidi)
  - Provides CoreMIDI backend for macOS
  - Virtual port creation support
  - Callback-based message handling
- **AHEasing:** Single-header easing functions (https://github.com/warrenm/AHEasing)
  - No dependencies
  - Multiple easing functions available
  - User already familiar from Arduino projects

### TR-3: Architecture

#### Threading Model
```
Main Thread:
  - MIDI I/O setup
  - MidiMix connection polling (every 1 second)
  - Message callbacks (RtMidi callbacks run in audio thread)
  - Console output

Easing Thread:
  - Runs at 100Hz (every 10ms)
  - Iterates through active easings
  - Calculates interpolated values
  - Sends eased MIDI messages
  - Thread-safe access to shared state
```

#### State Management
```cpp
struct ControlState {
    uint8_t last_midimix_value;      // Last value from controller
    uint8_t last_modulaser_value;    // Last echoed from Modulaser
    uint8_t current_target;          // Where we're easing to
    double easing_start_time;        // When current ease started (ms)
    uint32_t easing_duration;        // Duration for this control (ms)
    bool is_easing;                  // Is easing currently active?
    uint8_t last_sent_value;         // Last value sent (avoid duplicates)
    bool modulaser_value_known;      // Have we received echo yet?
};

// Global state (protected by mutex)
std::map<uint8_t, ControlState> control_states;
std::mutex state_mutex;
```

#### Message Flow
```
MidiMix → Callback → Apply Transformations → Check Threshold
                                                    ↓
                                        Threshold Met? → Start Easing
                                                    ↓
                                        Threshold Not Met? → Send Immediately

Modulaser → Callback → Update last_modulaser_value in state

Easing Thread (100Hz):
    For each is_easing == true:
        Calculate elapsed time
        Apply easing function
        Send interpolated value
        If complete: set is_easing = false
```

### TR-4: Performance Requirements
- **CPU Usage:** < 1% on modern Mac (M1/M2/Intel i5+)
- **Memory Usage:** < 10MB resident
- **Latency:**
  - Whitelisted controls: < 1ms (immediate passthrough)
  - Eased controls: 10ms granularity (100Hz update rate)
- **Thread Safety:** All shared state access protected by mutex

### TR-5: Error Handling
**Must handle:**
- Config file missing → Use defaults, log warning
- Config file malformed → Use defaults for bad values, log errors
- MidiMix not found → Poll every second, log status
- Virtual port creation fails → Exit with error message
- MIDI message errors → Log and continue
- Ctrl+C during operation → Clean shutdown

### TR-6: Build and Deployment
**Must provide:**
- `Makefile` with targets:
  - `make` or `make all` → Build binary
  - `make clean` → Remove build artifacts
  - `make install` → Copy to /usr/local/bin (optional)
- Output binary: `midi-easing-proxy`
- No external runtime dependencies (static linking preferred)

---

## 4. Implementation Hints

### File Structure
```
midi-easing-proxy.cpp       # Main implementation (~300-400 lines)
easing.h                    # AHEasing library (copied from repo)
config.ini                  # Configuration file
Makefile                    # Build system
README.md                   # Updated documentation
```

### Key Code Patterns

#### RtMidi Virtual Port Setup
```cpp
RtMidiIn *fromModulaser = new RtMidiIn();
RtMidiOut *toModulaser = new RtMidiOut();

fromModulaser->openVirtualPort("Midi Easing");
toModulaser->openVirtualPort("Midi Easing");

fromModulaser->setCallback(&modulaserCallback, userdata);
```

#### MidiMix Connection Polling
```cpp
void pollForMidiMix() {
    while (!midimixConnected) {
        RtMidiIn *midiin = new RtMidiIn();
        unsigned int nPorts = midiin->getPortCount();

        for (unsigned int i = 0; i < nPorts; i++) {
            std::string portName = midiin->getPortName(i);
            if (portName.find("MIDI Mix") != std::string::npos) {
                midiin->openPort(i);
                midiin->setCallback(&midimixCallback, userdata);
                midimixConnected = true;
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

#### Easing Thread Loop
```cpp
void easingThreadFunc() {
    while (running) {
        auto now = std::chrono::steady_clock::now();
        double now_ms = std::chrono::duration<double, std::milli>(
            now.time_since_epoch()
        ).count();

        std::lock_guard<std::mutex> lock(state_mutex);

        for (auto& [control, state] : control_states) {
            if (state.is_easing) {
                double elapsed = now_ms - state.easing_start_time;
                double t = std::min(1.0, elapsed / state.easing_duration);

                // Use AHEasing function
                double eased_t = SineEaseInOut(t);

                uint8_t eased_value = state.last_modulaser_value +
                    eased_t * (state.current_target - state.last_modulaser_value);

                if (eased_value != state.last_sent_value) {
                    sendMidiCC(control, eased_value);
                    state.last_sent_value = eased_value;
                }

                if (t >= 1.0) {
                    state.is_easing = false;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

#### Config Parsing
Use simple line-based parsing or lightweight INI parser like:
- inih (https://github.com/benhoyt/inih) - single file, public domain
- Or implement simple regex-based parser for the few values needed

#### Thread-Safe State Updates
```cpp
void handleMidiMixMessage(uint8_t control, uint8_t value) {
    std::lock_guard<std::mutex> lock(state_mutex);

    // Apply transformations
    if (control == 61) {
        value = map(value, 0, 127, 0, 31);
    } else if (control == 62) {
        value = 127 - value;
    }

    // Check whitelist
    if (isWhitelisted(control)) {
        sendMidiCC(control, value);
        return;
    }

    // Check threshold
    ControlState& state = control_states[control];
    if (!state.modulaser_value_known) {
        // First message, just send it
        sendMidiCC(control, value);
        state.last_midimix_value = value;
        return;
    }

    int diff = abs(value - state.last_modulaser_value);
    if (diff < easing_threshold) {
        // Below threshold, send immediately
        sendMidiCC(control, value);
        state.last_modulaser_value = value;
    } else {
        // Start easing
        state.current_target = value;
        state.easing_start_time = getCurrentTimeMs();
        state.is_easing = true;
    }
}

void handleModulaserMessage(uint8_t control, uint8_t value) {
    std::lock_guard<std::mutex> lock(state_mutex);

    ControlState& state = control_states[control];
    state.last_modulaser_value = value;
    state.modulaser_value_known = true;

    if (!quiet) {
        logMessage("Modulaser", control, value);
    }
}
```

---

## 5. Acceptance Criteria

### AC-1: Basic Functionality
- [ ] Proxy starts successfully and creates "Midi Easing" virtual port
- [ ] Modulaser can connect to virtual port and receive MIDI
- [ ] Proxy detects and connects to MidiMix controller
- [ ] Control messages flow: MidiMix → Proxy → Modulaser

### AC-2: Easing Behavior
- [ ] Large value jumps (diff >= 3) trigger smooth easing
- [ ] Small value adjustments (diff < 3) respond immediately
- [ ] Easing duration matches config.ini settings
- [ ] Per-control duration overrides work correctly
- [ ] Modulaser echo messages update proxy state

### AC-3: Whitelist and Transformations
- [ ] Controls 61 and 62 bypass easing (immediate response)
- [ ] Control 61: Values map correctly (0-127 → 0-31)
- [ ] Control 62: Values invert correctly (127 - value)

### AC-4: Connection Handling
- [ ] Works when started before MidiMix connected
- [ ] Works when started before Modulaser launched
- [ ] Works in any device startup order
- [ ] Reconnects to MidiMix if disconnected and reconnected
- [ ] Console shows connection status changes

### AC-5: Configuration
- [ ] Reads config.ini successfully
- [ ] Uses defaults if config.ini missing
- [ ] Logs configuration at startup
- [ ] Per-control easing durations work correctly

### AC-6: Logging and Output
- [ ] Default: Shows all MIDI messages with timestamps
- [ ] `-q` flag: Suppresses detailed logging
- [ ] Quiet mode: Still shows errors and connection status
- [ ] Output is readable and helpful for debugging

### AC-7: Performance and Stability
- [ ] CPU usage remains < 1% during active use
- [ ] No memory leaks during extended operation
- [ ] Ctrl+C cleanly shuts down and releases MIDI ports
- [ ] No crashes during MidiMix disconnect/reconnect

### AC-8: User Experience
- [ ] After preset change, touching knob results in smooth transition (not jump)
- [ ] Small knob adjustments feel responsive
- [ ] Whitelisted controls respond instantly
- [ ] No noticeable latency during normal operation

---

## 6. Assumptions

### A-1: Platform
- Running on macOS 10.13+ (for CoreMIDI support)
- Xcode Command Line Tools installed (for compiler)
- User has admin access to install RtMidi if needed

### A-2: Hardware
- Akai MidiMix controller is the only input device
- MidiMix sends standard MIDI Control Change messages
- Modulaser echoes received CC messages back to MIDI input

### A-3: Configuration
- config.ini is in same directory as binary (or specified path)
- Control numbers 61 and 62 are the only ones needing transformations
- Easing durations between 1-10 seconds are reasonable

### A-4: MIDI Behavior
- Modulaser consistently echoes CC messages
- Echo messages arrive within reasonable time (< 100ms)
- No other MIDI applications competing for MidiMix
- Virtual MIDI ports don't conflict with existing ports

### A-5: User Knowledge
- User can build C++ projects with Makefile
- User can edit config.ini text files
- User understands basic MIDI concepts (CC, control numbers, values)
- User can use command-line tools

---

## 7. Out of Scope (Future Enhancements)

The following are explicitly NOT part of this implementation but may be considered later:

- **Multiple controller support:** Only MidiMix for now
- **GUI configuration tool:** CLI and config.ini only
- **Hot config reload:** Restart required for config changes
- **MIDI file recording:** No logging to MIDI files
- **Advanced transformations:** Only hardcoded ones for 61/62
- **Configurable easing curves:** SineEaseInOut only (code can change it)
- **MIDI Learn mode:** Manual config.ini editing only
- **Preset management:** No built-in preset switching
- **Network MIDI:** Local CoreMIDI only
- **Linux/Windows support:** macOS only initially
- **Installer/Package:** Build from source only

---

## 8. Success Metrics

1. **Smooth Transitions:** After switching Modulaser presets, touching any knob results in smooth parameter change over configured duration (no instant jumps)

2. **Responsive Feel:** Small knob adjustments (< 3 value change) feel immediate and responsive

3. **Reliable Operation:** Runs for hours without crashes, memory leaks, or CPU spikes

4. **Easy Configuration:** User can adjust easing durations in config.ini and see immediate effect after restart

5. **Maintainable Code:** User can read, understand, and modify C++ code without difficulty

---

## 9. Timeline Estimate

- **RtMidi setup and virtual ports:** 30 minutes
- **Config parsing:** 30 minutes
- **Basic message routing:** 1 hour
- **State management and easing logic:** 2 hours
- **MidiMix polling and connection handling:** 1 hour
- **Testing and refinement:** 1-2 hours
- **Documentation updates:** 30 minutes

**Total:** ~6-7 hours for complete implementation and testing

---

## 10. Related Files

### To Create
- `midi-easing-proxy.cpp` - Main implementation
- `Makefile` - Build system
- Copy `easing.h` from AHEasing repo

### To Modify
- `config.ini` - Update output_port_name, add new parameters
- `README.md` - Document new C++ implementation

### Reference Only (Keep for History)
- `midi-easing.py` - Original Python implementation
- `rtimidi-listen.py` - Working POC with rtmidi
- `requirements.txt` - Python dependencies (no longer needed for C++)

---

**End of Requirements Specification**
