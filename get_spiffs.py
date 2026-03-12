#!/usr/bin/env python3
"""Download SPIFFS partition from ESP32 and unpack into data/ directory."""
import os
import shutil
import subprocess
import sys
import tempfile

# SPIFFS partition parameters (from default_16MB.csv)
SPIFFS_OFFSET = 0xC90000
SPIFFS_SIZE = 0x360000
PAGE_SIZE = 256
BLOCK_SIZE = 4096

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(PROJECT_DIR, "data")
MKSPIFFS = os.path.expanduser("~/.platformio/packages/tool-mkspiffs/mkspiffs_espressif32_espidf")

if not os.path.exists(MKSPIFFS):
    sys.exit(f"mkspiffs not found at {MKSPIFFS}")

with tempfile.TemporaryDirectory() as tmpdir:
    bin_path = os.path.join(tmpdir, "spiffs.bin")
    unpack_dir = os.path.join(tmpdir, "unpacked")
    os.makedirs(unpack_dir)

    print(f"Reading SPIFFS partition from {PORT}...")
    subprocess.run([
        sys.executable, "-m", "esptool",
        "--chip", "esp32s3", "--port", PORT, "--baud", "921600",
        "read_flash", hex(SPIFFS_OFFSET), hex(SPIFFS_SIZE), bin_path,
    ], check=True)

    print("Unpacking SPIFFS image...")
    ret = subprocess.run([
        MKSPIFFS, "-u", unpack_dir,
        "-b", str(BLOCK_SIZE), "-p", str(PAGE_SIZE), "-s", str(SPIFFS_SIZE),
        bin_path,
    ], capture_output=True, text=True)

    if ret.returncode != 0:
        print("Could not unpack SPIFFS (may be empty or unformatted)")
        print(ret.stderr)
        sys.exit(1)

    # Merge unpacked files into data/, preserving existing files
    for name in os.listdir(unpack_dir):
        src = os.path.join(unpack_dir, name)
        dst = os.path.join(DATA_DIR, name)
        os.makedirs(DATA_DIR, exist_ok=True)
        shutil.copy2(src, dst)
        print(f"  {name}")

print(f"Done. Files are in {DATA_DIR}/")
