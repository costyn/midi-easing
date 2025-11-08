# macOS Menu Bar Status Item Implementation Plan

## Overview

Add a native macOS menu bar status item to display real-time MIDI control state during live performance. This provides an always-visible, unobtrusive status display without requiring a terminal window.

## Goals

- Show current latch/waiting state at a glance
- Display Modulaser vs Controller values for active controls
- Minimal visual footprint (menu bar only, no dock icon)
- Non-blocking integration with existing thread model
- Zero external dependencies (native macOS AppKit)

## Architecture

### Component Overview

```
┌─────────────────────────────────────────┐
│  midi-easing-proxy.cpp (Main App)      │
│  ┌─────────────────────────────────┐   │
│  │ Main Thread                     │   │
│  │ - Setup, polling, console       │   │
│  └─────────────────────────────────┘   │
│  ┌─────────────────────────────────┐   │
│  │ MIDI Callback Threads           │   │
│  │ - RtMidi message handling       │   │
│  └─────────────────────────────────┘   │
│  ┌─────────────────────────────────┐   │
│  │ Smoothing Thread (100Hz)        │   │
│  │ - EMA calculations, output      │   │
│  └─────────────────────────────────┘   │
└─────────────────────────────────────────┘
                 │
                 │ C++ Interface
                 ▼
┌─────────────────────────────────────────┐
│  status_bar.mm (Objective-C++ Wrapper) │
│  ┌─────────────────────────────────┐   │
│  │ NSApplication Event Loop        │   │
│  │ - Runs in separate thread       │   │
│  └─────────────────────────────────┘   │
│  ┌─────────────────────────────────┐   │
│  │ NSStatusItem                    │   │
│  │ - Menu bar button + menu        │   │
│  └─────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

### Threading Model

**New Thread**: Cocoa Event Loop
- Runs `[NSApp run]` in dedicated thread
- Updates UI from main thread via performSelectorOnMainThread
- Non-blocking, coexists with RtMidi and smoothing threads

**Thread Safety**:
- Status updates queued via GCD (Grand Central Dispatch)
- No shared state between C++ and Obj-C++ except update queue
- NSStatusItem updates must happen on main thread

## Implementation Steps

### Phase 1: Minimal Objective-C++ Wrapper

**File**: `status_bar.h` (C++ header)

```cpp
#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <stdint.h>
#include <string>

// Initialize menu bar (call once at startup)
void initStatusBar();

// Update status text (thread-safe, call from any thread)
void updateStatusText(const std::string& text);

// Update specific control state
void updateControlState(uint8_t control, uint8_t modulaser_val,
                        uint8_t controller_val, bool is_latched);

// Start Cocoa event loop (blocks until app quits)
void runStatusBarEventLoop();

// Cleanup
void shutdownStatusBar();

#endif
```

**File**: `status_bar.mm` (Objective-C++ implementation)

```objective-cpp
#import <Cocoa/Cocoa.h>
#include "status_bar.h"
#include <map>
#include <sstream>

static NSStatusItem *statusItem = nil;
static NSMenu *statusMenu = nil;

struct ControlInfo {
    uint8_t modulaser_val;
    uint8_t controller_val;
    bool is_latched;
};

static std::map<uint8_t, ControlInfo> activeControls;

void initStatusBar() {
    // Initialize NSApplication (required for GUI elements)
    [NSApplication sharedApplication];

    // Hide dock icon (status bar only)
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    // Create status item in menu bar
    statusItem = [[NSStatusBar systemStatusBar]
                  statusItemWithLength:NSVariableStatusItemLength];

    // Set initial title
    statusItem.button.title = @"MIDI: Ready";

    // Create menu
    statusMenu = [[NSMenu alloc] init];
    [statusMenu addItemWithTitle:@"No active controls"
                          action:nil
                   keyEquivalent:@""];
    [statusMenu addItem:[NSMenuItem separatorItem]];
    [statusMenu addItemWithTitle:@"Quit"
                          action:@selector(terminate:)
                   keyEquivalent:@"q"];

    statusItem.menu = statusMenu;
}

