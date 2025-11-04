# MIDI Easing Proxy

A virtual MIDI proxy that sits between a MIDI controller (Akai MidiMix) and software (Modulaser) to provide smooth easing transitions when control values jump after preset changes.

## Version 2.0 (C++ Implementation)

This is a high-performance C++ rewrite of the original Python implementation, designed specifically for live performance with minimal latency and reliable operation.

## Features

- **Virtual MIDI Port**: Creates "Midi Easing" virtual port that appears as both input and output
- **Automatic Device Detection**: Polls for MidiMix controller and connects automatically
- **Smart Easing**: Only applies easing when value differences exceed threshold (default: 3)
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
3. Start the easing engine at 100Hz
4. Display all MIDI messages (verbose mode)

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
5. **Calculate difference**: `diff = |new_value - last_modulaser_value|`
6. **Threshold check**:
   - If `diff < easing_threshold` → send immediately
   - If `diff >= easing_threshold` → start easing
7. **Easing thread** (runs at 100Hz):
   - Calculate elapsed time: `t = elapsed / duration`
   - Apply easing function: `eased_t = SineEaseInOut(t)`
   - Interpolate: `value = start + eased_t * (target - start)`
   - Send eased value to Modulaser
   - Stop when `t >= 1.0`

### State Management

For each control (0-127), the proxy tracks:
- Last value from MidiMix
- Last value from Modulaser (echo)
- Current easing target
- Easing start time and duration
- Whether easing is active

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
- Enable verbose logging (remove `-q` flag) to see messages

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
├── Makefile                 # Build system
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
