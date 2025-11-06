# Makefile for MIDI Easing Proxy
# macOS version using CoreMIDI

CXX = clang++
CXXFLAGS = -std=c++17 -Wall -O2 -I. -Ivendor/rtmidi-6.0.0 -Ivendor/AHEasing
LDFLAGS = -framework CoreMIDI -framework CoreAudio -framework CoreFoundation
TARGET = midi-easing-proxy
TEST_LED = test-leds
TEST_PROXY = test-midi-proxy
RTMIDI_SRC = vendor/rtmidi-6.0.0/RtMidi.cpp
EASING_SRC = vendor/AHEasing/easing.c
LED_SRC = led_controller.cpp
SOURCES = midi-easing-proxy.cpp $(LED_SRC) $(RTMIDI_SRC) $(EASING_SRC)
OBJECTS = midi-easing-proxy.o led_controller.o vendor/rtmidi-6.0.0/RtMidi.o vendor/AHEasing/easing.o
TEST_LED_OBJECTS = test-leds.o vendor/rtmidi-6.0.0/RtMidi.o
TEST_PROXY_OBJECTS = test-midi-proxy.o

# Define __MACOSX_CORE__ for CoreMIDI support
CXXFLAGS += -D__MACOSX_CORE__

.PHONY: all clean install test-leds test

all: $(TARGET)

test-leds: $(TEST_LED)

test: $(TEST_PROXY)
	./$(TEST_PROXY)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)
	@echo ""
	@echo "Build complete! Run with: ./$(TARGET)"
	@echo "Quiet mode: ./$(TARGET) -q"

$(TEST_LED): $(TEST_LED_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TEST_LED) $(TEST_LED_OBJECTS) $(LDFLAGS)
	@echo ""
	@echo "LED test build complete! Run with: ./$(TEST_LED)"

$(TEST_PROXY): $(TEST_PROXY_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TEST_PROXY) $(TEST_PROXY_OBJECTS)
	@echo ""
	@echo "Test suite build complete! Run with: make test"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TEST_LED_OBJECTS) $(TEST_PROXY_OBJECTS) $(TARGET) $(TEST_LED) $(TEST_PROXY)
	@echo "Clean complete"

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	@echo "Installed to /usr/local/bin/$(TARGET)"

uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)
	@echo "Uninstalled from /usr/local/bin"
