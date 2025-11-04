# Phase 2: Discovery Questions

## Q1: Should the proxy run as a persistent background service/daemon?
**Default if unknown:** No (runs as foreground command-line process)

**Rationale:** Most audio/MIDI utilities run as foreground processes that you start when needed and stop with Ctrl+C. Background services add complexity for process management and logging. A foreground CLI tool is simpler to develop, debug, and control.

---

## Q2: Do you need a visual interface or configuration GUI?
**Default if unknown:** No (command-line only with config file)

**Rationale:** You mentioned wanting something maintainable and command-line is sufficient. Config file (INI) provides all needed configuration. Visual interfaces add significant complexity and dependencies.

---

## Q3: Should easing automatically disable when you stop moving a control for X seconds?
**Default if unknown:** No (easing completes to target value once started)

**Rationale:** Current behavior is to ease to target and stop. Auto-disable adds state machine complexity and might cause unexpected behavior. User can use whitelisted controls if immediate response is needed.

---

## Q4: Should the proxy support multiple MIDI controller devices simultaneously?
**Default if unknown:** No (single MidiMix controller only)

**Rationale:** Requirements specify Akai MidiMix controller only. Multi-device support adds routing complexity. Can be added later if needed, but YAGNI principle suggests starting simple.

---

## Q5: Do you need hot-reloading of configuration without restarting the proxy?
**Default if unknown:** No (restart required for config changes)

**Rationale:** Adding file watching and live config reload adds complexity. Restarting a CLI tool is fast and simple. Config changes are infrequent during actual performance/use.

---

## Q6: Should messages from Modulaser back to the proxy update the easing state?
**Default if unknown:** Yes (Modulaser echoes inform current parameter values)

**Rationale:** This is core to the solution - we need to know Modulaser's current parameter value to ease from it. When Modulaser echoes a value, that becomes our starting point for the next ease.

---

## Q7: Should the proxy remember state across restarts (persist to disk)?
**Default if unknown:** No (fresh state on each startup)

**Rationale:** MIDI state is transient and Modulaser's parameter values change between sessions. Clean state on startup is simpler and more predictable. The proxy will learn current state from Modulaser's echo messages after startup.
