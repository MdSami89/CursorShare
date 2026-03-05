#pragma once
// =============================================================================
// CursorShare — Input Event Structures
// Compact, cache-friendly event types shared across kernel and user-mode.
// =============================================================================

#include <cstdint>

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#endif

namespace CursorShare {

// ---------------------------------------------------------------------------
// Event Types
// ---------------------------------------------------------------------------
enum class InputEventType : uint8_t {
    None          = 0,
    KeyDown       = 1,
    KeyUp         = 2,
    MouseMove     = 3,
    MouseButtonDown = 4,
    MouseButtonUp   = 5,
    MouseWheel    = 6,
    MouseHWheel   = 7,  // Horizontal scroll
};

// ---------------------------------------------------------------------------
// Mouse Buttons (bitmask)
// ---------------------------------------------------------------------------
enum class MouseButton : uint8_t {
    None   = 0x00,
    Left   = 0x01,
    Right  = 0x02,
    Middle = 0x04,
    X1     = 0x08,
    X2     = 0x10,
};

inline MouseButton operator|(MouseButton a, MouseButton b) {
    return static_cast<MouseButton>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline MouseButton operator&(MouseButton a, MouseButton b) {
    return static_cast<MouseButton>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

// ---------------------------------------------------------------------------
// Keyboard Event (8 bytes payload)
// ---------------------------------------------------------------------------
struct KeyboardEvent {
    uint16_t scanCode;      // Scan code (Make code)
    uint16_t virtualKey;    // Virtual key code (VK_xxx)
    uint8_t  flags;         // E0/E1 prefix flags
    uint8_t  reserved[3];
};
static_assert(sizeof(KeyboardEvent) == 8, "KeyboardEvent must be 8 bytes");

// ---------------------------------------------------------------------------
// Mouse Event (12 bytes payload)
// ---------------------------------------------------------------------------
struct MouseEvent {
    int16_t  dx;            // Relative X movement
    int16_t  dy;            // Relative Y movement
    int16_t  wheelDelta;    // Vertical scroll delta
    int16_t  hWheelDelta;   // Horizontal scroll delta
    uint8_t  buttons;       // Current button state (MouseButton bitmask)
    uint8_t  reserved[3];
};
static_assert(sizeof(MouseEvent) == 12, "MouseEvent must be 12 bytes");

// ---------------------------------------------------------------------------
// Unified Input Event (32 bytes total — fits one half cache line)
// ---------------------------------------------------------------------------
struct alignas(32) InputEvent {
    InputEventType type;      // 1 byte
    uint8_t        reserved;  // 1 byte padding
    uint16_t       sequence;  // 2 bytes — sequence number for ordering
    int64_t        timestamp; // 8 bytes — QPC timestamp (100ns ticks)

    union {
        KeyboardEvent keyboard;  // 8 bytes
        MouseEvent    mouse;     // 12 bytes
        uint8_t       raw[12];   // Raw access
    } data;                      // 12 bytes

    uint32_t padding;            // 4 bytes — pad to 32 bytes total
};
static_assert(sizeof(InputEvent) == 32, "InputEvent must be 32 bytes");

// ---------------------------------------------------------------------------
// Helper: Get current QPC timestamp
// ---------------------------------------------------------------------------
#ifndef _KERNEL_MODE
inline int64_t GetQPCTimestamp() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}
#endif

}  // namespace CursorShare
