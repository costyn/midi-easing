/*
 * LED Controller for Akai MIDIMix
 *
 * Controls the LEDs on MIDIMix buttons using MIDI Note On messages
 * Supports light show sequence and individual LED control
 *
 * Copyright (c) 2025
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <vector>
#include <set>
#include <mutex>
#include <atomic>
#include <cstdint>
#include "vendor/rtmidi-6.0.0/RtMidi.h"

class LEDController {
public:
    LEDController();
    ~LEDController();

    // Set the MIDI output port for LED control
    void setMidiOut(RtMidiOut* out);

    // Run the light show sequence (non-blocking)
    void runLightShow();

    // Set individual LED on/off
    void setNoteLED(uint8_t note, bool on);

    // Turn all LEDs off
    void allOff();

    // Schedule a note LED to turn off after a delay
    void scheduleNoteOff(uint8_t note, uint32_t delay_ms);

    // Set permanent LEDs to their steady state (24, 25, 26 on)
    void setPermanentLEDs();

private:
    RtMidiOut* midiout;
    std::mutex mutex;
    std::atomic<bool> running;

    // All LEDs with controllable lights (in physical layout order for light show)
    const std::vector<uint8_t> all_leds = {
        1, 4, 7, 10, 13, 16, 19, 22,  // MUTE row
        3, 6, 9, 12, 15, 18, 21, 24,  // REC ARM row
        25, 26                         // BANK buttons
    };

    // Permanent LEDs that stay on when connected
    const std::set<uint8_t> permanent_leds = {24, 25, 26};

    // Currently lit note (for preset buttons)
    uint8_t current_lit_note;

    // Send LED control message
    void sendLEDMessage(uint8_t note, uint8_t velocity);

    // Internal light show implementation
    void lightShowSequence();
};

#endif // LED_CONTROLLER_H
