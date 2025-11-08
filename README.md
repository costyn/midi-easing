# MIDI Smoothing Proxy

A virtual MIDI proxy that sits between a MIDI controller (Akai MidiMix) and software (Modulaser) to provide smooth, responsive control with intelligent filtering to eliminate controller jitter and jarring value jumps.

## Version 2.0 (C++ Implementation)

This is a high-performance C++ rewrite of the original Python implementation, designed specifically for live performance with minimal latency and reliable operation.

## Features

- **Virtual MIDI Port**: Creates "Midi Easing" virtual port that appears as both input and output
- **Automatic Device Detection**: Polls for MidiMix controller and connects automatically
- **LED Control**: Visual feedback using MIDIMix button LEDs with startup light show
- **EMA Smoothing**: Exponential Moving Average filter with adaptive alpha for continuous, responsive smoothing
- **Adaptive Response**: Automatically adjusts smoothing intensity based on controller movement speed
  - Heavy smoothing for slow movements (eliminates stick-slip jitter)
  - Light smoothing for fast movements (maintains responsiveness)
- **Convergence**: Smoothed values continue updating until they match the target, even after controller stops moving
- **Latch/Pickup Mode**: Prevents controller jumps - physical controls must "pick up" current software values before taking effect
- **Bidirectional Communication**: Tracks echo messages from Modulaser to maintain accurate state
- **Control Whitelist**: Bypass smoothing for specific controls that need immediate response
- **Custom Transformations**: Built-in value mapping for specific controls (CC61, CC62)
- **Per-Control Tuning**: Configure smoothing parameters for individual controls
- **High Performance**: 100Hz update rate with <1% CPU usage
- **Thread-Safe**: Separate threads for MIDI I/O, polling, and smoothing calculations

## Requirements

- **macOS** 10.13+ (uses CoreMIDI)
- **Xcode Command Line Tools** (for building)
- **Akai MidiMix** controller
- Software that echoes MIDI messages (tested with Modulaser)

## Building

### First Time Setup

Download the required dependencies (RtMidi and AHEasing):

```bash
./setup_deps.sh
```

This script downloads the libraries into the `vendor/` directory (not tracked in git).

### Building the Proxy

```bash
make
```

This will create the `midi-easing-proxy` executable.

## Installation

Optional: Install to system path

```bash
make install
```

This copies the binary to `/usr/local/bin/midi-easing-proxy`

## Usage

### Basic Usage

```bash
./midi-easing-proxy
```

The proxy will:
1. Create a virtual MIDI port named "Midi Easing"
2. Poll for the MidiMix controller every second
3. When connected, run a LED light show on the MidiMix
4. Start the smoothing engine at 100Hz
5. Display all MIDI messages (verbose mode)

### Output Modes

**Default Mode** (verbose with color):
```bash
./midi-easing-proxy
```

Displays all MIDI messages with color-coded output:
- 🎨 **ANSI Colors**: Automatic color detection (disables when piped to files)
- ⏳ **WAITING**: Yellow - Shows Modulaser vs Controller values during pickup
- ✓ **LATCH**: Green - Control is synchronized and active
- 〰 **SMOOTH**: Cyan - EMA smoothing in progress
- ⚡ **WHITELIST**: Bright cyan - Bypassing smoothing for immediate response

**Quiet Mode**:
```bash
./midi-easing-proxy -q
```

Displays a clean status box showing only active waiting controls:
```
╔════════════════════════════════════════════╗
║ WAITING: CC19 → M:45  | C:82  | Δ37       ║
║ WAITING: CC24 → M:100 | C:45  | Δ55       ║
╚════════════════════════════════════════════╝
```

When all controls are synchronized, shows last sent control:
```
╔════════════════════════════════════════════╗
║ LAST SENT: CC19 → 95                      ║
╚════════════════════════════════════════════╝
```

The box updates in real-time as controls enter/exit waiting state. Perfect for keeping on top of a terminal window during live performance to monitor pickup status at a glance.

Also shows:
- Startup information
- Connection status changes
- Errors

### Setup Steps

