#!/bin/bash
#
# Setup script to download third-party dependencies
# Run this after cloning the repository: ./setup_deps.sh
#

set -e  # Exit on error

echo "Setting up MIDI Easing Proxy dependencies..."
echo ""

# Create vendor directory
mkdir -p vendor

# Download RtMidi
echo "Downloading RtMidi 6.0.0..."
curl -L -o vendor/rtmidi.tar.gz https://github.com/thestk/rtmidi/archive/refs/tags/6.0.0.tar.gz
cd vendor
tar -xzf rtmidi.tar.gz
rm rtmidi.tar.gz
cd ..
echo "✓ RtMidi installed"

# Download AHEasing
echo "Downloading AHEasing..."
mkdir -p vendor/AHEasing
curl -L -o vendor/AHEasing/easing.h https://raw.githubusercontent.com/warrenm/AHEasing/master/AHEasing/easing.h
curl -L -o vendor/AHEasing/easing.c https://raw.githubusercontent.com/warrenm/AHEasing/master/AHEasing/easing.c
echo "✓ AHEasing installed"

echo ""
echo "Dependencies installed successfully!"
echo "You can now build the project with: make"
