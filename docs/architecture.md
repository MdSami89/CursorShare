# CursorShare Architecture

## System Overview

CursorShare is a multi-layer system with kernel-mode and user-mode components communicating via shared memory IPC.

## Layer Diagram

```
┌─────────────────────────────────────────┐
│              UI Layer (WPF/C#)          │  Named pipe protocol
├─────────────────────────────────────────┤
│           Service Layer (C++)           │  Orchestrator
│  ┌──────────┐ ┌──────────┐ ┌─────────┐ │
│  │ Latency  │ │ Session  │ │ Pipe    │ │
│  │ Monitor  │ │ Manager  │ │ Server  │ │
│  └──────────┘ └──────────┘ └─────────┘ │
├─────────────────────────────────────────┤
│         Routing Layer (C++)             │  Lock-free pipeline
│  ┌──────────┐ ┌──────────┐ ┌─────────┐ │
│  │ Input    │ │ Shortcut │ │ Mouse   │ │
│  │ Router   │ │ Manager  │ │ Boundary│ │
│  └──────────┘ └──────────┘ └─────────┘ │
├───────────────────┬─────────────────────┤
│ Input Capture     │ Bluetooth Output    │
│ ┌───────────────┐ │ ┌─────────────────┐ │
│ │ Raw Input API │ │ │ BT HID Service  │ │
│ │ (user-mode)   │ │ │ SDP + L2CAP     │ │
│ ├───────────────┤ │ ├─────────────────┤ │
│ │ KMDF Keyboard │ │ │ Pairing Manager │ │
│ │ Filter Driver │ │ │ (preservation)  │ │
│ ├───────────────┤ │ ├─────────────────┤ │
│ │ KMDF Mouse   │ │ │ BT Validator    │ │
│ │ Filter Driver │ │ │ (diagnostics)   │ │
│ └───────────────┘ │ └─────────────────┘ │
├───────────────────┴─────────────────────┤
│              Shared Memory IPC          │  Ring buffers
│  ┌────────────────────────────────────┐ │
│  │ Header (64B) + Ring Buffer (128KB) │ │
│  │ Keyboard: CursorShareKbdShm       │ │
│  │ Mouse:    CursorShareMouShm       │ │
│  └────────────────────────────────────┘ │
├─────────────────────────────────────────┤
│            Windows Kernel               │
│  kbdclass ←→ kbfilter ←→ i8042prt      │
│  mouclass ←→ moufilter ←→ mouhid       │
└─────────────────────────────────────────┘
```

## Event Flow

1. Hardware generates interrupt
2. Port driver (i8042prt/kbdhid) processes interrupt
3. Filter driver intercepts `ClassServiceCallback`
4. Event copied to shared memory ring buffer (32 bytes, zero-alloc)
5. Kernel event signals user-mode
6. Service reads from ring buffer
7. Input Router decides: Host or Client
8. If Client: Mouse Boundary applies edge clamping
9. HID report encoded (keyboard 8 bytes, mouse 7 bytes)
10. Report transmitted via L2CAP interrupt channel

## Key Design Decisions

- **SPSC ring buffer**: Single-producer (kernel) single-consumer (service) avoids locks
- **32-byte events**: Half cache line, trivially copyable
- **Pre-allocated buffers**: Zero heap allocation in hot path
- **High-priority threads**: Input pipeline runs at `THREAD_PRIORITY_HIGHEST`
- **Atomic routing switch**: Lock-free target toggle via `std::atomic<RouteTarget>`
- **State flush on switch**: "All keys up" report prevents stuck keys
