# MIDI Easing Controller

This project is a Python script for smoothing MIDI control change messages using an easing function. It listens for MIDI input, applies easing to the control change values, and sends the smoothed values back out.

## Features

- Listens to MIDI input messages.
- Applies a smoothing function to control change messages.
- Sends the smoothed messages to a MIDI output.

## Requirements

- Python 3.x
- `mido` library for MIDI handling
- `python-rtmidi` library for MIDI comms
- `easing-functions` library for easing 

## Installation

1. Clone the repository:

```shell
git clone https://github.com/costyn/midi-easing.git
cd midi-easing
```

2.	Make a Python virtual environment and install the required packages:
```shell
python3 -m venv venv
source venv/bin/activate
pip3 install -r requirements.txt
```

## Usage

1.	Ensure your MIDI devices are properly connected and recognized by your system.
2.	Run the script:

```shell
python3 midi-easing.py
```

The script will start listening for MIDI input, apply smoothing to the control change messages, and send the processed messages.

Press `ctrl-c` to quit.

## Configuration

You can adjust parameters in the script such as:

* Easing duration: The time over which the value should be smoothed.
* MIDI input and output ports: Change these in the script to match your setup.
  * Run the `portid.py` script to see available ports.

## Troubleshooting

* No MIDI devices detected: Make sure your MIDI devices are connected and properly configured in your system.
* Errors with mido: Ensure you have the correct version of the mido library installed. You can adjust the version in the requirements.txt if needed.

## Roadmap
* Commandline options to pass midi in/out
* If no options, then a choose menu
* Console "GUI" ?