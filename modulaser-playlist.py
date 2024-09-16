import mido
from mido import Message
import time
import random

# Open the MIDI port for sending messages
port_name = 'IAC Driver Virtual Midi Port'
port = mido.open_output(port_name)

notes_channel0 = list(range(0, 126))
notes_channel1 = list(range(0, 65))

# Clear layer commands aren't presets.
notes_channel0.remove(25)
notes_channel0.remove(26)
notes_channel0.remove(27)

def clear_layers():
    # port.send(Message('note_on', note=25, velocity=127))
    # time.sleep(0.2)
    port.send(Message('note_off', note=25, velocity=127))  # C#1
    time.sleep(0.1)

    # port.send(Message('note_on', note=26, velocity=127))  # D-1
    # time.sleep(0.2)
    port.send(Message('note_off', note=26, velocity=127))  # D-1
    time.sleep(0.1)

    # port.send(Message('note_on', note=27, velocity=127))  # D#1
    # time.sleep(0.2)
    port.send(Message('note_off', note=27, velocity=127))  # D#1
    time.sleep(0.1)


# Function to send notes
def send_note(note, channel):
    clear_layers()
    print(f"Sending {note} on channel {channel}")
    port.send(Message('note_on', note=note, velocity=127, channel=channel))
    time.sleep(0.2)
    port.send(Message('note_off', note=note, velocity=127, channel=channel))

# Function to handle space bar press
def main():
    while True:
        # Choose a channel based on probabilities
        channel = 0 if random.random() < 2/3 else 1
        # Choose the corresponding list
        notes_list = notes_channel0 if channel == 0 else notes_channel1
        
        # Pop a random note from the selected list
        next_note = random.choice(notes_list)
        notes_list.remove(next_note)
        send_note(next_note, channel)
        notes_list.append(next_note)  # Add note back to the list
        time.sleep(5)

if __name__ == "__main__":
    main()