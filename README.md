# MIDI Easing Proxy

A virtual MIDI proxy that sits between a MIDI controller (Akai MidiMix) and software (Modulaser) to provide smooth easing transitions when control values jump after preset changes.

## Version 2.0 (C++ Implementation)

This is a high-performance C++ rewrite of the original Python implementation, designed specifically for live performance with minimal latency and reliable operation.

## Features

- **Virtual MIDI Port**: Creates "Midi Easing" virtual port that appears as both input and output
- **Automatic Device Detection**: Polls for MidiMix controller and connects automatically
- **LED Control**: Visual feedback using MIDIMix button LEDs with startup light show
- **Smart Easing**: Only applies easing when value differences exceed threshold (default: 3)
- **Latch/Pickup Mode**: Prevents controller jumps - physical controls must "pick up" current software values before taking effect
- **Bidirectional Communication**: Tracks echo messages from Modulaser to maintain accurate state
- **Control Whitelist**: Bypass easing for specific controls that need immediate response
- **Custom Transformations**: Built-in value mapping for specific controls (CC61, CC62)
- **Per-Control Duration**: Configure easing duration for individual controls
- **High Performance**: 100Hz update rate with <1% CPU usage
- **Thread-Safe**: Separate threads for MIDI I/O, polling, and easing calculations

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
4. Start the easing engine at 100Hz
5. Display all MIDI messages (verbose mode)

### Quiet Mode

```bash
./midi-easing-proxy -q
```

Suppresses detailed MIDI message logging, shows only:
- Startup information
- Connection status changes
- Easing start/stop events
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
whitelist_controls = 61,62         # Controls that bypass easing (immediate)
easing_threshold = 3               # Minimum value change to trigger easing
update_rate_hz = 100               # Easing thread update frequency

[DEFAULTS]
default_easing_duration = 5000     # Default easing time in milliseconds

[Easing_durations]
18 = 3000                          # Per-control duration overrides
19 = 1500                          # CC number = duration in milliseconds
20 = 2500
```

### Configuration Parameters

#### MIDI Section
- **input_port_name**: Name (or partial name) of the physical MIDI controller to connect to
- **virtual_port_name**: Name for the virtual MIDI port created by the proxy

#### Settings Section
- **whitelist_controls**: Comma-separated list of control numbers that bypass easing
  - These controls send immediately without smoothing
  - Useful for buttons or controls that need instant response
  - Default: `61,62`

- **easing_threshold**: Minimum value difference to trigger easing (0-127)
  - Changes smaller than this are sent immediately
  - Prevents unnecessary easing on small adjustments
  - Default: `3`

- **update_rate_hz**: How often the easing thread calculates new values
  - Higher = smoother but more CPU
  - Default: `100` (every 10ms)

#### DEFAULTS Section
- **default_easing_duration**: Default time for easing transitions in milliseconds
  - Used for controls not listed in [Easing_durations]
  - Default: `5000` (5 seconds)

#### Easing_durations Section
- Per-control override of easing duration
- Format: `control_number = duration_ms`
- Example: `18 = 3000` means CC18 eases over 3 seconds

### Built-in Transformations

The proxy includes special handling for two controls that need instant response with transformations:

- **CC61**: Maps 0-127 → 0-31 (range reduction)
  - Reason: Modulaser's speed parameter goes to 800% at full range, which is never needed
  - This keeps it in a more usable 0-25% range

- **CC62**: Inverts value (127 - value)
  - Reason: Physical slider orientation feels backwards for crossfader
  - Top position (0) = crossfade left, Bottom position (127) = crossfade right

**Important:** These controls bypass ALL easing logic and are processed immediately before any mutex locking. Modulaser echo messages for these controls are also ignored to prevent callback thread flooding and maintain buttery-smooth responsiveness.

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

3. **Pickup Threshold**: When the hardware value comes within the configured threshold of the software value:
   - Control becomes "unlatched"
   - Normal easing behavior resumes
   - Physical movements are sent smoothly to Modulaser

### Benefits
- **No Jumps**: Prevents sudden parameter changes when touching controls after preset changes
- **Natural Feel**: Physical control must sweep through the current software value to take effect
- **Automatic**: No special mode switching or button presses required
- **Per-Control**: Each control independently tracks its own pickup state

### Configuration
The pickup threshold uses the same `easing_threshold` setting in config.ini:
```ini
[Settings]
easing_threshold = 3   # Also used for pickup detection
```

## How It Works

### Architecture

```
MidiMix Controller → [Transformations] → [Threshold Check] → [Easing Engine] → Virtual Port → Modulaser
                                                                                       ↓
                                                                          Echo Messages (state tracking)
