import mido
from easing_functions import SineEaseInOut
import time
import threading

# Create an easing function instance
easing_function = SineEaseInOut()

# Define smoothing parameters
easing_duration = 2000  # Duration in milliseconds

# Shared state for easing
state = {
    'start_time': None,
    'start_value': None,
    'target_value': None,
    'running': False,
    'outport': None,
    'control': None
}

def apply_easing(start_value, end_value, t):
    eased_fraction = easing_function(t)
    return start_value + eased_fraction * (end_value - start_value)

def easing_thread():
    while True:
        time.sleep(0.01)  # Sleep for a short period to avoid busy-waiting
        if state['running']:
            current_time = time.time() * 1000  # Current time in milliseconds
            elapsed_time = current_time - state['start_time']
            if elapsed_time >= easing_duration:
                # Easing duration completed
                smoothed_value = state['target_value']
                state['running'] = False  # Stop easing
                print(f"Easing completed. Final value: {smoothed_value}")
            else:
                t = elapsed_time / easing_duration
                smoothed_value = apply_easing(state['start_value'], state['target_value'], t)
                # print(f"Easing in progress: smoothed_value={smoothed_value} at time: {current_time}")

            if int(smoothed_value) != int(state.get('last_sent_value', -1)):
                midi_message = mido.Message('control_change', control=state['control'], value=int(smoothed_value))
                state['outport'].send(midi_message)
                state['last_sent_value'] = smoothed_value
                print(f"Sent (during easing): {midi_message}")

def process_message(message):
    global state
    if message.type == 'control_change':
        current_time = time.time() * 1000  # Current time in milliseconds
        target_value = message.value

        if state['running']:
            # Update target value and reset timing
            state['target_value'] = target_value
            # state['start_time'] = current_time
            # print(f"Updated target value: {target_value} at time: {current_time}")
        else:
            # Start a new easing
            state['start_time'] = current_time
            state['start_value'] = target_value
            state['target_value'] = target_value
            state['running'] = True
            state['control'] = message.control
            print(f"Starting easing: start_time={state['start_time']}, start_value={state['start_value']}")
    else:
        # Print non-control change messages
        print(f"Incoming non-control change: {message}")
        return message

def main():
    input_port_name = 'MIDI Mix'
    output_port_name = 'IAC Driver Virtual Midi Port'

    print("Opening ports...")
    with mido.open_input(input_port_name) as inport:
        with mido.open_output(output_port_name) as outport:
            print("Listening for MIDI input...")

            # Initialize state with outport
            state['outport'] = outport

            # Start the easing thread
            threading.Thread(target=easing_thread, daemon=True).start()

            try:
                while True:
                    for msg in inport:
                        processed_msg = process_message(msg)
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
                
if __name__ == '__main__':
    main()