void updateStatusTextOnMainThread(NSString *text) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (statusItem) {
            statusItem.button.title = text;
        }
    });
}

void updateStatusText(const std::string& text) {
    NSString *nsText = [NSString stringWithUTF8String:text.c_str()];
    updateStatusTextOnMainThread(nsText);
}

void updateControlState(uint8_t control, uint8_t modulaser_val,
                        uint8_t controller_val, bool is_latched) {
    // Update map
    activeControls[control] = {modulaser_val, controller_val, is_latched};

    // Build status text
    std::ostringstream oss;
    if (activeControls.empty()) {
        oss << "MIDI: Ready";
    } else if (activeControls.size() == 1) {
        auto& info = activeControls.begin()->second;
        oss << "CC" << (int)control << ": M:" << (int)info.modulaser_val
            << " C:" << (int)info.controller_val
            << (info.is_latched ? " ✓" : " ⏳");
    } else {
        oss << "MIDI: " << activeControls.size() << " active";
    }

    updateStatusText(oss.str());

    // Update menu (on main thread)
    dispatch_async(dispatch_get_main_queue(), ^{
        [statusMenu removeAllItems];

        if (activeControls.empty()) {
            [statusMenu addItemWithTitle:@"No active controls"
                                  action:nil
                           keyEquivalent:@""];
        } else {
            for (auto& [cc, info] : activeControls) {
                NSString *title = [NSString stringWithFormat:
                    @"CC%d: M:%d → C:%d %@",
                    cc, info.modulaser_val, info.controller_val,
                    info.is_latched ? @"✓" : @"⏳"];
                [statusMenu addItemWithTitle:title
                                      action:nil
                               keyEquivalent:@""];
            }
        }

        [statusMenu addItem:[NSMenuItem separatorItem]];
        [statusMenu addItemWithTitle:@"Quit"
                              action:@selector(terminate:)
                       keyEquivalent:@"q"];
    });
}

void runStatusBarEventLoop() {
    [NSApp run];  // Blocks until app terminates
}

void shutdownStatusBar() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSStatusBar systemStatusBar] removeStatusItem:statusItem];
        statusItem = nil;
    });
}
```

### Phase 2: Integration with Main Application

**Modifications to `midi-easing-proxy.cpp`**:

1. **Include header**:
```cpp
#ifdef __APPLE__
#include "status_bar.h"
#endif
```

2. **Add command-line flag**:
```cpp
bool enable_menubar = false;

// In main():
if (arg == "-m" || arg == "--menubar") {
    enable_menubar = true;
}
```

3. **Initialize at startup** (in `main()` after config loading):
```cpp
#ifdef __APPLE__
if (enable_menubar) {
    initStatusBar();

    // Launch Cocoa event loop in separate thread
    std::thread cocoa_thread(runStatusBarEventLoop);
    cocoa_thread.detach();
}
#endif
```

4. **Update from existing log points**:

In `logWaiting()`:
```cpp
void logWaiting(uint8_t control, uint8_t modulaser_val, uint8_t controller_val) {
    std::ostringstream msg;
    msg << "waiting for crossover (Modulaser: " << (int)modulaser_val
        << ", Controller: " << (int)controller_val << ")";
    log("WAITING", control, msg.str());

#ifdef __APPLE__
    if (enable_menubar) {
        updateControlState(control, modulaser_val, controller_val, false);
    }
#endif
}
```

In latch detection (when control becomes latched):
```cpp
#ifdef __APPLE__
if (enable_menubar) {
    updateControlState(control, state.last_modulaser_value,
                       state.last_controller_value, true);
}
#endif
```

5. **Clear controls when converged**:
```cpp
// When smoothing completes or control becomes idle
#ifdef __APPLE__
if (enable_menubar) {
    updateStatusText("MIDI: Ready");
}
#endif
```

### Phase 3: Build System Integration

**Modifications to `Makefile`**:

```makefile
# Add AppKit framework for macOS
ifeq ($(shell uname), Darwin)
    FRAMEWORKS = -framework CoreMIDI -framework CoreFoundation -framework CoreAudio -framework AppKit
