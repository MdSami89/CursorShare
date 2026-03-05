# CursorShare

**Ultra-Low-Latency Bluetooth HID Redirect System**

CursorShare turns a Windows PC into a Bluetooth HID peripheral, forwarding wired keyboard and mouse input to any Bluetooth-capable client device (phone, tablet, PC, smart TV). The client sees a standard Bluetooth keyboard + mouse — **no software install required on the client**.

## Features

- **BLE HID over GATT** — Appears as a standard BLE keyboard + mouse (Windows 10+ C++/WinRT)
- **Bluetooth Classic HID** — Legacy SDP/L2CAP path for older clients
- **Exclusive Input Mode** — Low-level hooks suppress host input when routing to client, ensuring input goes to only one device at a time
- **Proper HID Scan Code Mapping** — Full Windows scan code → USB HID usage code lookup table (256 entries + E0-prefixed keys)
- **Raw Input API Capture** — User-mode capture requiring no driver installation
- **KMDF Filter Drivers** — Kernel-mode capture for ultra-low latency (< 0.5 ms)
- **Lock-Free Ring Buffers** — Zero-allocation SPSC event pipeline
- **Boundary-Aware Mouse** — Edge clamping to client display resolution
- **Global Shortcut** — Instant input switching (Ctrl+Alt+S)
- **Latency Monitoring** — Per-stage QPC-based timing (min/avg/p99/max)
- **File-Based Logging** — Thread-safe singleton logger with 6 levels, log rotation (5 MB max, 3 backups), millisecond timestamps
- **Safe Shutdown** — Full system state restoration, paired-device preservation
- **Cross-Platform Clients** — Windows, Android, macOS, Smart TVs

## Latency Budget

| Stage | Target |
|-------|--------|
| Hardware interrupt → capture | < 0.5 ms |
| Routing decision | < 0.1 ms |
| HID encoding + scan code mapping | < 0.2 ms |
| BLE GATT notification | 3–7 ms |
| **Total** | **3–8 ms typical** |

## Tested On

| Role | Device | Status |
|------|--------|--------|
| **Host** (Windows PC) | TP-Link UB500 Bluetooth 5.4 Adapter | ✅ Working |
| **Client** (Tablet) | Samsung Galaxy Tab A9 | ✅ Working |

## Project Status
🚧 In Progress

## Prerequisites

- **Windows 10/11** (x64)
- **Visual Studio 2022 Build Tools** with:
  - C++ Desktop Development workload
  - Windows SDK (10.0.26100+)
  - C++/WinRT headers
- **CMake 3.20+**
- **Bluetooth adapter** with BLE peripheral role support

### Optional (for kernel drivers)

- **Windows Driver Kit (WDK)** matching the SDK version
- **Test-signing mode** enabled: `bcdedit /set testsigning on`

## Quick Start

### Build

```powershell
# Open a VS Developer Command Prompt (or call vcvars64.bat first)
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

## Pre-built executables are also available, You can download From [Releases](https://github.com/MdSami89/CursorShare/releases)

### Run

```powershell
# From dist/ or build/ folder
.\CursorShare.exe
```

### Interactive Commands

| Command | Description |
|---------|-------------|
| `start` | Start Bluetooth Classic broadcasting |
| `ble` | Start BLE HID advertising (recommended) |
| `stop` | Stop broadcasting |
| `switch` | Toggle input routing (Host ↔ Client) |
| `status` | Show current status |
| `diag` | Run Bluetooth diagnostics |
| `latency` | Show latency statistics |
| `devices` | List connected devices |
| `paired` | List paired devices |
| `help` | Show all commands |
| `quit` | Shutdown and exit |

### Diagnostic Tools

```powershell
.\BtDiagnostic.exe        # Validate Bluetooth adapter compatibility
.\LatencyBenchmark.exe     # Measure pipeline latency
```

Both tools write to `cursorshare.log` alongside the executable.

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    Windows Host PC                       │
│                                                          │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐  │
│  │  Keyboard    │   │  Mouse       │   │  UI (future) │  │
│  │  (HW Device) │   │  (HW Device) │   │  Named Pipe  │  │
│  └──────┬───────┘   └──────┬───────┘   └──────┬───────┘  │
│         │                  │                  │          │
│  ┌──────┴───────────────────┴───────┐         │          │
│  │  Input Capture Layer             │         │          │
│  │  ┌─────────────┐ ┌─────────────┐ │         │          │
│  │  │ KMDF Filter │ │ Raw Input   │ │         │          │
│  │  │ Driver      │ │ API         │ │         │          │
│  │  └──────┬──────┘ └──────┬──────┘ │         │          │
│  │         └────────┬──────┘        │         │          │
│  └──────────────────┼───────────────┘         │          │
│                     ▼                         │          │
│  ┌──────────────────────────────┐             │          │
│  │  Input Router                │◄────────────┘          │
│  │  Host/Client switching       │                        │
│  │  Exclusive Mode (LL Hooks)   │                        │
│  └─────────────┬────────────────┘                        │
│                │                                         │
│  ┌─────────────▼────────────────┐                        │
│  │  Mouse Boundary Handler      │                        │
│  │  (Edge clamping)             │                        │
│  └─────────────┬────────────────┘                        │
│                │                                         │
│  ┌─────────────▼──────────────┐                          │
│  │  Scan Code → HID Mappin    │                          │
│  │  (scancode_to_hid.h les)   │                          │
│  └─────────────┬──────────────┘                          │
│                │                                         │
│  ┌─────────────▼──────────────┐                          │
│  │  BLE HID ovGATT (primary)  │                          │
│  │  ┌──────────┐ ┌──────────┐ │                          │
│  │  │ HIDrvice │ │ DevInfo  │ │                          │
│  │  │ 812      │ │ Battery  │ │                          │
│  │  ───────────┘ └──────────┘ │                          │
│  |────────────────────────────┤                          │
│  |  BT Classic HID (fallback) │                          │
│  │  ┌──────────┐ ┌───────────┐│                          │
│  │  │ L2CAP    │ │ SDP       |│                          │
│  │  └──────────┘ └───────────┘│                          │
│  └─────────────┬──────────────┘                          │
│                │ Bluetooth                               │
└────────────────┼─────────────────────────────────────────┘
                 │
                 ▼
    ┌────────────────────────┐
    │  Client Device         │
    │  (Phone/Tablet/TV/PC)  │
    │  Standard BLE/BT HID   │
    │  No software install   │
    └────────────────────────┘
```




