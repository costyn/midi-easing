# Phase 3: Context Findings

## Language/Framework Options Analysis

Based on user preferences (C, C++, Kotlin, Java, TypeScript) and technical requirements:

### Option 1: C++ with RtMidi ⭐ RECOMMENDED
**Pros:**
- Native performance, minimal overhead
- RtMidi is mature, actively maintained (2025)
- Direct CoreMIDI integration on macOS
- User is familiar with C++
- Easy to maintain and debug
- Single-file compilation possible for simple CLI tools

**Cons:**
- More boilerplate than higher-level languages
- Manual memory management (though modern C++ helps)

**Implementation:**
- Library: RtMidi (https://github.com/thestk/rtmidi)
- Virtual ports: `openVirtualPort()` method on RtMidiIn/RtMidiOut
- Build: Simple Makefile or CMake
- Threading: std::thread for easing loop

### Option 2: TypeScript/Node.js with node-midi
**Pros:**
- User is familiar with TypeScript
- node-midi wraps RtMidi (same underlying tech as Python)
- easymidi library provides higher-level API
- Good for rapid development
- TypeScript provides type safety

**Cons:**
- Node.js runtime dependency
- Slightly higher overhead than C++
- npm dependency management
- Compilation step (TypeScript → JavaScript)

**Implementation:**
- Library: node-midi or easymidi
- Virtual ports: `openVirtualPort()` supported
- Build: tsc + node
- Threading: setInterval for easing loop

### Option 3: Python with rtmidi (Current POC)
**Pros:**
- Working POC already exists
- Fastest to complete
- python-rtmidi wraps same C++ library

**Cons:**
- User not familiar with Python
- Harder to maintain long-term
- Runtime dependency

### Option 4: Java/Kotlin
**Not recommended:**
- Java MIDI API doesn't support virtual MIDI ports well on macOS
- Would need JNI wrapper around CoreMIDI
- Adds significant complexity

## MIDI Library Deep Dive

### RtMidi Virtual Port API (C++)
```cpp
RtMidiIn *midiin = new RtMidiIn();
midiin->openVirtualPort("My Virtual Port");

RtMidiOut *midiout = new RtMidiOut();
midiout->openVirtualPort("My Virtual Port");

// Callback-based message handling
void callback(double timeStamp, std::vector<unsigned char> *message, void *userData) {
    // Process message
}
midiin->setCallback(&callback, userData);
```

### Key Virtual Port Behavior
- Same port name for input/output creates bidirectional connection
- Other apps see "My Virtual Port" as available MIDI device
- CoreMIDI handles routing automatically
- No manual connection needed (unlike ALSA/JACK on Linux)

## Easing Implementation Patterns

### Recommended Approach: Separate Easing Thread
**Pattern from midi-easing.py:**
```
Main Thread:              Easing Thread:
  ↓                           ↓
Listen MIDI              Loop @ 100Hz
  ↓                           ↓
Update target           Interpolate
  ↓                           ↓
                         Send eased values
```

**Benefits:**
- Decouples MIDI I/O from easing calculations
- Predictable timing (fixed 100Hz)
- Smooth output even with irregular input

### Easing Function
**Current:** SineEaseInOut
**Alternatives to consider:**
- Linear (fastest, but more mechanical)
- QuadraticEaseOut (gentle deceleration)
- ExponentialEaseOut (natural feeling)
- CubicEaseInOut (balanced)

**For real-time MIDI control:** SineEaseInOut or CubicEaseInOut are good defaults
- Not too aggressive
- Predictable timing
- Musically appropriate

### Timing Best Practices
**From research:**
- 100Hz (10ms) update rate is standard for MIDI control smoothing
- Easing duration 1-5 seconds is typical
  - 1-2s: Responsive, slight smoothing
  - 3-5s: Very smooth, more noticeable lag
- Line segments (250ms grain) are common approach
- Current config (5s default) may be too slow for some uses

## State Tracking Strategy

### Required State Per Control
```cpp
struct ControlState {
    uint8_t last_midimix_value;      // Last value from controller
    uint8_t last_modulaser_value;    // Last echoed from Modulaser
    uint8_t current_target;          // Where we're easing to
    double easing_start_time;        // When current ease started
    bool is_easing;                  // Is easing active?
    uint8_t last_sent_value;         // Avoid duplicate sends
};

std::map<uint8_t, ControlState> control_states;  // Keyed by control number
```

### State Update Logic
**When MidiMix message arrives (control X, value Y):**
1. If control is whitelisted → send immediately, skip easing
2. Get current_modulaser_value from control_states[X]
3. If abs(Y - current_modulaser_value) > threshold:
   - Start new ease: from current_modulaser_value to Y
   - Update easing_start_time
   - Set is_easing = true

**When Modulaser echo arrives (control X, value Z):**
1. Update control_states[X].last_modulaser_value = Z
2. If not currently easing, this is our baseline

**Easing thread loop:**
1. For each control where is_easing == true
2. Calculate elapsed = now - easing_start_time
3. If elapsed >= duration: send target, set is_easing = false
4. Else: interpolate and send eased value

## Configuration Format

### Keep Current INI Format
**Rationale:**
- Simple, human-readable
- No external parser needed (C++ has simple INI parsers)
- Matches user's current setup

**Required Sections:**
```ini
[MIDI]
input_port_name = MIDI Mix
virtual_port_name = Midi Easing

[Settings]
whitelist_controls = 61,62
update_rate_hz = 100

[DEFAULTS]
default_easing_duration = 5000

[Easing_durations]
18 = 3000
19 = 1500
20 = 2500
```

## Files to Modify/Create

### If Sticking with Python (rtimidi-listen.py)
**Files:**
- `rtimidi-listen.py` → `midi-easing-proxy.py` (refactor with easing)
- `config.ini` (update output_port_name)
- Keep `requirements.txt`

### If Moving to C++
**New files:**
- `midi-easing-proxy.cpp` (main implementation)
- `easing.h` (easing functions)
- `config.h` (config parser)
- `Makefile` or `CMakeLists.txt`
- Update `README.md`

### If Moving to TypeScript
**New files:**
- `src/midi-easing-proxy.ts` (main)
- `src/easing.ts` (easing functions)
- `src/config.ts` (config parser)
- `package.json`, `tsconfig.json`
- Update `README.md`

## Technical Constraints

### MIDI Protocol
- Control Change: Status byte 0xB0-0xBF (176-191)
- Control number: 0-127
- Value: 0-127 (7-bit)
- Channel: Embedded in status byte (low 4 bits)

### macOS CoreMIDI
- Virtual ports supported via RtMidi
- No special permissions needed
- Works with any CoreMIDI-compatible app (Modulaser)

### Real-Time Performance
- 100Hz easing thread is low overhead
- MIDI messages are small (3 bytes)
- CPU usage should be negligible
- Memory usage: ~128 control states × ~50 bytes = ~6KB

## Integration Patterns from Codebase

### From midi-easing.py - Value Transformations
**Control 61:** Mapping (0-127 → 0-31)
```python
mapped_value = round(map_value(message.value, 0, 127, 0, 31))
```

**Control 62:** Inversion
```python
inverted_value = 127 - message.value
```

**Must preserve this behavior in new implementation**

### From midi-easing.py - Whitelist Behavior
Controls in whitelist:
1. Apply transformations (if specified)
2. Send immediately
3. Skip easing logic

### From rtimidi-listen.py - Message Handling
**Message format:** `[status, control, value]`
- Status: 176 = CC channel 1
- Control: which knob/fader
- Value: 0-127

**Callback pattern works well** - no need for polling loop

## Recommendation Summary

**Language:** C++ with RtMidi
- Best balance of performance, maintainability, user familiarity
- Direct compilation to native binary
- No runtime dependencies beyond system libraries

**Architecture:**
- Main thread: MIDI I/O with callbacks
- Easing thread: 100Hz interpolation loop
- Shared state: std::map with mutex

**Timeline:**
- ~2-4 hours to port from Python POC to C++
- ~1 hour testing and refinement
- Much more maintainable long-term
