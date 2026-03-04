# CursorShare

**Kernel-Assisted Ultra-Low-Latency Bluetooth HID Redirect System**

CursorShare turns a Windows PC into a Bluetooth Classic HID transmitter, forwarding wired keyboard and mouse input to any Bluetooth-capable client device (phone, tablet, PC, smart TV). The client sees a standard Bluetooth keyboard + mouse — **no software install required**.

## Features

- **Bluetooth Classic HID** — Appears as a standard keyboard + mouse combo device
- **Raw Input API Capture** — User-mode fallback requiring no driver installation
- **KMDF Filter Drivers** — Kernel-mode capture for ultra-low latency (< 0.5 ms)
- **Lock-Free Ring Buffers** — Zero-allocation event pipeline
- **Boundary-Aware Mouse** — Edge clamping to client display resolution
- **Global Shortcut** — Instant input switching (Ctrl+Alt+S)
- **Latency Monitoring** — Per-stage QPC-based timing (min/avg/p99/max)
- **Safe Shutdown** — Full system state restoration, paired-device preservation
- **Cross-Platform Clients** — Windows, Android, macOS, Smart TVs

## Latency Budget

| Stage | Target |
|-------|--------|
| Hardware interrupt → driver | < 0.5 ms |
| Routing decision | < 0.1 ms |
| HID encoding | < 0.2 ms |
| Bluetooth transmission | 3–7 ms |
| **Total** | **3–8 ms typical** |

## Prerequisites

- **Windows 10/11** (x64)
- **Visual Studio 2022** with:
  - C++ Desktop Development workload
  - Windows SDK (10.0.22621+)
- **CMake 3.20+**
- **Bluetooth adapter** with Bluetooth Classic (BR/EDR) support

### Optional (for kernel drivers)

- **Windows Driver Kit (WDK)** matching the SDK version
- **Test-signing mode** enabled: `bcdedit /set testsigning on`
- **EV code-signing certificate** for production deployment

## Quick Start

### Build (User-Mode Components)

```powershell
# From CursorShare root directory
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Run

```powershell
.\build\Release\CursorShare.exe
```

### Commands

| Command | Description |
|---------|-------------|
| `start` | Start Bluetooth broadcasting |
| `stop` | Stop broadcasting |
| `switch` | Toggle input routing (Host ↔ Client) |
| `status` | Show current status |
| `diag` | Run Bluetooth diagnostics |
| `latency` | Show latency statistics |
| `devices` | List connected devices |
| `paired` | List paired devices |
| `quit` | Shutdown and exit |

### Bluetooth Diagnostics

```powershell
.\build\Release\BtDiagnostic.exe
```

### Latency Benchmark

```powershell
.\build\Release\LatencyBenchmark.exe
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Windows Host PC                       │
│                                                          │
│  ┌──────────────┐   ┌──────────────┐   ┌─────────────┐ │
│  │  Keyboard     │   │  Mouse        │   │  UI (WPF)   │ │
│  │  (HW Device)  │   │  (HW Device)  │   │  Named Pipe │ │
│  └──────┬───────┘   └──────┬───────┘   └──────┬──────┘ │
│         │                   │                   │        │
│  ┌──────┴───────────────────┴───────┐          │        │
│  │  Input Capture Layer              │          │        │
│  │  ┌─────────────┐ ┌─────────────┐ │          │        │
│  │  │ KMDF Filter  │ │ Raw Input   │ │          │        │
│  │  │ Driver       │ │ API         │ │          │        │
│  │  └──────┬──────┘ └──────┬──────┘ │          │        │
│  │         │ Shared Mem     │ Ring    │          │        │
│  │         └────────┬───────┘ Buffer │          │        │
│  └──────────────────┼────────────────┘          │        │
│                     ▼                           │        │
│  ┌─────────────────────────────┐                │        │
│  │  Input Router                │◄──────────────┘        │
│  │  (Host/Client switching)     │                         │
│  │  Global Shortcut Handler     │                         │
│  └─────────────┬───────────────┘                         │
│                │                                          │
│  ┌─────────────▼───────────────┐                         │
│  │  Mouse Boundary Handler      │                         │
│  │  (Edge clamping)             │                         │
│  └─────────────┬───────────────┘                         │
│                │                                          │
│  ┌─────────────▼───────────────┐                         │
│  │  HID Report Encoder          │                         │
│  │  (Keyboard 6KRO + Mouse)     │                         │
│  └─────────────┬───────────────┘                         │
│                │                                          │
│  ┌─────────────▼───────────────┐                         │
│  │  Bluetooth HID Service       │                         │
│  │  ┌──────────┐ ┌───────────┐ │                         │
│  │  │ L2CAP    │ │ SDP       │ │                         │
│  │  │ Channels │ │ Record    │ │                         │
│  │  └──────────┘ └───────────┘ │                         │
│  └─────────────┬───────────────┘                         │
│                │ Bluetooth Classic                        │
└────────────────┼─────────────────────────────────────────┘
                 │
                 ▼
    ┌────────────────────────┐
    │  Client Device          │
    │  (Phone/Tablet/TV/PC)   │
    │  Standard BT HID        │
    │  No software install    │
    └────────────────────────┘
