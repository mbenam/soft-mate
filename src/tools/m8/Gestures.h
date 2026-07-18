#pragma once

// ===========================================================================
// Gestures.h — pinned edit gestures for M8 device control.
//
// Tier 2 of M8_DEVICE_CONTROL_SPEC.md. Loaded from hw_buttons.json.
// Until Tier 2 hardware pinning is completed, the table is empty and
// editValue/enterNote return explicit "gestures not pinned" errors.
// ===========================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace m8 {
namespace dev {

// A single pinned gesture: a key combination that produces a known effect.
struct Gesture {
    const char* name;       // e.g. "value_inc", "value_dec", "note_enter"
    const char* description;
    uint8_t mask;           // the key mask to press (OR of Key::* masks)
    bool needsChord;        // true = press simultaneously, false = sequential
};

// The gesture table. Populated at runtime from hw_buttons.json.
// Empty until Tier 2 pinning is done.
struct GestureTable {
    bool populated = false;     // true if all gestures are pinned and confirmed
    int pinnedFwMajor = 0;      // firmware version these were pinned on
    int pinnedFwMinor = 0;
    int pinnedFwPatch = 0;

    // Value editing
    uint8_t valueInc = 0;       // e.g. EDIT|UP
    uint8_t valueDec = 0;       // e.g. EDIT|DOWN
    uint8_t valueInc16 = 0;     // e.g. EDIT|RIGHT (high nibble +1)
    uint8_t valueDec16 = 0;     // e.g. EDIT|LEFT (high nibble -1)

    // Enum stepping
    uint8_t enumNext = 0;       // same as valueInc for enums
    uint8_t enumPrev = 0;       // same as valueDec for enums

    // Toggle
    uint8_t toggle = 0;         // e.g. single EDIT press

    // Note entry
    uint8_t noteEnter = 0;      // the gesture to enter a note
    uint8_t noteInc = 0;        // nudge note +1 semitone
    uint8_t noteDec = 0;        // nudge note -1 semitone
    uint8_t noteOctInc = 0;     // nudge note +12 semitones (octave up)
    uint8_t noteOctDec = 0;     // nudge note -12 semitones (octave down)

    // Cell operations
    uint8_t insertDefault = 0;  // insert default value / fill cell
    uint8_t clearCell = 0;      // clear cell

    // Check if the table is usable.
    bool isReady() const { return populated; }

    // Load from hw_buttons.json. Returns false on parse error.
    bool loadFromFile(const std::string& path);

    // Save to hw_buttons.json.
    bool saveToFile(const std::string& path) const;
};

// Global gesture table instance.
GestureTable& getGestures();

} // namespace dev
} // namespace m8
