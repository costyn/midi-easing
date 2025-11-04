# Phase 4: Expert Detail Questions

## Q1: Should we implement this in C++ for better long-term maintainability?
**Default if unknown:** Yes (C++ with RtMidi)

**Context:** You mentioned wanting "something solid that I can maintain myself" and you're familiar with C++. Python is less familiar to you. A C++ implementation would compile to a native binary with no runtime dependencies.

**If Yes:** Single C++ file with RtMidi, simple Makefile, ~300 lines of code
**If No:** Continue with Python and rtmidi

---

## Q2: When a MidiMix control sends a value very close to Modulaser's current value, should we skip easing?
**Default if unknown:** Yes (skip if difference < 3)

**Context:** If you barely touch a knob (value changes by 1-2), easing might feel sluggish. A small threshold (e.g., 3 units) would make tiny adjustments feel more responsive while still smoothing big jumps.

**If Yes:** Will add `easing_threshold = 3` to config.ini
**If No:** All value changes will be eased, regardless of size

---

## Q3: Should the whitelist control transformations (inversion for 61, mapping for 62) be configurable in config.ini?
**Default if unknown:** No (keep hardcoded for now)

**Context:** Current code has hardcoded logic for control 61 (0-127→0-31) and 62 (inversion). Making this configurable adds complexity. You can add it later if needed.

**If No:** Keep transformations hardcoded in source code
**If Yes:** Add `[Transformations]` section to config.ini with per-control mapping rules

---

## Q4: Should the proxy log MIDI messages to console for debugging?
**Default if unknown:** Yes (verbose mode with command-line flag)

**Context:** Debugging MIDI issues requires seeing message flow. A `-v` or `--verbose` flag could enable detailed logging (like your current rtimidi-listen.py output) while keeping it quiet by default.

**If Yes:** Add verbose mode (e.g., `-v` flag or `verbose = true` in config)
**If No:** Minimal logging only (startup/errors)

---

## Q5: When Modulaser isn't running, should the proxy still start and wait for connection?
**Default if unknown:** Yes (start successfully, wait for Modulaser)

**Context:** Virtual MIDI ports don't require the "client" app to be running. The proxy can create the port first, then you launch Modulaser and connect to it. This is more flexible than requiring a specific startup order.

**If Yes:** Proxy creates virtual port and waits for Modulaser to connect
**If No:** Require Modulaser to be running first (would need connection checking)