else
    FRAMEWORKS = -framework CoreMIDI -framework CoreFoundation -framework CoreAudio
endif

CXXFLAGS = -std=c++17 -Wall -Wextra -I./vendor $(FRAMEWORKS)

# Add status_bar.o to dependencies
OBJECTS = led_controller.o

ifeq ($(shell uname), Darwin)
    OBJECTS += status_bar.o
endif

midi-easing-proxy: midi-easing-proxy.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Objective-C++ compilation rule
status_bar.o: status_bar.mm status_bar.h
	$(CXX) $(CXXFLAGS) -c status_bar.mm -o $@

# Update clean target
clean:
	rm -f midi-easing-proxy test-midi-proxy test-leds *.o
```

### Phase 4: Advanced Features (Optional)

**4.1 Custom Icon**

Replace text with icon:
```objective-c
NSImage *icon = [NSImage imageNamed:@"StatusIcon"];
[icon setTemplate:YES];  // Makes it adapt to menu bar theme
statusItem.button.image = icon;
```

**4.2 Click Actions**

Add click handlers:
```objective-c
statusItem.button.action = @selector(statusItemClicked:);
statusItem.button.target = self;
```

**4.3 Color-Coded States**

Use attributed strings for colored text:
```objective-c
NSDictionary *attrs = @{
    NSForegroundColorAttributeName: [NSColor redColor]
};
NSAttributedString *attrStr = [[NSAttributedString alloc]
                                initWithString:@"⏳"
                                    attributes:attrs];
```

**4.4 Notifications**

Add user notifications for important state changes:
```objective-c
NSUserNotification *notif = [[NSUserNotification alloc] init];
notif.title = @"MIDI Control Latched";
notif.informativeText = @"CC19 waiting for crossover";
[[NSUserNotificationCenter defaultUserNotificationCenter]
    deliverNotification:notif];
```

## Testing Strategy

### Unit Testing
- Create `test-menubar.mm` for isolated testing
- Test status text updates from multiple threads
- Verify menu rebuilding with varying control counts
- Test shutdown cleanup

### Integration Testing
1. Run with `-m` flag
2. Verify menu bar item appears
3. Touch controls on MidiMix, verify updates appear
4. Click menu bar item, verify dropdown shows current state
5. Change presets in Modulaser, verify waiting states appear
6. Verify no crashes when toggling controls rapidly

### Performance Testing
- Monitor CPU usage (Cocoa thread should be <1% when idle)
- Test with 10+ simultaneous control changes
- Verify no blocking of MIDI threads
- Check memory leaks with Instruments

## Known Limitations

1. **macOS Only**: Uses AppKit framework
2. **Menu Bar Space**: Limited to ~20 characters before truncation
3. **Thread Complexity**: Adds another thread to existing model
4. **Display Sleep**: Menu bar updates may pause during display sleep

## Future Enhancements

- Persistent preferences (remember menubar enable state)
- Configurable update throttling (reduce UI updates for CPU savings)
- History log in menu (last 10 state changes)
- Export state to JSON for debugging
- Integration with macOS notification center
- Accessibility features (VoiceOver support)

## References

- [NSStatusBar Documentation](https://developer.apple.com/documentation/appkit/nsstatusbar)
- [NSStatusItem Documentation](https://developer.apple.com/documentation/appkit/nsstatusitem)
- [Making Simple Menu Bar Apps for OS X](https://kmikael.com/2013/07/01/simple-menu-bar-apps-for-os-x/)
- [Grand Central Dispatch (GCD) Guide](https://developer.apple.com/documentation/dispatch)

## Estimated Effort

- **Phase 1**: 2-3 hours (basic wrapper implementation)
- **Phase 2**: 1-2 hours (integration points)
- **Phase 3**: 30 minutes (build system)
- **Phase 4**: 2-4 hours (optional enhancements)
- **Testing**: 1-2 hours

**Total**: 6-11 hours for complete implementation with testing
