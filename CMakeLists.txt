# Minimal CMakeLists.txt for MCP C++ tools integration
# Note: This is NOT used for building - use Makefile instead
# This file exists only to make the project discoverable by MCP C++ server

cmake_minimum_required(VERSION 3.10)
project(midi-easing-proxy)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define sources
set(SOURCES
    midi-easing-proxy.cpp
    led_controller.cpp
    vendor/rtmidi-6.0.0/RtMidi.cpp
    vendor/AHEasing/easing.c
)

# Include directories (from Makefile)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/rtmidi-6.0.0
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/AHEasing
)

# Define macOS CoreMIDI
add_definitions(-D__MACOSX_CORE__)

# Main executable
add_executable(midi-easing-proxy ${SOURCES})

# Link frameworks (macOS)
if(APPLE)
    target_link_libraries(midi-easing-proxy
        "-framework CoreMIDI"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
endif()

# Test executable
add_executable(test-midi-proxy test-midi-proxy.cpp)

# LED test executable
add_executable(test-leds
    test-leds.cpp
    vendor/rtmidi-6.0.0/RtMidi.cpp
)

if(APPLE)
    target_link_libraries(test-leds
        "-framework CoreMIDI"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
endif()
