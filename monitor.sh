#!/bin/bash

# Navigate to project directory
cd /home/maf/Pulpit/T-Energy-S3

# Set up serial port and monitor output
# Using stty + cat as a workaround for PlatformIO monitor terminal issues
stty -F /dev/ttyACM* 115200 raw -echo
echo "Monitoring ESP32-S3 serial output (Ctrl+C to exit)..."
cat /dev/ttyACM*
