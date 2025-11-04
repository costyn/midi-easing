# Phase 2: Discovery Answers

## Q1: Should the proxy run as a persistent background service/daemon?
**Answer:** No

**Implications:**
- Simple foreground CLI process
- Start with command, stop with Ctrl+C
- No systemd/launchd integration needed
- Simpler error handling and logging

---

## Q2: Do you need a visual interface or configuration GUI?
**Answer:** No

**Implications:**
- Pure command-line application
- Configuration via config.ini file
- Console output for status/debugging
- No GUI framework dependencies

---

## Q3: Should easing automatically disable when you stop moving a control for X seconds?
**Answer:** No

**Implications:**
- Easing runs to completion once started
- Simpler state machine
- Predictable behavior
- Use whitelist for controls needing immediate response

---

## Q4: Should the proxy support multiple MIDI controller devices simultaneously?
**Answer:** No

**Implications:**
- Single input device (MidiMix) only
- Simpler routing logic
- No device management complexity
- Can be extended later if needed

---

## Q5: Do you need hot-reloading of configuration without restarting the proxy?
**Answer:** No

**Implications:**
- Config loaded once at startup
- No file watching required
- Restart required for config changes
- Simpler implementation

---

## Q6: Should messages from Modulaser back to the proxy update the easing state?
**Answer:** Yes

**Implications:**
- Track Modulaser's echo messages as current state
- Use echoed value as starting point for next ease
- Enables smooth transitions after preset changes
- Core to solving the jump problem

---

## Q7: Should the proxy remember state across restarts (persist to disk)?
**Answer:** No

**Implications:**
- Fresh state on each startup
- Learn current state from Modulaser echoes
- No persistence layer needed
- Simpler, more predictable behavior