1. **Edit config.ini** with your preferences (see Configuration section below)
2. **Start the proxy**: `./midi-easing-proxy`
3. **Configure Modulaser**:
   - MIDI Input: Select "Midi Easing"
   - MIDI Output: Select "Midi Easing" (for echo messages)
4. **Connect MidiMix**: Plug in your controller (proxy detects automatically)
5. **Test**: Touch a knob and verify smooth transitions

Press `Ctrl+C` to exit gracefully.

## Configuration

The proxy reads `config.ini` on startup. If the file is missing, it uses sensible defaults.

### config.ini Format

```ini
[MIDI]
input_port_name = MIDI Mix         # Physical controller name to detect
virtual_port_name = Midi Easing    # Virtual port name to create

[Settings]
whitelist_controls = 31,51,55,59,61,62  # Controls that bypass smoothing (immediate)
easing_threshold = 3               # Used for echo detection margin
update_rate_hz = 100               # Smoothing thread update frequency

[Smoothing]
# EMA smoothing parameters
# Lower alpha = MORE smoothing (more lag, smoother)
# Higher alpha = LESS smoothing (more responsive, less smooth)
default_alpha_min = 0.25           # Heavy smoothing for slow movements
default_alpha_max = 0.8            # Light smoothing for fast movements
velocity_threshold_low = 2         # Below this: use alpha_min
velocity_threshold_high = 10       # Above this: use alpha_max
adaptive_enabled = true            # Use velocity-based adaptive alpha

[Smoothing_per_control]
# Per-control smoothing overrides
# Format: CC = alpha_min,alpha_max,vel_low,vel_high (adaptive)
# Or:     CC = fixed_alpha (non-adaptive)
18 = 0.2,0.75,2,12                 # Extra smoothing for CC18
20 = 0.25,0.75,3,10                # Custom settings for CC20
61 = 1.0                           # No smoothing (instant)
62 = 1.0                           # No smoothing (instant)
```

### Configuration Parameters

#### MIDI Section
- **input_port_name**: Name (or partial name) of the physical MIDI controller to connect to
- **virtual_port_name**: Name for the virtual MIDI port created by the proxy

#### Settings Section
- **whitelist_controls**: Comma-separated list of control numbers that bypass smoothing
  - These controls send immediately without filtering
  - Useful for buttons or controls that need instant response
  - Default: `31,51,55,59,61,62`

- **easing_threshold**: Margin used for echo detection (0-127)
  - Used to determine if Modulaser echo messages are in the expected range
  - Default: `3`

- **update_rate_hz**: How often the smoothing thread calculates convergence updates
  - Higher = smoother convergence but more CPU
  - Default: `100` (every 10ms)

#### Smoothing Section
- **default_alpha_min**: Alpha value for slow controller movements (0.0-1.0)
  - Lower = heavier smoothing (more lag, eliminates jitter)
  - Higher = lighter smoothing (more responsive)
  - Default: `0.25`

- **default_alpha_max**: Alpha value for fast controller movements (0.0-1.0)
  - Used when movement velocity exceeds `velocity_threshold_high`
  - Default: `0.8`

- **velocity_threshold_low**: Movement speed below which `alpha_min` is used (0-127)
  - Default: `2`

- **velocity_threshold_high**: Movement speed above which `alpha_max` is used (0-127)
  - Between low and high, alpha is linearly interpolated
  - Default: `10`

- **adaptive_enabled**: Enable velocity-based adaptive alpha (true/false)
  - When `true`, alpha adjusts based on movement speed
  - When `false`, uses `alpha_min` for all movements
  - Default: `true`

#### Smoothing_per_control Section
- Per-control override of smoothing parameters
- **Adaptive format**: `CC = alpha_min,alpha_max,vel_low,vel_high`
  - Example: `18 = 0.2,0.75,2,12` - CC18 uses custom adaptive smoothing
- **Fixed format**: `CC = alpha`
  - Example: `61 = 1.0` - CC61 has no smoothing (instant response)

### Tuning Guide

**If smoothing feels too laggy:**
- Increase `default_alpha_min` (e.g., 0.3-0.4)
- Increase `default_alpha_max` (e.g., 0.85-0.95)
- Lower `velocity_threshold_high` (reach max alpha sooner)

