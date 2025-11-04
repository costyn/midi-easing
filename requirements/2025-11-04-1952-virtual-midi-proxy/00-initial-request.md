# Initial Request

**Date:** 2025-11-04 19:52
**Source:** midi-proxy-requirements.txt

## User's Original Request

In this requirements session we are going to be modifying a MIDI proxy that sits between laser software Modulaser and an Akai MidiMix controller.

The midi-easing.py listens to incoming messages and applies easing to incoming parameters so the movement is less jerky. See README too.

## The Problem

The problem I'm having is that, in the laser control software Modulaser, the midi knobs map to parameter controls. And whenever you switch to a new preset, the knob positions no longer correspond to the values in the presets, so whenever you then start adjusting a parameter, the animation jumps as it immediately uses the new midi input value.

## Required Changes

1. **Stop sending MIDI messages to IAC Bus** (see config.ini)

2. **Create a new virtual MIDI port** called "Midi Easing" using rtmidi:
   ```python
   import rtmidi

   # Create virtual output (appears as input to other apps)
   midiout = rtmidi.MidiOut()
   midiout.open_virtual_port("Midi Easing")

   # Create virtual input (appears as output to other apps)
   midiin = rtmidi.MidiIn()
   midiin.open_virtual_port("Midi Easing")

   # Process/route between them as needed
   ```
   Note: User doesn't know if mido supports this, otherwise we will need to refactor to use the rtmidi library, or use a different language/framework altogether (Swift? Objective C?). Please advise.

3. **Modulaser will send/receive MIDI messages through the "Midi Easing" virtual midi port**

4. **Track incoming MIDI messages and update internal state**

5. **Compare new MIDI control messages from MidiMix controller to stored state**, and apply easing to move towards the new value from the controller

6. **Goal: Moving a control on the MidiMix will not make the animation instantly jerk to the new parameter value**

7. **Code quality improvements** - The midi-easing.py script was created by an earlier version of Claude Code and there's parts that are not great, code quality wise

8. **Respect config.ini** - Where certain channels aren't eased (whitelist_controls)

## Current Status

- User made a start with rtimidi in `rtimidi-listen.py`. It looks like all the code is there, but messages don't appear to be arriving at Modulaser
- User sees output like:
  ```
  Received MIDI Mix message: ([176, 27, 61], 0.008192416000000001) at time: None
  Received MIDI Mix message: ([176, 27, 63], 0.016438666) at time: None
  ```
- But nothing happens in Modulaser when they do 'learn' and move a control

## Technology Preferences

User is not tied to Python; they wanted something quick & dirty to try this feature out. User is familiar with:
- C, C++
- Kotlin, Java
- TypeScript

## Current Implementation

- **midi-easing.py** - Main script using mido, sends to IAC Bus
- **rtimidi-listen.py** - New experiment with rtmidi, creates virtual port
- **config.ini** - Configuration for ports and whitelist controls
- Uses easing-functions library for SineEaseInOut
- Threading for easing calculations
- Configurable easing durations per control channel
