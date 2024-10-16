import rtmidi
import time

def midi_callback(message, time_stamp):
    # Print the received MIDI message
    print(f"Received MIDI message: {message} at time: {time_stamp}")

def main():
    # Create a MIDI input instance
    midiin = rtmidi.MidiIn()

    # Open a virtual MIDI port
    midiin.open_virtual_port("My Virtual MIDI Device")

    # Set the callback function to print incoming messages
    midiin.set_callback(midi_callback)

    print("MIDI device 'My Virtual MIDI Device' is ready. Listening for messages... Press Ctrl+C to exit.")

    try:
        # Keep the script running to listen for incoming messages
        while True:
            time.sleep(1)  # Sleep to prevent high CPU usage
    except KeyboardInterrupt:
        print("Exiting...")

    finally:
        midiin.close_port()
        del midiin

if __name__ == "__main__":
    main()