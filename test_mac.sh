#!/bin/bash
cd build && make -j4 && ./wuxing > ../log.txt 2>&1 &
PID=$!
sleep 2
# Use AppleScript to simulate a mouse click in the center of the Wuxing window if possible, or we just trust the C code
