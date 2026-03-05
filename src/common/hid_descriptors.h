#pragma once
// =============================================================================
// CursorShare — HID Report Descriptors
// Bluetooth HID device descriptors for keyboard (6KRO) and mouse (3+btn, wheel)
// Follows USB HID Specification v1.11 and HID Usage Tables v1.12
// =============================================================================

#include <cstddef>
#include <cstdint>


namespace CursorShare {

// ---------------------------------------------------------------------------
// HID Descriptor Shorthand Macros
// ---------------------------------------------------------------------------
#define HID_USAGE_PAGE(x) 0x05, (x)
#define HID_USAGE_PAGE_16(x) 0x06, ((x) & 0xFF), (((x) >> 8) & 0xFF)
#define HID_USAGE(x) 0x09, (x)
#define HID_USAGE_16(x) 0x0A, ((x) & 0xFF), (((x) >> 8) & 0xFF)
#define HID_USAGE_MIN(x) 0x19, (x)
#define HID_USAGE_MAX(x) 0x29, (x)
#define HID_USAGE_MIN_16(x) 0x1A, ((x) & 0xFF), (((x) >> 8) & 0xFF)
#define HID_USAGE_MAX_16(x) 0x2A, ((x) & 0xFF), (((x) >> 8) & 0xFF)
#define HID_LOG_MIN(x) 0x15, (x)
#define HID_LOG_MIN_16(x) 0x16, ((x) & 0xFF), (((x) >> 8) & 0xFF)
#define HID_LOG_MAX(x) 0x25, (x)
#define HID_LOG_MAX_16(x) 0x26, ((x) & 0xFF), (((x) >> 8) & 0xFF)
#define HID_REPORT_SIZE(x) 0x75, (x)
#define HID_REPORT_COUNT(x) 0x95, (x)
#define HID_REPORT_ID(x) 0x85, (x)
#define HID_COLLECTION(x) 0xA1, (x)
#define HID_END_COLLECTION 0xC0
#define HID_INPUT(x) 0x81, (x)
#define HID_OUTPUT(x) 0x91, (x)
#define HID_FEATURE(x) 0xB1, (x)
#define HID_PHYS_MIN(x) 0x35, (x)
#define HID_PHYS_MAX_16(x) 0x46, ((x) & 0xFF), (((x) >> 8) & 0xFF)
#define HID_UNIT_EXPONENT(x) 0x55, (x)
#define HID_UNIT(x) 0x65, (x)

// Collection types
#define COLLECTION_APPLICATION 0x01
#define COLLECTION_LOGICAL 0x02

// Input/Output/Feature flags
#define DATA_VAR_ABS 0x02   // Data, Variable, Absolute
#define DATA_VAR_REL 0x06   // Data, Variable, Relative
#define CONST_VAR_ABS 0x03  // Constant, Variable, Absolute
#define DATA_ARRAY_ABS 0x00 // Data, Array, Absolute

// ---------------------------------------------------------------------------
// Keyboard HID Report Descriptor (Report ID 0x01)
// 6-Key Rollover + Modifiers + LEDs
//
// INPUT Report (8 bytes):
//   Byte 0: Modifier keys (bitmap: LCtrl, LShift, LAlt, LGUI, RCtrl, RShift,
//   RAlt, RGUI) Byte 1: Reserved (0x00) Bytes 2-7: Key codes (up to 6
//   simultaneous keys)
//
// OUTPUT Report (1 byte):
//   Byte 0: LED indicators (NumLock, CapsLock, ScrollLock, Compose, Kana)
// ---------------------------------------------------------------------------
static const uint8_t kKeyboardHidDescriptor[] = {
    HID_USAGE_PAGE(0x01), // Generic Desktop
    HID_USAGE(0x06),      // Keyboard
    HID_COLLECTION(COLLECTION_APPLICATION), HID_REPORT_ID(0x01),

    // Modifier keys (8 bits)
    HID_USAGE_PAGE(0x07), // Keyboard/Keypad
    HID_USAGE_MIN(0xE0),  // Left Control
    HID_USAGE_MAX(0xE7),  // Right GUI
    HID_LOG_MIN(0x00), HID_LOG_MAX(0x01), HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(8),
    HID_INPUT(DATA_VAR_ABS), // 8 modifier bits

    // Reserved byte
    HID_REPORT_COUNT(1), HID_REPORT_SIZE(8),
    HID_INPUT(CONST_VAR_ABS), // 1 byte reserved

    // LED output report (5 LEDs)
    HID_USAGE_PAGE(0x08), // LEDs
    HID_USAGE_MIN(0x01),  // Num Lock
    HID_USAGE_MAX(0x05),  // Kana
    HID_REPORT_COUNT(5), HID_REPORT_SIZE(1),
    HID_OUTPUT(DATA_VAR_ABS), // 5 LED bits

    // LED padding (3 bits)
    HID_REPORT_COUNT(1), HID_REPORT_SIZE(3),
    HID_OUTPUT(CONST_VAR_ABS), // Pad to byte boundary

    // Key array (6 keys)
    HID_USAGE_PAGE(0x07), // Keyboard/Keypad
    HID_USAGE_MIN(0x00), HID_USAGE_MAX(0xFF), HID_LOG_MIN(0x00),
    HID_LOG_MAX_16(0x00FF), HID_REPORT_SIZE(8), HID_REPORT_COUNT(6),
    HID_INPUT(DATA_ARRAY_ABS), // 6 key codes

    HID_END_COLLECTION};

static const size_t kKeyboardHidDescriptorSize = sizeof(kKeyboardHidDescriptor);

// Keyboard input report size (excluding report ID): 8 bytes
static const size_t kKeyboardReportSize = 8;

// ---------------------------------------------------------------------------
// Mouse HID Report Descriptor (Report ID 0x02)
// 5 Buttons + X/Y relative (16-bit) + Vertical + Horizontal scroll
//
// INPUT Report (7 bytes):
//   Byte 0: Buttons (5 bits) + padding (3 bits)
//   Bytes 1-2: X movement (16-bit signed, relative)
//   Bytes 3-4: Y movement (16-bit signed, relative)
//   Byte 5: Vertical wheel (8-bit signed)
//   Byte 6: Horizontal wheel (8-bit signed)
// ---------------------------------------------------------------------------
static const uint8_t kMouseHidDescriptor[] = {
    HID_USAGE_PAGE(0x01), // Generic Desktop
    HID_USAGE(0x02),      // Mouse
    HID_COLLECTION(COLLECTION_APPLICATION), HID_REPORT_ID(0x02),
    HID_USAGE(0x01), // Pointer
    HID_COLLECTION(COLLECTION_LOGICAL),

    // Buttons (5 buttons)
    HID_USAGE_PAGE(0x09), // Button
    HID_USAGE_MIN(0x01),  // Button 1 (Left)
    HID_USAGE_MAX(0x05),  // Button 5 (X2)
    HID_LOG_MIN(0x00), HID_LOG_MAX(0x01), HID_REPORT_COUNT(5),
    HID_REPORT_SIZE(1),
    HID_INPUT(DATA_VAR_ABS), // 5 button bits

    // Button padding (3 bits)
    HID_REPORT_COUNT(1), HID_REPORT_SIZE(3), HID_INPUT(CONST_VAR_ABS),

    // X, Y movement (16-bit relative)
    HID_USAGE_PAGE(0x01),   // Generic Desktop
    HID_USAGE(0x30),        // X
    HID_USAGE(0x31),        // Y
    HID_LOG_MIN_16(0x8001), // -32767
    HID_LOG_MAX_16(0x7FFF), // +32767
    HID_REPORT_SIZE(16), HID_REPORT_COUNT(2),
    HID_INPUT(DATA_VAR_REL), // 2x 16-bit relative

    // Vertical scroll wheel
    HID_USAGE(0x38), // Wheel
    HID_LOG_MIN(static_cast<uint8_t>(-127)), HID_LOG_MAX(127),
    HID_REPORT_SIZE(8), HID_REPORT_COUNT(1), HID_INPUT(DATA_VAR_REL),

    // Horizontal scroll wheel
    HID_USAGE_PAGE(0x0C), // Consumer
    HID_USAGE_16(0x0238), // AC Pan
    HID_LOG_MIN(static_cast<uint8_t>(-127)), HID_LOG_MAX(127),
    HID_REPORT_SIZE(8), HID_REPORT_COUNT(1), HID_INPUT(DATA_VAR_REL),

    HID_END_COLLECTION, HID_END_COLLECTION};

static const size_t kMouseHidDescriptorSize = sizeof(kMouseHidDescriptor);

// Mouse input report size (excluding report ID): 7 bytes
static const size_t kMouseReportSize = 7;

// ---------------------------------------------------------------------------
// Combined HID Descriptor (for SDP registration)
// Concatenation of keyboard + mouse descriptors
// ---------------------------------------------------------------------------
// NOTE: Combined descriptor is built at runtime by concatenating the above.
// See bt_sdp.cpp for SDP record construction.

// ---------------------------------------------------------------------------
// HID Report Encoding Helpers
// ---------------------------------------------------------------------------

/// Encode a keyboard HID input report (8 bytes, no report ID prefix).
/// @param modifiers  Modifier key bitmask (bit0=LCtrl ... bit7=RGUI)
/// @param keys       Array of up to 6 HID key codes (0 = no key)
/// @param out        Output buffer (must be >= 8 bytes)
inline void EncodeKeyboardReport(uint8_t modifiers, const uint8_t keys[6],
                                 uint8_t out[8]) {
  out[0] = modifiers;
  out[1] = 0x00; // Reserved
  out[2] = keys[0];
  out[3] = keys[1];
  out[4] = keys[2];
  out[5] = keys[3];
  out[6] = keys[4];
  out[7] = keys[5];
}

/// Encode a mouse HID input report (7 bytes, no report ID prefix).
/// @param buttons    Button bitmask (bit0=Left, bit1=Right, bit2=Middle, ...)
/// @param dx         Relative X movement (-32767..32767)
/// @param dy         Relative Y movement (-32767..32767)
/// @param wheel      Vertical scroll (-127..127)
/// @param hwheel     Horizontal scroll (-127..127)
/// @param out        Output buffer (must be >= 7 bytes)
inline void EncodeMouseReport(uint8_t buttons, int16_t dx, int16_t dy,
                              int8_t wheel, int8_t hwheel, uint8_t out[7]) {
  out[0] = buttons;
  out[1] = static_cast<uint8_t>(dx & 0xFF);
  out[2] = static_cast<uint8_t>((dx >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(dy & 0xFF);
  out[4] = static_cast<uint8_t>((dy >> 8) & 0xFF);
  out[5] = static_cast<uint8_t>(wheel);
  out[6] = static_cast<uint8_t>(hwheel);
}

/// Encode an "all keys up" keyboard report (all zeros).
inline void EncodeKeyboardAllUp(uint8_t out[8]) {
  for (int i = 0; i < 8; ++i)
    out[i] = 0;
}

/// Encode a "no movement, no buttons" mouse report.
inline void EncodeMouseIdle(uint8_t out[7]) {
  for (int i = 0; i < 7; ++i)
    out[i] = 0;
}

} // namespace CursorShare