```

### Easing Logic

1. **MidiMix sends CC message** (e.g., CC18 value 100)
2. **Apply transformations** (if control is 61 or 62)
3. **Check whitelist**: If whitelisted → send immediately and done
4. **Check if Modulaser state known**: If first message → send immediately
5. **Latch/Pickup Check**: If hardware value doesn't match software value:
   - Enter latched state (suppress sending)
   - Wait for hardware to "pick up" software value
   - Once within threshold → unlatch and resume normal operation
6. **Calculate difference**: `diff = |new_value - last_modulaser_value|`
7. **Threshold check**:
   - If `diff < easing_threshold` → send immediately
   - If `diff >= easing_threshold` → start easing
8. **Easing thread** (runs at 100Hz):
   - Calculate elapsed time: `t = elapsed / duration`
   - Apply easing function: `eased_t = SineEaseInOut(t)`
   - Interpolate: `value = start + eased_t * (target - start)`
   - Send eased value to Modulaser
   - Stop when `t >= 1.0`

### State Management

For each control (0-127), the proxy tracks:
- Last value from MidiMix (hardware position)
- Last value from Modulaser (software state via echo)
- Current easing target
- Easing start time and duration
- Whether easing is active
- Latch state (whether control needs to pick up software value)

This state is thread-safe (protected by mutex) and allows:
- Smooth transitions when presets change
- Immediate response for small adjustments
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

### Easing not working
- Verify Modulaser is echoing MIDI messages back to the proxy
- Check that value differences exceed `easing_threshold` (default 3)
- Control may be in latched state - move it to pick up the current software value
- Enable verbose logging (remove `-q` flag) to see messages

### LEDs not working
- Verify MidiMix is properly connected and detected
- Check that the proxy opened the MidiMix output port (shown in verbose logs)
- Try the `test-leds` utility to verify hardware LED functionality
- Ensure Modulaser is sending Note On messages for preset feedback

### Performance issues
- Lower `update_rate_hz` in config.ini (e.g., 50 instead of 100)
- Increase `default_easing_duration` to reduce transition frequency
- Use quiet mode (`-q`) to reduce console I/O overhead

## Architecture Details

### Threading Model

- **Main Thread**: MIDI I/O setup, device polling, console output
- **RtMidi Callback Threads**: Handle incoming MIDI messages (audio thread priority)
- **Easing Thread**: Runs at configured Hz, calculates and sends eased values
- **Polling Thread**: Checks for MidiMix connection every second

All shared state access is protected by a mutex for thread safety.

### Easing Function

Uses **SineEaseInOut** from the AHEasing library:
- Smooth acceleration at start
- Constant velocity in middle
- Smooth deceleration at end
- Feels natural for visual parameters

The function is: `sin((t * π / 2) - π/2) * 0.5 + 0.5`

## Legacy Python Implementation

The original Python implementation ([midi-easing.py](midi-easing.py)) is still available in the `attic/` directory for reference. The C++ version provides:
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
- **AHEasing**: Warren Moore - https://github.com/warrenm/AHEasing
- **Easing Functions**: Robert Penner's easing equations

## License

MIT License - Feel free to use and modify
