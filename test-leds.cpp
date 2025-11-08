// Test program to discover and demonstrate LED control on Akai MIDIMix
// Based on unofficial MIDIMix communications protocol

#include <iostream>
#include <chrono>
#include <thread>
#include "RtMidi.h"

void sleep_ms(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void testLED(RtMidiOut* midiout, int note_number, const char* button_name) {
    std::vector<unsigned char> message(3);

    // Turn LED ON
    message[0] = 0x90;  // Note On, Channel 0
    message[1] = note_number;
    message[2] = 0x7F;  // Full velocity (on)

    std::cout << "Testing " << button_name << " (Note " << note_number << ") - ON" << std::endl;
    midiout->sendMessage(&message);
    sleep_ms(500);

    // Turn LED OFF
    message[2] = 0x00;  // Velocity 0 (off)
    midiout->sendMessage(&message);
    sleep_ms(200);
}

void testAllLEDs(RtMidiOut* midiout) {
    std::cout << "\n=== Testing MUTE Button LEDs ===" << std::endl;
    const int mute_notes[] = {1, 4, 7, 10, 13, 16, 19, 22};
    for (int i = 0; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof(name), "MUTE %d", i+1);
        testLED(midiout, mute_notes[i], name);
    }

    std::cout << "\n=== Testing SOLO (MUTE+SOLO) LEDs ===" << std::endl;
    const int solo_notes[] = {2, 5, 8, 11, 14, 17, 20, 23};
    for (int i = 0; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof(name), "SOLO %d", i+1);
        testLED(midiout, solo_notes[i], name);
    }

    std::cout << "\n=== Testing REC ARM Button LEDs ===" << std::endl;
    const int rec_notes[] = {3, 6, 9, 12, 15, 18, 21, 24};
    for (int i = 0; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof(name), "REC ARM %d", i+1);
        testLED(midiout, rec_notes[i], name);
    }

    std::cout << "\n=== Testing Special Button LEDs ===" << std::endl;
    testLED(midiout, 25, "BANK LEFT");
    testLED(midiout, 26, "BANK RIGHT");
    testLED(midiout, 27, "SOLO (probably no LED)");
}

void scanAllNotes(RtMidiOut* midiout) {
    std::cout << "\n=== Scanning all note numbers 1-127 ===" << std::endl;
    std::cout << "Watch for any LEDs that light up..." << std::endl;

    std::vector<unsigned char> message(3);
    message[0] = 0x90;  // Note On, Channel 0

    for (int note = 1; note <= 127; note++) {
        // Turn ON
        message[1] = note;
        message[2] = 0x7F;
        midiout->sendMessage(&message);

        if (note % 10 == 0) {
            std::cout << "Note " << note << "..." << std::endl;
        }

        sleep_ms(50);

        // Turn OFF
        message[2] = 0x00;
        midiout->sendMessage(&message);
        sleep_ms(50);
    }
    std::cout << "Scan complete!" << std::endl;
}

void lightShow(RtMidiOut* midiout) {
    std::cout << "\n=== Running LED Light Show ===" << std::endl;

    std::vector<unsigned char> on_msg(3);
    std::vector<unsigned char> off_msg(3);
    on_msg[0] = 0x90;
    off_msg[0] = 0x90;
    on_msg[2] = 0x7F;
    off_msg[2] = 0x00;

    // All LEDs (MUTE + REC ARM + BANK buttons)
    const int all_leds[] = {1, 4, 7, 10, 13, 16, 19, 22,  // MUTE
                            3, 6, 9, 12, 15, 18, 21, 24,  // REC ARM
                            25, 26};                       // BANK buttons

    // Chase pattern
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 18; i++) {
            on_msg[1] = all_leds[i];
            midiout->sendMessage(&on_msg);
            sleep_ms(80);
            off_msg[1] = all_leds[i];
            midiout->sendMessage(&off_msg);
        }
    }

    // All on, then all off
    for (int i = 0; i < 18; i++) {
        on_msg[1] = all_leds[i];
        midiout->sendMessage(&on_msg);
    }
    sleep_ms(500);
    for (int i = 0; i < 18; i++) {
        off_msg[1] = all_leds[i];
        midiout->sendMessage(&off_msg);
    }

    std::cout << "Light show complete!" << std::endl;
}

int main() {
    RtMidiOut* midiout = nullptr;

    try {
        midiout = new RtMidiOut();

        // List available ports
        unsigned int nPorts = midiout->getPortCount();
        std::cout << "\nAvailable MIDI output ports:" << std::endl;
        for (unsigned int i = 0; i < nPorts; i++) {
            std::cout << "  Port " << i << ": " << midiout->getPortName(i) << std::endl;
        }

        if (nPorts == 0) {
            std::cout << "No MIDI output ports available!" << std::endl;
            delete midiout;
            return 1;
        }

        // Find MIDIMix or use first port
        int port = -1;
        for (unsigned int i = 0; i < nPorts; i++) {
            std::string name = midiout->getPortName(i);
            if (name.find("MIDI Mix") != std::string::npos ||
                name.find("MIDIMix") != std::string::npos) {
                port = i;
                break;
            }
        }

        if (port == -1) {
            std::cout << "\nMIDIMix not found. Enter port number to use: ";
            std::cin >> port;
        }

        if (port < 0 || port >= (int)nPorts) {
            std::cout << "Invalid port number!" << std::endl;
            delete midiout;
            return 1;
        }

        midiout->openPort(port);
        std::cout << "\nConnected to: " << midiout->getPortName(port) << std::endl;

        // Menu
        while (true) {
            std::cout << "\n=== MIDIMix LED Test Menu ===" << std::endl;
            std::cout << "1. Test all known LED buttons (documented)" << std::endl;
            std::cout << "2. Scan all possible note numbers (1-127)" << std::endl;
            std::cout << "3. Run light show demo" << std::endl;
            std::cout << "4. Test single note number" << std::endl;
            std::cout << "0. Exit" << std::endl;
            std::cout << "Choice: ";

            int choice;
            std::cin >> choice;

            switch (choice) {
                case 1:
                    testAllLEDs(midiout);
                    break;
                case 2:
                    scanAllNotes(midiout);
                    break;
                case 3:
                    lightShow(midiout);
                    break;
                case 4: {
                    std::cout << "Enter note number (1-127): ";
                    int note;
                    std::cin >> note;
                    if (note >= 1 && note <= 127) {
                        testLED(midiout, note, "Custom note");
                    } else {
                        std::cout << "Invalid note number!" << std::endl;
                    }
                    break;
                }
                case 0: {
                    std::cout << "Exiting..." << std::endl;

                    // Turn off all LEDs before exit
                    std::vector<unsigned char> off_msg(3);
                    off_msg[0] = 0x90;
                    off_msg[2] = 0x00;
                    for (int i = 1; i <= 27; i++) {
                        off_msg[1] = i;
                        midiout->sendMessage(&off_msg);
                    }

                    delete midiout;
                    return 0;
                }
                default:
                    std::cout << "Invalid choice!" << std::endl;
            }
        }

    } catch (RtMidiError &error) {
        error.printMessage();
        if (midiout) delete midiout;
        return 1;
    }

    return 0;
}