## Logging

CursorShare uses a centralized file-based logger that writes to `cursorshare.log` next to the executable.

- **6 log levels**: Trace, Debug, Info, Warn, Error, Fatal
- **Log rotation**: 5 MB max per file, keeps 3 rotated backups
- **Thread-safe**: Mutex-protected writes with millisecond timestamps
- **Console mirror**: All log entries are also printed to the console
- **Hot-path excluded**: Individual mouse/keyboard event data is not logged (too noisy)

Example log output:
```
[2026-03-05 04:50:42.123] [INFO ] [BLE-HID] BLE HID Service initialized.
[2026-03-05 04:50:43.456] [INFO ] [Router] Switched to CLIENT mode.
[2026-03-05 04:50:43.457] [INFO ] [Input ] Exclusive mode ENABLED — host input suppressed.
```

## HID Descriptors

CursorShare implements two HID report descriptors:

### Keyboard (Report ID 0x01)
- 8-bit modifier keys (LCtrl, LShift, LAlt, LGUI, RCtrl, RShift, RAlt, RGUI)
- 6-key rollover array
- 5 LED indicators (NumLock, CapsLock, ScrollLock, Compose, Kana)

### Mouse (Report ID 0x02)
- 5 buttons (Left, Right, Middle, X1, X2)
- 16-bit X/Y relative movement
- Vertical scroll wheel
- Horizontal scroll wheel (AC Pan)

## BLE GATT Services

| Service | UUID | Purpose |
|---------|------|---------|
| HID Service | `0x1812` | Keyboard + Mouse reports, Report Map, HID Info, Control Point |
| Device Information | `0x180A` | Manufacturer, Model, PnP ID |
| Battery Service | `0x180F` | Battery level (fixed at 100%) |

## Safety Guarantees

- **Exclusive input**: Low-level hooks ensure input goes to only one device at a time
- **Paired-device preservation**: Snapshots paired devices on startup, verifies on shutdown
- **Input restoration**: All hooks and filters fully removed on exit
- **Bluetooth cleanup**: GATT services stopped, L2CAP channels closed, SDP records unregistered
- **No persistent changes**: Default Windows behavior fully restored on exit
- **Deterministic shutdown**: Verified cleanup sequence with error reporting to log file

## Building KMDF Drivers

Kernel drivers require the **Windows Driver Kit (WDK)** and must be built separately:

1. Open the driver project in Visual Studio with WDK installed
2. Select **x64 Release** configuration
3. Build the project
4. Enable test-signing: `bcdedit /set testsigning on`
5. Sign with test certificate
6. Install via INF

See `docs/driver-signing-guide.md` for detailed instructions.

## License & Attribution

This project is licensed under the **CursorShare Custom License (CSCL v1.0)**.


 **Attribution**  - Credit to the original author (**MdSami89**) is mandatory in all copies and forks 
 **Commercial Use** - Prohibited without prior  permission 
 **Forking** - Allowed - original author and repo link must remain visible 
 **Redistribution** - Re-uploading as your own work is not allowed 
 **Modification** - Allowed — must be clearly marked as modified 
 **Warranty** - Provided "as is" with no warranty of any kind 

See [LICENSE](LICENSE) and [NOTICE](NOTICE) for full terms.

### Author

**Mohammad Sami** ([@MdSami89](https://github.com/MdSami89))

For commercial licensing, permissions, or inquiries:
[github.com/MdSami89](https://github.com/MdSami89)
