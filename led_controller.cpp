/*
 * LED Controller for Akai MIDIMix - Implementation
 *
 * Copyright (c) 2025
 */

#include "led_controller.h"
#include <thread>
#include <chrono>
#include <future>
#include <iostream>

LEDController::LEDController()
    : midiout(nullptr)
    , running(true)
    , current_lit_note(0)
{
}

LEDController::~LEDController() {
    running = false;
    allOff();
}

void LEDController::setMidiOut(RtMidiOut* out) {
    std::lock_guard<std::mutex> lock(mutex);
    midiout = out;
}

void LEDController::sendLEDMessage(uint8_t note, uint8_t velocity) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!midiout) return;

    try {
        std::vector<unsigned char> message(3);
        message[0] = 0x90;  // Note On, Channel 0
        message[1] = note;
        message[2] = velocity;  // 0x7F = on, 0x00 = off
        midiout->sendMessage(&message);
    } catch (RtMidiError &error) {
        // Silent failure - don't crash the proxy if LED control fails
        std::cerr << "[LED] Error: " << error.getMessage() << std::endl;
    }
}

void LEDController::setNoteLED(uint8_t note, bool on) {
    // If this is a permanent LED, don't allow manual control
    if (permanent_leds.count(note)) {
        return;
    }

    sendLEDMessage(note, on ? 0x7F : 0x00);

    if (on) {
        current_lit_note = note;
    } else if (current_lit_note == note) {
        current_lit_note = 0;
    }
}

void LEDController::allOff() {
    for (uint8_t note : all_leds) {
        sendLEDMessage(note, 0x00);
    }
    current_lit_note = 0;
}

void LEDController::setPermanentLEDs() {
    for (uint8_t note : permanent_leds) {
        sendLEDMessage(note, 0x7F);
    }
}

void LEDController::scheduleNoteOff(uint8_t note, uint32_t delay_ms) {
    // Don't schedule off for permanent LEDs
    if (permanent_leds.count(note)) {
        return;
    }

    // Launch async task to turn off LED after delay
    std::thread([this, note, delay_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

        // Only turn off if this note is still the current one
        // (prevents turning off if another button was pressed)
        if (running && current_lit_note == note) {
            setNoteLED(note, false);
        }
    }).detach();
}

void LEDController::lightShowSequence() {
    if (!midiout) return;

    std::cout << "[LED] Running light show..." << std::endl;

    // Phase 1: Chase through each LED once
    for (uint8_t note : all_leds) {
        if (!running) return;
        sendLEDMessage(note, 0x7F);  // On
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sendLEDMessage(note, 0x00);  // Off
    }

    // Phase 2: All LEDs on together
    if (!running) return;
    for (uint8_t note : all_leds) {
        sendLEDMessage(note, 0x7F);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Phase 3: Transition to steady state (only permanent LEDs on)
    if (!running) return;
    for (uint8_t note : all_leds) {
        if (!permanent_leds.count(note)) {
            sendLEDMessage(note, 0x00);
        }
    }

    std::cout << "[LED] Light show complete - steady state active" << std::endl;
}

void LEDController::runLightShow() {
    // Run in a detached thread so it doesn't block
    std::thread([this]() {
        lightShowSequence();
    }).detach();
}
