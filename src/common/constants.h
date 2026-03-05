#pragma once
// =============================================================================
// CursorShare — Project-Wide Constants
// =============================================================================

#include <cstdint>

namespace CursorShare {

// ---------------------------------------------------------------------------
// HID Usage Pages & Usages
// ---------------------------------------------------------------------------
constexpr uint16_t kHidUsagePageGenericDesktop = 0x01;
constexpr uint16_t kHidUsagePageKeyboard       = 0x07;
constexpr uint16_t kHidUsagePageButton         = 0x09;
constexpr uint16_t kHidUsagePageConsumer       = 0x0C;

constexpr uint16_t kHidUsageMouse    = 0x02;
constexpr uint16_t kHidUsageKeyboard = 0x06;

// HID Report IDs
constexpr uint8_t kReportIdKeyboard = 0x01;
constexpr uint8_t kReportIdMouse    = 0x02;

// ---------------------------------------------------------------------------
// Bluetooth Constants
// ---------------------------------------------------------------------------
// Standard Bluetooth HID PSMs (reserved by Windows — used in kernel driver)
constexpr uint16_t kPsmHidControl   = 0x0011;
constexpr uint16_t kPsmHidInterrupt = 0x0013;

// Custom PSMs for user-mode fallback (dynamically assigned range)
constexpr uint16_t kPsmCustomControl   = 0x1001;
constexpr uint16_t kPsmCustomInterrupt = 0x1003;

// Bluetooth Class of Device: Keyboard + Pointing combo
// Major: Peripheral (0x05), Minor: Combo keyboard/pointing (0x03)
constexpr uint32_t kBluetoothCoD = 0x002540;

// SDP Service Class UUID for HID
constexpr uint16_t kSdpHidServiceClassUuid = 0x1124;

// Bluetooth device name advertised by CursorShare
constexpr const char* kBluetoothDeviceName = "CursorShare HID";

// ---------------------------------------------------------------------------
// Ring Buffer / IPC
// ---------------------------------------------------------------------------
constexpr size_t kRingBufferCapacity = 4096;  // Number of events
constexpr size_t kRingBufferSizeBytes = kRingBufferCapacity * 32;  // ~128 KB

// Shared memory section names (kernel <-> user)
constexpr const wchar_t* kSharedMemKeyboard = L"\\BaseNamedObjects\\CursorShareKbdShm";
constexpr const wchar_t* kSharedMemMouse    = L"\\BaseNamedObjects\\CursorShareMouShm";

// Event names for signaling
constexpr const wchar_t* kEventKeyboardReady = L"Global\\CursorShareKbdEvent";
constexpr const wchar_t* kEventMouseReady    = L"Global\\CursorShareMouEvent";

// Named pipe for UI <-> Service communication
constexpr const wchar_t* kServicePipeName = L"\\\\.\\pipe\\CursorShareService";

// ---------------------------------------------------------------------------
// Latency Budget (microseconds)
// ---------------------------------------------------------------------------
constexpr int64_t kLatencyBudgetHwInterrupt_us  = 500;   // < 0.5 ms
constexpr int64_t kLatencyBudgetRouting_us       = 100;   // < 0.1 ms
constexpr int64_t kLatencyBudgetEncoding_us      = 200;   // < 0.2 ms
constexpr int64_t kLatencyBudgetBtTransmit_us    = 7000;  // 3–7 ms
constexpr int64_t kLatencyBudgetTotal_us         = 8000;  // < 8 ms total

// ---------------------------------------------------------------------------
// Input Configuration
// ---------------------------------------------------------------------------
constexpr int kKeyboardMaxKeys = 6;  // 6-key rollover
constexpr int kMouseButtonCount = 5; // Left, Right, Middle, X1, X2

// Default global shortcut: Ctrl+Alt+S
constexpr int kDefaultShortcutModifiers = 0x0006;  // MOD_CONTROL | MOD_ALT
constexpr int kDefaultShortcutVk        = 0x53;    // 'S'

// Failover time target
constexpr int64_t kFailoverTarget_us = 10000;  // < 10 ms

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------
constexpr const char* kAppName    = "CursorShare";
constexpr const char* kAppVersion = "0.1.0";

}  // namespace CursorShare