**If controller still feels jerky:**
- Decrease `default_alpha_min` (e.g., 0.1-0.2)
- Lower `velocity_threshold_low` (apply heavy smoothing to more movements)

**Recommended starting values for different use cases:**

- **Conservative (maximum smoothness)**: `alpha_min=0.15, alpha_max=0.6`
- **Balanced (recommended)**: `alpha_min=0.25, alpha_max=0.8`
- **Responsive (minimum smoothing)**: `alpha_min=0.4, alpha_max=0.95`

### Built-in Transformations

The proxy includes special handling for two controls that need instant response with transformations:

- **CC61**: Maps 0-127 → 0-31 (range reduction)
  - Reason: Modulaser's speed parameter goes to 800% at full range, which is never needed
  - This keeps it in a more usable 0-25% range

- **CC62**: Inverts value (127 - value)
  - Reason: Physical slider orientation feels backwards for crossfader
  - Top position (0) = crossfade left, Bottom position (127) = crossfade right

**Important:** These controls bypass ALL smoothing logic and are processed immediately before any mutex locking. Modulaser echo messages for these controls are also ignored to prevent callback thread flooding and maintain buttery-smooth responsiveness.

## LED Control

The proxy includes intelligent LED feedback using the MIDIMix button LEDs:

### Startup Light Show
When the MidiMix connects, the proxy runs a light show sequence:
1. Chase pattern through all LEDs (MUTE row → REC ARM row → BANK buttons)
2. All LEDs flash on together briefly
3. Transition to steady state with permanent LEDs on (notes 24, 25, 26)

### Dynamic LED Control
The proxy responds to MIDI Note On messages from Modulaser to control LEDs:
- **Note velocity > 100**: LED turns ON (indicates active preset)
- **Note velocity ≤ 100**: LED turns OFF (indicates available preset)

This provides visual feedback on the physical controller matching the software state.

### LED Notes Mapping
- **MUTE buttons**: Notes 1, 4, 7, 10, 13, 16, 19, 22
- **REC ARM buttons**: Notes 3, 6, 9, 12, 15, 18, 21, 24
- **BANK LEFT**: Note 25
- **BANK RIGHT**: Note 26

Permanent LEDs (24, 25, 26) remain lit during normal operation to indicate active connection.

### Testing LEDs
A standalone utility is included to test LED functionality:
```bash
make test-leds
./test-leds
```

This interactive tool allows you to:
- Test all known LED buttons
- Scan all possible note numbers (1-127)
- Run a demonstration light show
- Test individual note numbers

## Latch/Pickup Mode

The proxy includes intelligent pickup detection to prevent jarring jumps when physical controls don't match software values (e.g., after a preset change):

### How It Works
1. **Initial State**: When Modulaser reports a control value (via echo), the proxy tracks both:
   - Last known software value (from Modulaser echo)
   - Last known hardware value (from MidiMix)

2. **Pickup Detection**: If hardware and software values diverge (common after preset changes):
   - The control enters "latched" state
   - Physical movements are **not** sent to Modulaser yet
   - Proxy waits for the hardware value to "pick up" the software value

3. **Pickup Threshold**: When the hardware value crosses the software value:
   - Control becomes "unlatched"
   - Normal smoothing behavior resumes
   - Physical movements are filtered and sent smoothly to Modulaser

### Benefits
- **No Jumps**: Prevents sudden parameter changes when touching controls after preset changes
- **Natural Feel**: Physical control must sweep through the current software value to take effect
- **Automatic**: No special mode switching or button presses required
- **Per-Control**: Each control independently tracks its own pickup state

## Example Output

When running in default mode, the proxy displays color-coded, real-time status:

