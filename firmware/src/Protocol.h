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

constexpr const char *kCostSnapshotUUID = "33FAAC2D-3935-467F-A0A0-899CE2306366";

constexpr uint8_t  kCostVersionMajor    = 2;
constexpr uint8_t  kCostHeaderSize      = 12;
constexpr uint8_t  kCostRecordSize      = 85;
constexpr uint8_t  kCostHistoryDays     = 30;
constexpr uint8_t  kCostMaxModelSlots   = 3;
constexpr uint8_t  kCostMaxRecords      = 2;   // codex, claude
constexpr uint16_t kCostSnapshotMaxSize = kCostHeaderSize + kCostRecordSize * kCostMaxRecords;  // 182

constexpr uint8_t kCostFlagStale       = 0b00000001;
constexpr uint8_t kCostFlagBridgeError = 0b00000010;
constexpr uint8_t kCostFlagUnavailable = 0b00000100;

constexpr uint8_t kTriggerScopeCost    = 0x04;

constexpr const char *kBalanceSnapshotUUID = "4D9E8F21-7C3A-4B6D-8E15-9A2F6C3B0D74";

constexpr uint8_t  kBalanceVersionMajor = 1;
constexpr uint8_t  kBalanceHeaderSize   = 8;
constexpr uint8_t  kBalanceRecordSize   = 36;
constexpr uint8_t  kBalanceMaxRecords   = 16;
constexpr uint16_t kBalanceSnapshotMaxSize = kBalanceHeaderSize + kBalanceRecordSize * kBalanceMaxRecords;  // 584

constexpr uint8_t kBalanceFlagStale       = 0b00000001;
constexpr uint8_t kBalanceFlagBridgeError = 0b00000010;
constexpr uint8_t kBalanceRecordFlagLow   = 0b00000001;

constexpr uint8_t kTriggerScopeBalances = 0x05;

constexpr const char *kUsageSnapshotUUID = "7E2C5A19-4B8F-4D3A-9E61-2F7A8C0B5D34";

constexpr uint8_t  kUsageVersionMajor  = 1;
constexpr uint8_t  kUsageHeaderSize    = 12;
constexpr uint8_t  kUsageRecordSize    = 96;
constexpr uint8_t  kUsageHistoryDays   = 30;
constexpr uint8_t  kUsageMaxRecords    = 4;    // openrouter, deepseek, aihubmix (+1 headroom)
constexpr uint16_t kUsageSnapshotMaxSize = kUsageHeaderSize + kUsageRecordSize * kUsageMaxRecords;  // 396

constexpr uint8_t kUsageFlagStale       = 0b00000001;
constexpr uint8_t kUsageFlagBridgeError = 0b00000010;
constexpr uint8_t kUsageFlagUnavailable = 0b00000100;

constexpr uint8_t kTriggerScopeUsage    = 0x06;

enum class BalanceKind : uint8_t {
    Generic = 0, OpenRouter = 1, DeepSeek = 2, Groq = 3, Together = 4,
    Fireworks = 5, SiliconFlow = 6, Moonshot = 7, Zhipu = 8
};
enum class BalanceStatus : uint8_t { Ok = 0, Stale = 1, AuthError = 2, Unreachable = 3, Depleted = 4 };

}  // namespace stopwatch
