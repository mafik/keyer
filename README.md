# 𝖒𝖆𝖋's EyeTerm 📟☣︎

A wearable SSH Terminal. Beams a remote shell directly into your retinas.

Runs an a bespoke hand-held keyboard (https://github.com/mafik/keyer/). Results are displayed on a laser projector embedded in smart glasses.

## Misc commands

```shell
# Tweak FreeRTOS configuration
$ pio run --target menuconfig

# Clean build files
$ pio run --target clean
```

## Repository structure

- `layout_generator/` - a set of Python scripts for generating an optimized chord layout
  - `corpus/` - directory for text files that will be used for evaluating the layout
  - `planner.py` - main entry point for doing the optimization
  - `qwerty_analysis.py` - converts the text files into a sequence of equivalent IBM PC keyboard keys
  - `keyer_simulator.cpp` - simulates text entry on the keyer
  - `beam_optimizer.py` - optional utility to double-check whether the generated layout is (locally) optimal
- `src/` - code that runs on the ESP32
- `sdkconfig.EyeTerm` - configuration for the ESP-IDF firmware
- `layout_tutor/` - the home of "Keyer Flight School" - a webapp for learning to type with chords
