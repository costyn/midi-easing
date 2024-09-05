import mido
from easing_functions import SineEaseInOut
import time
import threading
import configparser

# Create an easing function instance
easing_function = SineEaseInOut()

# Shared state for easing
state = {
    'easing': {},  # Dictionary to hold state per control number
    'outport': None
}

def apply_easing(start_value, end_value, t):
    eased_fraction = easing_function(t)
    return start_value + eased_fraction * (end_value - start_value)

def easing_thread():
    while True:
        time.sleep(0.01)  # Sleep for a short period to avoid busy-waiting
        current_time = time.time() * 1000  # Current time in milliseconds
        for control, data in state['easing'].items():
            if data['running']:
                elapsed_time = current_time - data['start_time']
                if elapsed_time >= easing_duration:
                    # Easing duration completed
                    smoothed_value = data['target_value']
                    data['running'] = False  # Stop easing
                    print(f"Easing completed for control {control}. Final value: {smoothed_value}")
                else:
                    t = elapsed_time / easing_duration
                    smoothed_value = apply_easing(data['start_value'], data['target_value'], t)

                # Check if the value has changed before sending
                last_sent_value = data.get('last_sent_value')
                if last_sent_value is None or int(smoothed_value) != int(last_sent_value):
                    midi_message = mido.Message('control_change', control=control, value=int(smoothed_value))
                    state['outport'].send(midi_message)
                    data['last_sent_value'] = smoothed_value
                    print(f"Sent (during easing) for control {control}: {midi_message}")

def process_message(message, whitelist_controls):
    global state
    if message.type == 'control_change':
        if message.control in whitelist_controls:
            print(f"Control {message.control} is whitelisted; passthrough.")
            return message

        current_time = time.time() * 1000  # Current time in milliseconds
        control = message.control
        target_value = message.value

        if control in state['easing'] and state['easing'][control]['running']:
            # Update target value and reset timing
            state['easing'][control]['target_value'] = target_value
        else:
            # Start a new easing
            state['easing'][control] = {
                'start_time': current_time,
                'start_value': target_value,
                'target_value': target_value,
                'running': True,
                'last_sent_value': None
            }
            print(f"Starting easing for control {control}: start_time={state['easing'][control]['start_time']}, start_value={state['easing'][control]['start_value']}")
    else:
        # Print non-control change messages
        print(f"Incoming non-control change: {message}")
        return message

def main():
    config = load_config()

    input_port_name = config['input_port_name']
    output_port_name = config['output_port_name']
    global easing_duration
    easing_duration = config['easing_duration']  # Update easing duration from config

    try:
        print("Trying to open ports...")
        with mido.open_input(input_port_name) as inport:
            with mido.open_output(output_port_name) as outport:
                print(f"Connected to '{input_port_name}' and '{output_port_name}'. Listening for MIDI input...")

                # Initialize state with outport
                state['outport'] = outport

                # Start the easing thread
                threading.Thread(target=easing_thread, daemon=True).start()

                try:
                    while True:
                        for msg in inport:
                            processed_msg = process_message(msg, config['whitelist_controls'])
                            if processed_msg:
                                # Directly send the processed message if it's necessary
                                outport.send(processed_msg)
                                print(f"Sent: {processed_msg}")

                except KeyboardInterrupt:
                    print("Interrupted by user. Exiting...")
                    
                finally:
                    print("Cleaning up...")
                    # Clean up resources if needed
                    state['outport'].close()

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

def load_config(config_file='config.ini'):
    config = configparser.ConfigParser()
    config.read(config_file)

    midi_config = config['MIDI']
    settings_config = config['Settings']
    defaults_config = config['DEFAULTS']
    easing_durations = config['Easing_durations']

    custom_easing_durations = {}
    for control_channel, duration in easing_durations.items():
        try:
            custom_easing_durations[int(control_channel)] = int(duration)
        except ValueError:
            print(f"Warning: Invalid duration '{duration}' for control channel '{control_channel}'")
 
    return {
        'input_port_name': midi_config.get('input_port_name', 'MIDI Mix'),
        'output_port_name': midi_config.get('output_port_name', 'IAC Driver Virtual Midi Port'),
        'whitelist_controls': list(map(int, settings_config.get('whitelist_controls', '').split(','))),
        'default_easing_duration': defaults_config.getint('easing_duration', 2000),
        'custom_easing_durations': custom_easing_durations
    }

if __name__ == '__main__':
    main()