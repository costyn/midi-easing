# EMA Smoothing Implementation Plan

## Executive Summary

Replace the current target-based easing system with an Exponential Moving Average (EMA) filter with adaptive alpha. This provides continuous smoothing appropriate for live controller input, eliminating lag during rapid movements while smoothing out jerky stick-slip behavior during slow movements.

---

## Problem Statement

### Current System Issues

The existing "target-based easing" system was designed for discrete value changes (e.g., after preset switches) but performs poorly with continuous controller input:

**Symptoms:**
- Lag accumulation during rapid controller movements
- Unnatural "catch-up" behavior when target keeps changing
- User reported: "when moving the knob a bit faster, it seems to lag/wait and then suddenly start the easing"

**Root Cause:**
The current system:
1. Detects a jump larger than threshold (e.g., 7 units)
2. Starts easing from current position to target over 3000ms
3. If controller keeps moving, just updates the target
4. Easing curve is still based on ORIGINAL start time/position
5. Creates lag because easing hasn't caught up yet

**Example from user's log:**
```
[08:35:31.618] [EASING] CC18 43 -> 36 over 3000ms
[08:35:31.618] [EASING] CC18 updated target to 34
[08:35:31.618] [EASING] CC18 updated target to 32
[08:35:31.618] [EASING] CC18 updated target to 30
... (many more target updates)
[08:35:34.626] [EASING] CC18 completed  ← 3 seconds later!
```

### User Requirements

**Primary Goal:** Smooth out jerky controller movements during live performance
- Controllers have stick-slip behavior during slow movements
- Creates noticeable jerky changes in animation parameters
- Especially problematic with physical sliders

**Priority:**
1. **CRITICAL:** Smooth slow movements (eliminate stick-slip jerkiness)
2. **IMPORTANT:** Fast movements must remain responsive (for musical timing)
3. **NICE:** Per-control tuning capability

---

## Solution: EMA with Adaptive Alpha

### Why EMA?

Exponential Moving Average is ideal for continuous input streams:
- No concept of "target" or "duration"
- Always responsive (no lag accumulation)
- Simple, predictable behavior
- Computationally efficient
- Works perfectly with continuous controller input

### EMA Algorithm

**Basic formula:**
```
smoothed_value(t) = α × raw_value(t) + (1-α) × smoothed_value(t-1)
```

**Parameters:**
- `α` (alpha): Smoothing factor, range [0, 1]
  - α = 1.0: No smoothing (instant response)
  - α = 0.0: Maximum smoothing (very slow response)
  - α = 0.3-0.5: Good balance for most use cases

### Adaptive Alpha (Velocity-Based)

**Problem:** Fixed alpha doesn't distinguish between:
- Slow jerky movements (need heavy smoothing)
- Fast intentional movements (need light smoothing)

**Solution:** Adjust alpha based on movement velocity:

```cpp
// Calculate velocity (rate of change)
int velocity = abs(new_raw_value - last_raw_value);

// Map velocity to alpha
if (velocity < velocity_threshold_low) {
    // Slow movement: Heavy smoothing
    alpha = alpha_min;  // e.g., 0.2
} else if (velocity > velocity_threshold_high) {
    // Fast movement: Light smoothing (more responsive)
    alpha = alpha_max;  // e.g., 0.8
} else {
    // Medium movement: Linear interpolation
    alpha = map(velocity, velocity_threshold_low, velocity_threshold_high,
                alpha_min, alpha_max);
}

// Apply EMA
smoothed_value = alpha * new_raw_value + (1.0 - alpha) * smoothed_value;
```

**Benefits:**
- Slow movements get heavy filtering (smooth stick-slip)
- Fast movements get light filtering (responsive)
- No lag accumulation
- Always tracks current input

---

## Technical Specification

### ControlState Structure Changes

**Remove (old easing system):**
```cpp
uint8_t current_target = 0;
double easing_start_time = 0.0;
uint32_t easing_duration = 5000;
bool is_easing = false;
```

**Add (EMA system):**
```cpp
// EMA smoothing state
float smoothed_value = 0.0f;      // Current smoothed output value
uint8_t last_raw_value = 0;       // Last raw input value (for velocity calc)
double last_update_time = 0.0;    // For time-based velocity calculation
float alpha = 0.5f;               // Current alpha value (can be adaptive)

// EMA configuration (per-control)
float alpha_min = 0.2f;           // Alpha for slow movements
float alpha_max = 0.8f;           // Alpha for fast movements
uint8_t velocity_threshold_low = 2;   // Below this: use alpha_min
uint8_t velocity_threshold_high = 10; // Above this: use alpha_max
bool adaptive_enabled = true;     // Use adaptive alpha
```

