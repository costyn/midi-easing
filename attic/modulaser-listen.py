import mido

def main():
    input_port_name = "IAC Driver Bus 2"
    
    try:
        print("Trying to open ports...")
        with mido.open_input(input_port_name) as inport:
            print(f"Connected to '{input_port_name}'. Listening for MIDI input...")

            try:
                while True:
                    for msg in inport:
                        print(f"Received: {msg}")

            except KeyboardInterrupt:
                print("Interrupted by user. Exiting...")
                        
    except IOError as e:
        print(f"Error opening a MIDI port in configured in config.ini: {e}. Available ports:")
        print("\nInput ports:")
        for port in mido.get_input_names():
            print(f"\t{port}")
        print("\nOutput Ports:")
        for port in mido.get_output_names():
            print(f"\t{port}")
    except Exception as e:
        print(f"Unexpected error: {e}")

if __name__ == '__main__':
    main()