#pragma once
// =============================================================================
// CursorShare — IPC Protocol Definitions
// Shared memory layout between kernel-mode filter drivers and user-mode service.
// =============================================================================

#include <cstdint>
#include <cstddef>

namespace CursorShare {

// ---------------------------------------------------------------------------
// Shared Memory Layout
//
// The shared memory section is organized as:
//   [SharedMemoryHeader]  (64 bytes, cache-line aligned)
//   [Ring buffer data]    (Capacity * sizeof(InputEvent) bytes)
//
// The header contains atomic indices and control flags.
// ---------------------------------------------------------------------------

/// Control flags for the shared memory channel.
enum class ChannelFlags : uint32_t {
    None            = 0x00000000,
    Active          = 0x00000001,  // Channel is active
    ExclusiveMode   = 0x00000002,  // Suppress local input
    ClientConnected = 0x00000004,  // A BT client is connected
    DriverReady     = 0x00000008,  // Kernel driver is initialized
    ServiceReady    = 0x00000010,  // User-mode service is initialized
};

inline ChannelFlags operator|(ChannelFlags a, ChannelFlags b) {
    return static_cast<ChannelFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline ChannelFlags operator&(ChannelFlags a, ChannelFlags b) {
    return static_cast<ChannelFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(ChannelFlags flags, ChannelFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

/// Shared memory header — sits at offset 0 of the shared section.
/// Must be exactly 64 bytes (one cache line).
struct alignas(64) SharedMemoryHeader {
    // Version and magic number for validation
    uint32_t magic;           // Must be kSharedMemMagic
    uint32_t version;         // Protocol version
    uint32_t headerSize;      // sizeof(SharedMemoryHeader)
    uint32_t eventSize;       // sizeof(InputEvent)

    // Ring buffer indices (written by kernel, read by user-mode)
    volatile uint32_t writeIndex;   // Producer (driver) write position
    volatile uint32_t readIndex;    // Consumer (service) read position
    uint32_t capacity;              // Number of event slots
    uint32_t reserved1;

    // Control flags
    volatile uint32_t flags;        // ChannelFlags bitmask
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;

    // Sequence counter for diagnostics
    volatile uint64_t totalEventsWritten;
    volatile uint64_t totalEventsRead;
};
static_assert(sizeof(SharedMemoryHeader) == 64,
              "SharedMemoryHeader must be exactly 64 bytes");

// Magic number: "CSHR" in little-endian
constexpr uint32_t kSharedMemMagic = 0x52485343;  // 'C','S','H','R'

// Protocol version
constexpr uint32_t kSharedMemVersion = 1;

// ---------------------------------------------------------------------------
// Named Pipe Protocol (UI <-> Service)
// Simple request/response message protocol.
// ---------------------------------------------------------------------------

enum class PipeMessageType : uint32_t {
    // Requests (UI -> Service)
    GetStatus          = 0x0001,
    StartBroadcast     = 0x0002,
    StopBroadcast      = 0x0003,
    SetShortcut        = 0x0004,
    SwitchTarget       = 0x0005,
    GetDeviceList      = 0x0006,
    PairDevice         = 0x0007,
    UnpairDevice       = 0x0008,
    GetLatencyStats    = 0x0009,
    GetDiagnostics     = 0x000A,
    SetExclusiveMode   = 0x000B,
    SetClientResolution = 0x000C,

    // Responses (Service -> UI)
    StatusResponse     = 0x8001,
    DeviceListResponse = 0x8006,
    LatencyResponse    = 0x8009,
    DiagnosticsResponse = 0x800A,
    AckResponse        = 0x8FFF,
    ErrorResponse      = 0xFFFF,
};

/// Fixed-size pipe message header.
struct PipeMessageHeader {
    PipeMessageType type;
    uint32_t        payloadSize;  // Size of payload following this header
    uint32_t        requestId;    // For matching responses to requests
    uint32_t        reserved;
};
static_assert(sizeof(PipeMessageHeader) == 16, "PipeMessageHeader must be 16 bytes");

// Maximum pipe message payload size
constexpr size_t kMaxPipePayload = 4096;

// ---------------------------------------------------------------------------
// Status Payload (Service -> UI)
// ---------------------------------------------------------------------------
enum class BroadcastState : uint8_t {
    Stopped    = 0,
    Starting   = 1,
    Running    = 2,
    Stopping   = 3,
    Error      = 4,
};

enum class TargetMode : uint8_t {
    Host   = 0,
    Client = 1,
};

enum class InputMode : uint8_t {
    RawInput     = 0,   // User-mode Raw Input API
    KernelDriver = 1,   // KMDF filter driver
};

struct StatusPayload {
    BroadcastState broadcastState;
    TargetMode     targetMode;
    InputMode      inputMode;
    uint8_t        connectedDeviceCount;
    uint8_t        btAdapterPresent;
    uint8_t        btAdapterEnabled;
    uint8_t        driverInstalled;
    uint8_t        reserved;
};
static_assert(sizeof(StatusPayload) == 8, "StatusPayload must be 8 bytes");

}  // namespace CursorShare