```
[10:23:45.123] ℹ INFO Creating virtual MIDI port: Midi Easing
[10:23:45.234] ℹ INFO Polling for MIDI Mix...
[10:23:46.345] ℹ INFO Connected to MIDI Mix
[10:23:46.456] ⏳ WAITING CC19 waiting for crossover → Modulaser:45 | Controller:82 (Δ37)
[10:23:47.001] ⏳ WAITING CC19 waiting for crossover → Modulaser:45 | Controller:68 (Δ23)
[10:23:47.503] ✓ LATCH CC19 controller crossed Modulaser:45 (was 68 → now 44)
[10:23:47.512] 〰 SMOOTH CC19 raw=44 -> smoothed=44 (alpha=0.25, vel=2)
[10:23:47.522] 〰 SMOOTH CC19 raw=41 -> smoothed=43 (alpha=0.30, vel=3)
[10:23:48.012] ✓ CONVERGE CC19 complete
```

**Color Legend** (when terminal supports ANSI):
- Timestamps: Dim gray
- ⏳ WAITING: Bright yellow - Controller hasn't picked up software value yet
- ✓ LATCH: Bright green - Controller synchronized with software
- 〰 SMOOTH: Cyan - EMA filtering in progress
- ✓ CONVERGE: Green - Smoothing complete, value stabilized
- ⚡ WHITELIST: Bright cyan - Immediate bypass (no smoothing)
- Control numbers (CC19): Bold white
- Modulaser values: Bright magenta
- Controller values: Bright cyan

## How It Works

### Architecture

```
MidiMix Controller → [Transformations] → [EMA Smoothing] → [Convergence Thread] → Virtual Port → Modulaser
                                                                                            ↓
                                                                               Echo Messages (state tracking)
```

### EMA Smoothing Logic

1. **MidiMix sends CC message** (e.g., CC18 value 100)
2. **Apply transformations** (if control is 61 or 62)
3. **Check whitelist**: If whitelisted → send immediately and done
4. **Check if Modulaser state known**: If first message → send immediately
5. **Latch/Pickup Check**: If hardware value doesn't match software value:
   - Enter latched state (suppress sending)
   - Wait for hardware to "pick up" software value
   - Once crossed → unlatch and resume normal operation
6. **Apply EMA smoothing**:
   - Calculate velocity: `velocity = |new_value - last_raw_value|`
   - Determine alpha based on velocity (adaptive mode):
     - `velocity < velocity_threshold_low` → use `alpha_min` (heavy smoothing)
     - `velocity > velocity_threshold_high` → use `alpha_max` (light smoothing)
     - Between thresholds → linear interpolation
   - Apply EMA: `smoothed = alpha × raw + (1-alpha) × smoothed_prev`
   - Send smoothed value to Modulaser
7. **Convergence thread** (runs at 100Hz):
   - If smoothed value hasn't reached target yet
   - Continue applying EMA at `alpha_min` for smooth convergence
   - Send updated values as they change
   - Stop when `smoothed == target`

### EMA (Exponential Moving Average) Explained

EMA is a low-pass filter that smooths noisy input by maintaining a running average:

**Formula:** `smoothed(t) = α × input(t) + (1-α) × smoothed(t-1)`

**Alpha (α) parameter:**
- `α = 0.0`: Maximum smoothing (very slow response, infinite lag)
- `α = 0.5`: Moderate smoothing (balanced)
- `α = 1.0`: No smoothing (instant response, no filtering)

**Adaptive Alpha:**
The proxy adjusts α based on how fast you're moving the controller:
- **Slow movements** (stick-slip jitter): Use low α (heavy smoothing) to eliminate jitter
- **Fast movements** (intentional changes): Use high α (light smoothing) to stay responsive

**Convergence:**
After you stop moving the controller, the smoothing thread continues updating at 100Hz until the smoothed value matches the target. This ensures the output always reaches the intended position, even with heavy smoothing.

### State Management

