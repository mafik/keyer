#pragma once

#include <esp_timer.h>
#include <functional>

#include "common_esp32.hpp"

namespace atmt {

// Set this to something like 350 to enable chord autostart when a chord is held
// down for this duration. Chords started this way cause the keys to be pressed
// and they will be released only when the chord is also released. This allows
// chords to function more like keyboard keys.
//
// This is disabled by default because it makes learning very hard. Re-enable
// this once your WPM is above 20.
constexpr unsigned long kChordAutostartMillis = 350 * 1000 * 1000;

// The two arpeggio keys must be spread apart by at least this many
// milliseconds.
constexpr unsigned long kArpeggioMinSpacingMillis = 80;

// Arpeggios must be released quickly after the last button is pressed. This
// constant conrols how long the last button can be held down for an action to
// be registered as an arpeggio.
constexpr unsigned long kArpeggioMaxHoldMillis = 240;

// 0 = not pressing, 1 = pressing first button, 2 = pressing second button, etc.
using FingerPosition = uint8_t;

void InitKeyer();

void RegisterChord(FingerPosition thumb, FingerPosition index,
                   FingerPosition middle, FingerPosition ring,
                   FingerPosition little, std::function<void()> fn);

} // namespace atmt
