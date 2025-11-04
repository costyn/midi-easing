# Phase 4: Expert Detail Answers

## Q1: Should we implement this in C++ for better long-term maintainability?
**Answer:** Yes

**Implications:**
- Implement in C++ with RtMidi library
- Use AHEasing library (user already familiar from Arduino projects)
- Native binary, no runtime dependencies
- Simple Makefile for building
- ~300 lines of well-structured C++ code
- Better long-term maintainability for user

---

## Q2: When a MidiMix control sends a value very close to Modulaser's current value, should we skip easing?
**Answer:** Yes

**Implications:**
- Add `easing_threshold = 3` to config.ini (configurable)
- Skip easing if abs(new_value - current_value) < threshold
- Makes small adjustments feel more responsive
- Big jumps (like after preset changes) still get smoothed
- Python script already checks for value changes before sending

---

## Q3: Should the whitelist control transformations (inversion for 61, mapping for 62) be configurable in config.ini?
**Answer:** No

**Implications:**
- Keep control 61 mapping (0-127 → 0-31) hardcoded
- Keep control 62 inversion (127 - value) hardcoded
- Simpler implementation
- Can be made configurable later if needed (YAGNI principle)

---

## Q4: Should the proxy log MIDI messages to console for debugging?
**Answer:** No (modified - default verbose, add `-q` for quiet)

**Implications:**
- Default behavior: Verbose logging (like current rtimidi-listen.py)
- Shows all MIDI messages received/sent
- Add `-q` or `--quiet` flag to suppress detailed logging
- Quiet mode shows only: startup messages, errors, warnings
- Easier debugging by default

---

## Q5: When Modulaser isn't running, should the proxy still start and wait for connection?
**Answer:** Yes (with enhancements)

**Implications:**
- Proxy creates virtual MIDI port at startup (no dependencies)
- Polls for MidiMix device connection every second
- Works regardless of startup order:
  1. Start proxy first, then MidiMix, then Modulaser ✓
  2. Start MidiMix, then proxy, then Modulaser ✓
  3. Any order ✓
- Automatically detects when MidiMix connects/disconnects
- Console messages indicate connection status
- Use proper sleep/polling to avoid CPU hogging
  - std::this_thread::sleep_for() in C++
  - Not busy-waiting like Arduino delay()
