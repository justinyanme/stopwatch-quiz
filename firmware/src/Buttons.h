// firmware/src/Buttons.h
#pragma once
#include <cstdint>

namespace stopwatch {

enum class ButtonEvent : uint8_t {
    None,
    KeyAShort, KeyALong,
    KeyBShort, KeyBLong,
};

/// Polls M5 button state. Call once per loop tick; debounces at ~20ms.
/// Long press threshold: 800ms held.
ButtonEvent pollButtons();

}  // namespace stopwatch
