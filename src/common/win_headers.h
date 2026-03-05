#pragma once
// =============================================================================
// CursorShare — Windows Header Inclusion Guard
//
// This file MUST be included instead of directly including Windows/Bluetooth
// headers. The include order is critical for MSVC compatibility:
//   1. winsock2.h (before windows.h to avoid winsock1 conflicts)
//   2. ws2bth.h (Bluetooth Winsock extensions)
//   3. windows.h (core Win32 API)
//   4. bluetoothapis.h (Bluetooth APIs)
//
// DO NOT re-sort these includes. The order matters.
// =============================================================================

// clang-format off

// Step 1: Winsock2 must come first
#ifndef _WINSOCKAPI_
#include <winsock2.h>
#endif
#include <ws2bth.h>

// Step 2: Core Windows
#ifndef _WINDOWS_
#include <windows.h>
#endif

// Step 3: Bluetooth APIs
#include <bluetoothapis.h>

// clang-format on

// Link libraries
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bthprops.lib")
