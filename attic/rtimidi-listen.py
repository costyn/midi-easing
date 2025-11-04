import rtmidi
import time

def fromMidiMixCallback(message, time_stamp):
    print(f"Received MIDI Mix message: {message} at time: {time_stamp}")
    
    # Forward the message to the output port (extract just the MIDI bytes)
    midi_bytes = message[0] if isinstance(message, tuple) else message
    toModulaser.send_message(midi_bytes)

def fromModulaserCallback(message, time_stamp):
    # Print the received MIDI message
    print(f"Received Modulaser message: {message} at time: {time_stamp}")
    
def main():
    global toModulaser

    # Create MIDI input and output instances
    fromMidiMix = rtmidi.MidiIn()
    fromModulaser = rtmidi.MidiIn()
    toModulaser = rtmidi.MidiOut()

    # Open a virtual MIDI output port
    toModulaser.open_virtual_port("My Virtual MIDI Device")
    fromModulaser.open_virtual_port("My Virtual MIDI Device")

    # Open the existing MIDI input port "MIDI Mix"
    available_ports = fromMidiMix.get_ports()
    if "MIDI Mix" in available_ports:
        print("Found MidiMix")
        fromMidiMix.open_port(available_ports.index("MIDI Mix"))
    else:
        print("Error: 'MIDI Mix' port not found.")
        return

    # Set the callback function to print incoming messages and forward them
    fromMidiMix.set_callback(fromMidiMixCallback)
    fromModulaser.set_callback(fromModulaserCallback)

    print("MIDI device 'My Virtual MIDI Device' is ready. Listening for messages from 'MIDI Mix'... Press Ctrl+C to exit.")

    try:
        # Keep the script running to listen for incoming messages
        while True:
            time.sleep(1)  # Sleep to prevent high CPU usage
    except KeyboardInterrupt:
        print("Exiting...")

    finally:
        fromMidiMix.close_port()
        fromModulaser.close_port()
        toModulaser.close_port()
        del fromMidiMix
        del fromModulaser
        del toModulaser

if __name__ == "__main__":
    main()