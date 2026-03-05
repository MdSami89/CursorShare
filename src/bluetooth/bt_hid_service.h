#pragma once
// =============================================================================
// CursorShare — Bluetooth HID Service
// Manages Bluetooth HID device registration, SDP, and report transmission.
// =============================================================================

#include "../common/hid_descriptors.h"
#include "../common/input_event.h"
#include "../common/win_headers.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace CursorShare {

/// Connected client device info.
struct ConnectedDevice {
  BTH_ADDR address;
  std::string name;
  SOCKET controlSocket;   // L2CAP control channel
  SOCKET interruptSocket; // L2CAP interrupt channel
  bool authenticated;
  uint64_t connectedAt; // QPC timestamp
};

/// Bluetooth HID Service state.
enum class BtHidState : uint8_t {
  Stopped = 0,
  Initializing = 1,
  Listening = 2,
  Connected = 3,
  Error = 4,
};

/// Callback for device connect/disconnect events.
using DeviceEventCallback =
    std::function<void(const ConnectedDevice &, bool connected)>;

/// Bluetooth HID Service — manages Bluetooth Classic HID device emulation.
class BluetoothHidService {
public:
  BluetoothHidService();
  ~BluetoothHidService();

  BluetoothHidService(const BluetoothHidService &) = delete;
  BluetoothHidService &operator=(const BluetoothHidService &) = delete;

  /// Initialize the service (Winsock, SDP registration).
  bool Initialize();

  /// Start listening for incoming connections.
  bool StartListening();

  /// Stop all connections and cleanup.
  void Shutdown();

  /// Send a keyboard HID report to connected client(s).
  bool SendKeyboardReport(uint8_t modifiers, const uint8_t keys[6]);

  /// Send a mouse HID report to connected client(s).
  bool SendMouseReport(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel,
                       int8_t hwheel);

  /// Send an "all keys up" report.
  bool SendKeyboardAllUp();

  /// Send an idle mouse report.
  bool SendMouseIdle();

  /// Process an InputEvent and send appropriate HID report.
  bool SendInputEvent(const InputEvent &event);

  /// Get current state.
  BtHidState GetState() const { return state_.load(std::memory_order_acquire); }

  /// Get connected device list.
  std::vector<ConnectedDevice> GetConnectedDevices() const;

  /// Set device event callback.
  void SetDeviceCallback(DeviceEventCallback callback) {
    deviceCallback_ = std::move(callback);
  }

  /// Enable/disable discoverability.
  bool SetDiscoverable(bool discoverable);

  /// Get number of connected clients.
  int GetConnectionCount() const;

private:
  bool InitWinsock();
  bool RegisterSdpRecord();
  void UnregisterSdpRecord();
  bool CreateListeningSockets();
  void AcceptLoop();

  bool SendReport(SOCKET sock, const uint8_t *data, int len);

  std::atomic<BtHidState> state_{BtHidState::Stopped};
  DeviceEventCallback deviceCallback_;

  // Winsock state
  bool winsockInitialized_ = false;

  // Listening sockets
  SOCKET controlListenSocket_ = INVALID_SOCKET;
  SOCKET interruptListenSocket_ = INVALID_SOCKET;

  // SDP record handle
  ULONG sdpRecordHandle_ = 0;

  // Connected devices
  mutable std::mutex devicesMutex_;
  std::vector<ConnectedDevice> connectedDevices_;

  // Pre-allocated report buffers (no heap alloc during send)
  uint8_t keyboardReportBuf_[9]; // Report ID + 8 bytes
  uint8_t mouseReportBuf_[8];    // Report ID + 7 bytes

  // Keyboard state tracking for HID reports
  uint8_t currentModifiers_ = 0;
  uint8_t currentKeys_[6] = {};

  // Accept thread
  std::thread acceptThread_;
  std::atomic<bool> shouldStop_{false};

  // Original Bluetooth state (for restoration on shutdown)
  BOOL originalDiscoverable_ = FALSE;
  BOOL originalConnectable_ = FALSE;
  HANDLE savedRadioHandle_ = nullptr;
};

} // namespace CursorShare
