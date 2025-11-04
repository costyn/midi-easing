import mido

print("Input Ports:")
for port in mido.get_input_names():
    print(port)

print("\nOutput Ports:")
for port in mido.get_output_names():
    print(port)