For each control (0-127), the proxy tracks:
- Last value from MidiMix (hardware position)
- Last value from Modulaser (software state via echo)
- Current smoothed value (floating point for precision)
- Target value (what we're converging to)
- Current alpha (for adaptive smoothing)
- Last raw value (for velocity calculation)
- Whether actively smoothing to convergence
- Latch state (whether control needs to pick up software value)

This state is thread-safe (protected by mutex) and provides:
- Continuous smooth filtering of controller input
- Automatic convergence to final target
- Velocity-adaptive response
- No duplicate messages (only sends when value changes)

## Troubleshooting

### Virtual port not appearing
- Ensure CoreMIDI is working: Open Audio MIDI Setup.app
- Check for permission issues in System Preferences → Security & Privacy
- Try restarting the proxy

### MidiMix not detected
- Verify controller is connected and powered on
- Check connection: `ls /dev/cu.*` (should show MIDI device)
- Proxy polls every second, so wait a moment after connecting

### Smoothing not working or feels wrong
- Verify Modulaser is echoing MIDI messages back to the proxy
- Control may be in latched state - move it to pick up the current software value
- Adjust alpha values in `[Smoothing]` section for desired response
- Try different values: lower alpha = more smoothing, higher alpha = less smoothing
- Enable verbose logging (remove `-q` flag) to see smoothing and convergence messages

### LEDs not working
- Verify MidiMix is properly connected and detected
- Check that the proxy opened the MidiMix output port (shown in verbose logs)
- Try the `test-leds` utility to verify hardware LED functionality
- Ensure Modulaser is sending Note On messages for preset feedback

### Performance issues
- Lower `update_rate_hz` in config.ini (e.g., 50 instead of 100)
- Increase alpha values to reduce smoothing computation
- Use quiet mode (`-q`) to reduce console I/O overhead

## Architecture Details

### Threading Model

- **Main Thread**: MIDI I/O setup, device polling, console output
- **RtMidi Callback Threads**: Handle incoming MIDI messages (audio thread priority)
  - Apply EMA smoothing immediately when MIDI input arrives
  - Send initial smoothed value to Modulaser
- **Smoothing Thread**: Runs at configured Hz (default 100Hz)
  - Continues applying EMA to converge smoothed values toward targets
  - Only active when controls haven't reached their final position
- **Polling Thread**: Checks for MidiMix connection every second

All shared state access is protected by a mutex for thread safety.

### Smoothing Algorithm

Uses **Exponential Moving Average (EMA)** with adaptive alpha:
- Continuous filtering (not target-based)
- No lag accumulation
- Velocity-adaptive response
- Automatic convergence to final value

**Benefits over traditional easing:**
- Always responsive (no "catch-up" lag during continuous movement)
- Smooth slow movements (eliminates stick-slip jitter)
- Fast movements remain snappy
- Simpler implementation (no complex time-based curves)

## Version History

### v2.0 - EMA Smoothing System
- **New**: Exponential Moving Average (EMA) smoothing with adaptive alpha
- **New**: Velocity-based adaptive response (heavy smoothing for slow movements, light for fast)
- **New**: Automatic convergence - smoothed values continue updating until they reach target
- **Improved**: Per-control tuning via config.ini
- **Removed**: Legacy target-based easing system

### v1.x - Target-Based Easing
- Original implementation with SineEaseInOut easing curves
- Fixed duration transitions
- Available in git history for reference

### Legacy Python Implementation
The original Python implementation is available in the `attic/` directory. The C++ version provides:
- 10x lower latency
- <1% CPU usage (vs ~5% for Python)
- More reliable device detection
- Better thread safety
- Native CoreMIDI integration

## Project Structure

```
.
├── midi-easing-proxy.cpp    # Main C++ implementation
├── led_controller.cpp       # LED control implementation
├── led_controller.h         # LED controller header
├── test-leds.cpp            # LED testing utility
├── Makefile                 # Build system (includes test-leds target)
├── config.ini               # Configuration file
├── setup_deps.sh            # Dependency download script
├── README.md                # This file
├── vendor/                  # Third-party libraries (not in git)
│   ├── rtmidi-6.0.0/       # RtMidi library
│   └── AHEasing/           # AHEasing library
└── attic/
    └── midi-easing.py       # Original Python implementation
```

## Credits

- **RtMidi**: Gary P. Scavone - https://github.com/thestk/rtmidi
- **EMA/Smoothing Concept**: Standard digital signal processing technique
- **Original Easing Implementation**: Robert Penner's easing equations (v1.x)

## License

MIT License - Feel free to use and modify