### Config Structure Changes

**Remove:**
```ini
[DEFAULTS]
default_easing_duration = 3000

[Easing_durations]
18 = 3000
20 = 2500
```

**Add:**
```ini
[Smoothing]
# Global EMA settings
default_alpha_min = 0.2      # Heavy smoothing for slow movements
default_alpha_max = 0.8      # Light smoothing for fast movements
velocity_threshold_low = 2   # Below this: heavy smoothing
velocity_threshold_high = 10 # Above this: light smoothing
adaptive_enabled = true      # Use velocity-based adaptive alpha

[Smoothing_per_control]
# Per-control alpha overrides (format: CC = alpha_min,alpha_max,vel_low,vel_high)
# Or simple format: CC = fixed_alpha (no adaptive)
18 = 0.15,0.7,2,12   # More smoothing for CC18
20 = 0.25,0.75,3,10  # Less smoothing for CC20
61 = 1.0              # No smoothing (instant, already whitelisted)
```

### Code Flow Changes

**Old Flow (Target-based easing):**
```
MidiMix Input
  ↓
Check threshold (diff > 3)
  ↓ YES                    ↓ NO
Start/Update Easing    Send immediately
  ↓
Easing Thread (3000ms)
  ↓
Send to Modulaser
```

**New Flow (EMA smoothing):**
```
MidiMix Input
  ↓
Calculate velocity
  ↓
Determine alpha (adaptive or fixed)
  ↓
Apply EMA filter
  ↓
Send smoothed value immediately
(No separate thread needed for smoothing)
```

### Function Changes

#### 1. **Remove `easingThreadFunc()`**
- No longer needed
- EMA applies smoothing instantly in the callback

#### 2. **Modify `midimixCallback()`**
- Replace easing logic with EMA application
- Calculate velocity: `abs(value - state.last_raw_value)`
- Determine alpha (adaptive or fixed)
- Apply EMA: `smoothed = α × value + (1-α) × smoothed`
- Send rounded smoothed value immediately

#### 3. **Keep `sendMidiCC()`**
- Unchanged (still tracks send window for echo detection)

#### 4. **Update `parseConfig()`**
- Remove easing_duration parsing
- Add smoothing parameter parsing
- Parse per-control smoothing overrides

---

## Implementation Steps

### Phase 1: Add EMA Structure (No Behavior Change)

**Goal:** Add EMA fields to ControlState without breaking existing functionality

