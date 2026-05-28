#pragma once
#include <cstdint>

namespace stopwatch {

constexpr const char *kServiceUUID  = "91412041-D927-4633-A0ED-B066DF91EE55";
constexpr const char *kSnapshotUUID = "621645B4-14D2-4E58-975B-73B81D43916D";
constexpr const char *kTriggerUUID  = "6817329E-A603-4A34-BB4D-04215218304C";
constexpr const char *kLocalName    = "Stopwatch Bridge";

constexpr uint8_t  kVersionMajor       = 1;
constexpr uint8_t  kVersionMinor       = 0;
constexpr uint8_t  kHeaderSize         = 8;
constexpr uint8_t  kPerProviderSize    = 16;
constexpr uint8_t  kProviderCount      = 3;
constexpr uint16_t kSnapshotSize       = kHeaderSize + kPerProviderSize * kProviderCount;  // 56

enum class ProviderID : uint8_t { Codex = 1, Claude = 2, Gemini = 3 };
enum class ProviderStatus : uint8_t { Ok = 0, Warn = 1, Critical = 2, Error = 3, Disabled = 4 };
enum class ProviderPlan   : uint8_t { Unknown = 0, Free = 1, Plus = 2, Pro = 3, Team = 4, Enterprise = 5 };

constexpr uint8_t kFlagStale           = 0b00000001;
constexpr uint8_t kFlagBridgeError     = 0b00000010;
constexpr uint8_t kFlagProviderMissing = 0b00000100;

}  // namespace stopwatch