```

## Project Structure

```
CursorShare/
├── CMakeLists.txt              # Build system
├── README.md                   # This file
├── docs/                       # Documentation
├── src/
│   ├── main.cpp                # Entry point
│   ├── common/                 # Shared headers
│   │   ├── constants.h         # Project-wide constants
│   │   ├── hid_descriptors.h   # HID report descriptors
│   │   ├── input_event.h       # Input event structures
│   │   ├── ipc_protocol.h      # IPC definitions
│   │   └── ring_buffer.h       # Lock-free SPSC ring buffer
│   ├── bluetooth/              # Bluetooth subsystem
│   │   ├── bt_validator.*      # Adapter validation
│   │   ├── bt_hid_service.*    # HID service manager
│   │   └── bt_pairing.*        # Pairing preservation
│   ├── input/                  # Input capture subsystem
│   │   ├── raw_input_capture.* # Raw Input API
│   │   ├── input_router.*      # Host/Client routing
│   │   ├── shortcut_manager.*  # Global shortcut
│   │   └── mouse_boundary.*    # Boundary-aware mouse
│   ├── driver/                 # KMDF filter drivers
│   │   ├── kbfilter/           # Keyboard filter
│   │   └── moufilter/          # Mouse filter
│   └── service/                # Service layer
│       ├── cursorshare_service.* # Main orchestrator
│       └── latency_monitor.*   # Pipeline timing
├── tools/                      # Diagnostic tools
│   ├── bt_diagnostic.cpp       # BT adapter checker
│   └── latency_benchmark.cpp   # Performance benchmark
└── tests/                      # Test suite
```

## Building KMDF Drivers

Kernel drivers require the **Windows Driver Kit (WDK)** and must be built separately:

1. Open the driver project in Visual Studio with WDK installed
2. Select **x64 Release** configuration
3. Build the project
4. Enable test-signing: `bcdedit /set testsigning on`
5. Sign with test certificate: `signtool sign /v /s PrivateCertStore /n CursorShareTest /t http://timestamp.digicert.com *.sys`
6. Install via INF: `devcon install kbfilter.inf *whatever_hwid*`

See `docs/driver-signing-guide.md` for detailed instructions.

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

## Safety Guarantees

- **Paired-device preservation**: Snapshots paired devices on startup, verifies on shutdown
- **Input restoration**: All hooks and filters fully removed on exit
- **Bluetooth cleanup**: L2CAP channels closed, SDP records unregistered, discoverability restored
- **No persistent changes**: Default Windows behavior fully restored on exit
- **Deterministic shutdown**: Verified cleanup sequence with error reporting

## License

This project is proprietary. All rights reserved.