1. **Update ControlState struct** ([midi-easing-proxy.cpp:47-61](midi-easing-proxy.cpp#L47-L61))
   - Add EMA fields (smoothed_value, alpha, etc.)
   - Keep old easing fields (for now)

2. **Update Config struct** ([midi-easing-proxy.cpp:31-40](midi-easing-proxy.cpp#L31-L40))
   - Add smoothing config fields
   - Keep old easing config fields (for now)

3. **Build and test** - Should compile and run with no behavior change

### Phase 2: Implement EMA Logic (Parallel to Easing)

**Goal:** Add EMA smoothing as an alternative path (selectable via config)

1. **Add EMA helper function**
   ```cpp
   uint8_t applyEMASmoothing(ControlState& state, uint8_t raw_value, const Config& config) {
       // Calculate velocity
       int velocity = std::abs((int)raw_value - (int)state.last_raw_value);

       // Determine alpha (adaptive or fixed)
       float alpha = state.alpha;  // Default/fixed
       if (state.adaptive_enabled) {
           if (velocity < state.velocity_threshold_low) {
               alpha = state.alpha_min;
           } else if (velocity > state.velocity_threshold_high) {
               alpha = state.alpha_max;
           } else {
               // Linear interpolation
               float t = (float)(velocity - state.velocity_threshold_low) /
                        (state.velocity_threshold_high - state.velocity_threshold_low);
               alpha = state.alpha_min + t * (state.alpha_max - state.alpha_min);
           }
       }

       // Initialize smoothed_value on first use
       if (state.smoothed_value == 0.0f && state.last_raw_value == 0) {
           state.smoothed_value = raw_value;
       }

       // Apply EMA
       state.smoothed_value = alpha * raw_value + (1.0f - alpha) * state.smoothed_value;
       state.last_raw_value = raw_value;

       // Round to nearest integer
       return (uint8_t)(state.smoothed_value + 0.5f);
   }
   ```

2. **Add config option to select mode**
   ```ini
   [Settings]
   smoothing_mode = ema  # or "easing" for old behavior
   ```

3. **Modify midimixCallback()** ([midi-easing-proxy.cpp:434-459](midi-easing-proxy.cpp#L434-L459))
   - Add conditional: `if (config.smoothing_mode == "ema")`
   - Call `applyEMASmoothing()` and send immediately
   - Else: use old easing logic

4. **Build and test both modes**

### Phase 3: Parse Smoothing Config

**Goal:** Load EMA parameters from config.ini

1. **Update parseConfig()** ([midi-easing-proxy.cpp:467-529](midi-easing-proxy.cpp#L467-L529))
   - Add `[Smoothing]` section parsing
   - Add `[Smoothing_per_control]` section parsing
   - Initialize per-control states with config values

2. **Create example config.ini**
   ```ini
   [Settings]
   smoothing_mode = ema

   [Smoothing]
   default_alpha_min = 0.2
   default_alpha_max = 0.8
   velocity_threshold_low = 2
   velocity_threshold_high = 10
   adaptive_enabled = true

   [Smoothing_per_control]
   18 = 0.15,0.7,2,12
   20 = 0.3
   ```

3. **Build and test** - Verify config loads correctly

### Phase 4: Remove Old Easing System

**Goal:** Clean up deprecated code once EMA is proven

1. **Remove from ControlState:**
   - `current_target`, `easing_start_time`, `easing_duration`, `is_easing`

2. **Remove from Config:**
   - `default_easing_duration`, `easing_durations` map

3. **Remove functions:**
   - `easingThreadFunc()`
   - `logEasingStart()`, `logEasingStop()`, `logEasingUpdate()`

4. **Remove from main():**
   - Easing thread creation/join

5. **Update config.ini:**
   - Remove `[DEFAULTS]` and `[Easing_durations]` sections

6. **Build and test** - Verify everything still works

### Phase 5: Update Tests

**Goal:** Adapt test suite for EMA behavior

1. **Update test-midi-proxy.cpp:**
   - Remove easing-specific tests
   - Add EMA smoothing tests:
     - Test fixed alpha smoothing
     - Test adaptive alpha (slow vs fast movements)
     - Test per-control configuration
     - Test smoothed value convergence

2. **Example test:**
   ```cpp
   TEST(test_ema_slow_movement_heavy_smoothing) {
       resetTestState();
       test_config.smoothing_mode = "ema";
       test_state.alpha_min = 0.2;
       test_state.alpha_max = 0.8;
       test_state.velocity_threshold_low = 2;
       test_state.velocity_threshold_high = 10;

       // Simulate slow movement (velocity = 1)
       simulateMidiMixMessage(19, 50);
       simulateMidiMixMessage(19, 51);  // velocity = 1, should use alpha_min

       // Check that smoothing was applied (not instant)
       ASSERT_LT(sent_messages.back().second, 51);  // Should be < 51
       ASSERT_GT(sent_messages.back().second, 50);  // Should be > 50
   }
   ```

3. **Build and run tests**

---

## Testing Strategy

### Unit Tests

1. **Test EMA convergence**
   - Input: constant value (50)
   - Expected: smoothed_value → 50 over time

2. **Test adaptive alpha**
   - Slow movement (velocity=1): Uses alpha_min
   - Fast movement (velocity=15): Uses alpha_max
   - Medium movement (velocity=5): Interpolates

3. **Test per-control config**
   - CC18: Different alpha values
   - CC20: Different alpha values
   - Verify they don't interfere

### Integration Tests

1. **Test with actual MIDI controller**
   - Slow slider movement: Should see smooth animation
   - Fast slider movement: Should see responsive tracking
   - Check Modulaser output for smoothness

2. **Test stick-slip scenario**
   - Move slider very slowly
   - Verify output doesn't jump/jerk
   - Compare with old easing system

### Performance Tests

1. **CPU usage**
   - Monitor with Activity Monitor
   - Should be lower than old system (no easing thread)

2. **Latency**
   - Measure input-to-output delay
   - Should be ~10ms (update_rate_hz = 100)

---

## Configuration Examples

### Example 1: Conservative (Heavy Smoothing)

```ini
[Settings]
smoothing_mode = ema

[Smoothing]
default_alpha_min = 0.15     # Very heavy smoothing for slow movements
default_alpha_max = 0.6      # Moderate smoothing even for fast movements
velocity_threshold_low = 3   # Wider slow range
velocity_threshold_high = 15
adaptive_enabled = true
```

**Use case:** Minimizing ALL jitter, acceptable lag for fast movements

### Example 2: Responsive (Light Smoothing)

```ini
[Smoothing]
default_alpha_min = 0.4      # Light smoothing for slow movements
default_alpha_max = 0.95     # Almost no smoothing for fast movements
velocity_threshold_low = 1   # Narrow slow range
velocity_threshold_high = 5
adaptive_enabled = true
```

**Use case:** Maximum responsiveness, minimal smoothing

### Example 3: Balanced (Recommended)

```ini
[Smoothing]
default_alpha_min = 0.25     # Good smoothing for slow movements
default_alpha_max = 0.8      # Still responsive for fast movements
velocity_threshold_low = 2
velocity_threshold_high = 10
adaptive_enabled = true

[Smoothing_per_control]
18 = 0.2,0.75,2,12    # Extra smoothing for problematic control
61 = 1.0              # No smoothing (instant)
```

**Use case:** Good default for most scenarios

### Example 4: Fixed Alpha (Non-Adaptive)

```ini
[Smoothing]
default_alpha_min = 0.4
adaptive_enabled = false      # Use fixed alpha_min for all movements

[Smoothing_per_control]
18 = 0.3     # Simple fixed alpha for CC18
20 = 0.5     # Simple fixed alpha for CC20
```

**Use case:** Predictable, consistent smoothing

---

## Code References

### Files to Modify

1. **[midi-easing-proxy.cpp](midi-easing-proxy.cpp)**
   - `ControlState` struct (line ~47)
   - `Config` struct (line ~31)
   - `midimixCallback()` (line ~434)
   - `parseConfig()` (line ~467)
   - `easingThreadFunc()` (line ~465) - **REMOVE**
   - `main()` (line ~569) - Remove easing thread

2. **[config.ini](config.ini)**
   - Replace `[DEFAULTS]` and `[Easing_durations]` with `[Smoothing]` and `[Smoothing_per_control]`

3. **[test-midi-proxy.cpp](test-midi-proxy.cpp)**
   - Update test structure to match new ControlState
   - Replace easing tests with EMA tests

4. **[Makefile](Makefile)** - No changes needed

### Key Functions

#### Current Functions to Modify

```cpp
// midi-easing-proxy.cpp:201-374
void midimixCallback(double deltatime, std::vector<unsigned char> *message, void *userData)
// Line ~434-459: Replace easing logic with EMA

// midi-easing-proxy.cpp:467-529
bool parseConfig(const std::string& filename)
// Add Smoothing section parsing
```

#### Current Functions to Remove

```cpp
// midi-easing-proxy.cpp:465-520
void easingThreadFunc()
// Delete entire function

// Logging functions (if only used for easing):
void logEasingStart(uint8_t control, uint8_t from, uint8_t to, uint32_t duration)
void logEasingStop(uint8_t control)
void logEasingUpdate(uint8_t control, uint8_t value)
```

#### New Functions to Add

```cpp
uint8_t applyEMASmoothing(ControlState& state, uint8_t raw_value, const Config& config)
// Implement EMA with adaptive alpha

void logSmoothing(uint8_t control, uint8_t raw, uint8_t smoothed, float alpha)
// Optional: Debug logging for smoothing
```

---

## Migration Path (Backward Compatibility)

### Option 1: Dual Mode (Recommended for Testing)

Keep both systems temporarily:
```ini
[Settings]
smoothing_mode = ema  # or "easing"
```

Users can switch back to old behavior if needed.

### Option 2: Clean Break

Remove old system entirely, force migration to EMA.

**Recommendation:** Use Option 1 during testing, then remove old system in a future commit once EMA is proven stable.

---

## Expected Behavior Changes

### What Users Will Notice

**Improvements:**
- ✅ No more lag during rapid movements
- ✅ Smooth, continuous response (no "catch-up" jumps)
- ✅ Better feel for live performance
- ✅ Configurable smoothing amount

**Differences from Old System:**
- ⚠️ ALL movements are smoothed (not just jumps > threshold)
- ⚠️ No discrete "easing" events (continuous filtering)
- ⚠️ Different config parameters (alpha instead of duration)

### Tuning Guide for Users

**If output feels too laggy:**
- Increase `default_alpha_min` (more responsive for slow movements)
- Increase `default_alpha_max` (more responsive for fast movements)
- Lower `velocity_threshold_high` (reach max alpha sooner)

**If output still feels jerky:**
- Decrease `default_alpha_min` (more smoothing for slow movements)
- Lower `velocity_threshold_low` (apply heavy smoothing to more movements)

**Recommended starting values:**
- `alpha_min = 0.25` (moderate slow smoothing)
- `alpha_max = 0.8` (mostly responsive for fast)
- `velocity_threshold_low = 2` (small movements are "slow")
- `velocity_threshold_high = 10` (large movements are "fast")

---

## Success Criteria

### Must Have
- ✅ Smooth output during slow controller movements (no stick-slip jitter)
- ✅ Responsive output during fast controller movements (no noticeable lag)
- ✅ Configurable per-control smoothing
- ✅ Compiles and runs without errors
- ✅ Echo detection still works (from recent changes)

### Should Have
- ✅ Tests pass for EMA behavior
- ✅ CPU usage lower than old system
- ✅ Easy to tune via config.ini

### Nice to Have
- ⚪ Live config reload (change config without restart)
- ⚪ Debug mode showing raw vs smoothed values
- ⚪ Automatic alpha tuning based on controller characteristics

---

## Potential Issues and Solutions

### Issue 1: Initial Value Jump
**Problem:** First smoothed value might jump from 0 to input value
**Solution:** Initialize `smoothed_value = first_raw_value` on first input

### Issue 2: Controller Direction Changes
**Problem:** EMA has lag when changing direction
**Solution:** Adaptive alpha already helps; fast direction changes use high alpha

### Issue 3: Different Update Rates
**Problem:** EMA behavior depends on how fast updates arrive
**Solution:** Consider time-based alpha adjustment (advanced)
```cpp
// Time-based alpha (optional enhancement)
double time_constant = 0.1;  // seconds
double dt = now_ms - state.last_update_time;
float alpha = 1.0 - exp(-dt / (1000.0 * time_constant));
```

### Issue 4: Integer Rounding Artifacts
**Problem:** Rounding float to uint8_t can cause quantization
**Solution:** Use proper rounding: `(uint8_t)(smoothed + 0.5f)`

### Issue 5: Controller at Rest Still Changing
**Problem:** EMA might send slight changes when controller is steady
**Solution:** Add dead zone: Don't send if `abs(new_smoothed - last_sent) < 1`

---

## Appendix: EMA Math Explained

### Continuous vs Discrete

**Continuous (ideal):**
```
y(t) = α × x(t) + (1-α) × y(t-1)
```

**Discrete (our implementation):**
```cpp
smoothed_value = alpha * raw_value + (1.0f - alpha) * smoothed_value;
```

### Relationship to Time Constant

EMA alpha relates to time constant τ:
```
α = 1 - e^(-Δt / τ)
```

For our use case:
- Δt = 1 / update_rate_hz = 10ms (for 100Hz)
- τ = desired smoothing time constant

**Example:**
- Want τ = 50ms smoothing
- α = 1 - e^(-10/50) ≈ 0.18

### Frequency Response

EMA is a first-order low-pass filter:
- Cutoff frequency: fc = 1 / (2π τ)
- Higher alpha → higher cutoff → less smoothing
- Lower alpha → lower cutoff → more smoothing

---

## Questions for Implementation

Before starting, consider:

1. **Should velocity be calculated per-sample or over multiple samples?**
   - Per-sample: `velocity = abs(value[t] - value[t-1])`
   - Multi-sample: `velocity = abs(value[t] - value[t-N])` (more stable)
   - **Recommendation:** Start with per-sample, add multi-sample if needed

2. **Should there be a maximum smoothing time?**
   - Prevent extremely slow alpha values
   - **Recommendation:** Clamp `alpha_min >= 0.1` (prevents infinite lag)

3. **Should smoothing apply to whitelisted controls?**
   - Currently whitelisted controls bypass all processing
   - **Recommendation:** Keep bypass behavior for compatibility

4. **Should there be visual feedback for current alpha value?**
   - Could log alpha in debug mode
   - **Recommendation:** Add optional debug logging

---

## Resources

### Reference Implementations
- **Moving Average Filters:** https://en.wikipedia.org/wiki/Exponential_smoothing
- **One Euro Filter:** https://cristal.univ-lille.fr/~casiez/1euro/
- **Audio DSP Filters:** https://github.com/vinniefalco/DSPFilters

### Related Reading
- Control smoothing in VR/AR systems
- MIDI controller signal conditioning
- Real-time filter design

---

## Conclusion

This plan provides a complete specification for replacing the target-based easing system with EMA smoothing. The EMA approach is fundamentally better suited for continuous controller input and will provide:

- ✅ Smooth slow movements (primary goal)
- ✅ Responsive fast movements (secondary goal)
- ✅ No lag accumulation (eliminates current problem)
- ✅ Simpler code (removes easing thread and state machine)
- ✅ Better performance (less CPU, no thread overhead)

Implementation can proceed in phases, allowing for testing and validation at each step. The dual-mode approach allows for backward compatibility during the transition period.

**Estimated implementation time:** 4-6 hours for experienced C++ developer

**Testing time:** 2-3 hours with actual hardware

**Total:** ~1 day for complete implementation and validation
