// firmware/src/Theme.h
#pragma once
#include <M5Unified.h>
#include "Protocol.h"

namespace stopwatch::theme {

// Hex colors converted to 24-bit ints; M5Unified uses 24-bit RGB at the API layer.
constexpr uint32_t kBackground   = 0x000000;
constexpr uint32_t kCodex        = 0x3D8AFF;   // clean blue (distinct from gemini purple)
constexpr uint32_t kCodexDim     = 0x255299;
constexpr uint32_t kClaude       = 0xFF8A3D;   // warm orange (Anthropic-leaning)
constexpr uint32_t kClaudeDim    = 0x995230;
constexpr uint32_t kGemini       = 0xC47BFF;   // purple
constexpr uint32_t kGeminiDim    = 0x76499A;
constexpr uint32_t kTextPrimary  = 0xF4F6F8;   // off-white, cool tint
constexpr uint32_t kTextMuted    = 0x7B8590;   // cool grey, instrument feel
constexpr uint32_t kRingTrack    = 0x14181C;   // tinted near-black, separates from background
constexpr uint32_t kCenterWell   = 0x0A0D10;   // gravity disc behind center metric

constexpr uint32_t kPillStale    = 0xFFB37A;   // amber
constexpr uint32_t kPillError    = 0xFF6666;   // red
constexpr uint32_t kPillInfo     = 0x8A8D92;   // muted

constexpr int kRingStroke    = 14;   // pixels
constexpr int kRingOuterR    = 200;
constexpr int kRingMiddleR   = 150;
constexpr int kRingInnerR    = 100;
constexpr int kCenterX       = 233;  // 466 / 2
constexpr int kCenterY       = 233;

inline uint32_t colorFor(ProviderID id) {
    switch (id) {
        case ProviderID::Codex:  return kCodex;
        case ProviderID::Claude: return kClaude;
        case ProviderID::Gemini: return kGemini;
    }
    return kTextMuted;
}

inline uint32_t colorDimFor(ProviderID id) {
    switch (id) {
        case ProviderID::Codex:  return kCodexDim;
        case ProviderID::Claude: return kClaudeDim;
        case ProviderID::Gemini: return kGeminiDim;
    }
    return kRingTrack;
}

}  // namespace stopwatch::theme
