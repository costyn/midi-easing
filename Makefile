# Makefile for MIDI Easing Proxy
# macOS version using CoreMIDI

CXX = clang++
CXXFLAGS = -std=c++17 -Wall -O2 -I. -Ivendor/rtmidi-6.0.0 -Ivendor/AHEasing
LDFLAGS = -framework CoreMIDI -framework CoreAudio -framework CoreFoundation
TARGET = midi-easing-proxy
RTMIDI_SRC = vendor/rtmidi-6.0.0/RtMidi.cpp
EASING_SRC = vendor/AHEasing/easing.c
SOURCES = midi-easing-proxy.cpp $(RTMIDI_SRC) $(EASING_SRC)
OBJECTS = midi-easing-proxy.o vendor/rtmidi-6.0.0/RtMidi.o vendor/AHEasing/easing.o

# Define __MACOSX_CORE__ for CoreMIDI support
CXXFLAGS += -D__MACOSX_CORE__

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)
	@echo ""
	@echo "Build complete! Run with: ./$(TARGET)"
	@echo "Quiet mode: ./$(TARGET) -q"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete"

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	@echo "Installed to /usr/local/bin/$(TARGET)"

uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)
	@echo "Uninstalled from /usr/local/bin"
