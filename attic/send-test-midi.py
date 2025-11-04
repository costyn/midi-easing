#!/usr/bin/env python3
"""
Send test MIDI CC messages to Bus 2 for mapping in Modulaser

Usage:
1. Run this script
2. In Modulaser, enter "MIDI Learn" mode
3. Click a parameter knob in Modulaser
4. Press a number key (0-9) in this terminal
5. The parameter will be mapped to that CC number on Bus 2
"""

import mido
import sys

def main():
    port_name = "IAC Driver Bus 2"

    try:
        with mido.open_output(port_name) as outport:
            print(f"Connected to {port_name}")
            print("Ready to send MIDI CC messages for mapping.")
            print()
            print("Press 0-9 to send CC on channel 0-9")
            print("Each press sends CC with value 64")
            print("Press 'q' to quit")
            print("-" * 50)

            cc_number = 16  # Start at CC 16

            while True:
                key = input(f"Press key to send CC{cc_number} (or 'q' to quit): ").strip()

                if key.lower() == 'q':
                    break

                msg = mido.Message('control_change', control=cc_number, value=64)
                outport.send(msg)
                print(f"✓ Sent {msg}")
                cc_number += 1

    except IOError as e:
        print(f"Error: {e}")
        print("\nAvailable output ports:")
        for port in mido.get_output_names():
            print(f"  {port}")
    except KeyboardInterrupt:
        print("\nExiting...")


if __name__ == '__main__':
    main()
