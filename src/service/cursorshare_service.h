#pragma once
// =============================================================================
// CursorShare — Main Service
// Orchestrates all components: input capture, routing, Bluetooth HID, etc.
// =============================================================================

#include "../bluetooth/ble_hid_service.h"
#include "../bluetooth/bt_hid_service.h"
#include "../bluetooth/bt_pairing.h"
#include "../bluetooth/bt_validator.h"
#include "../common/input_event.h"
#include "../common/ipc_protocol.h"
#include "../input/input_router.h"
#include "../input/mouse_boundary.h"
#include "../input/raw_input_capture.h"
#include "../input/shortcut_manager.h"
#include "latency_monitor.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace CursorShare {

/// Service configuration.
struct ServiceConfig {
  InputMode inputMode = InputMode::RawInput;
  bool exclusiveMode = false;
  ShortcutConfig shortcut;
  ClientDisplay defaultDisplay;
};

/// Main CursorShare service — ties everything together.
class CursorShareService {
public:
  CursorShareService();
  ~CursorShareService();

  CursorShareService(const CursorShareService &) = delete;
  CursorShareService &operator=(const CursorShareService &) = delete;

  /// Initialize the service with configuration.
  bool Initialize(const ServiceConfig &config = {});

  /// Start broadcasting (enables BT and input capture).
  bool StartBroadcast();

  /// Start BLE HID broadcasting (BLE GATT peripheral).
  bool StartBleBroadcast();

  /// Stop broadcasting (disables BT, releases input).
  void StopBroadcast();

  /// Full shutdown — restores all system state.
  void Shutdown();

  /// Switch input target.
  void SwitchToHost() { router_.SwitchToHost(); }
  void SwitchToClient() { router_.SwitchToClient(); }
  void ToggleTarget() { router_.Toggle(); }

  /// Get status.
  StatusPayload GetStatus() const;

  /// Get Bluetooth diagnostics.
  BluetoothValidationResult GetDiagnostics() const { return lastDiagnostics_; }

  /// Get latency statistics.
  LatencyStats GetLatencyStats(PipelineStage stage) const {
    return latencyMonitor_.GetStats(stage);
  }

  /// Get connected devices.
  std::vector<ConnectedDevice> GetConnectedDevices() const {
    return btService_.GetConnectedDevices();
  }

  /// Get BLE connection count.
  int GetBleConnectionCount() const { return bleService_.GetConnectionCount(); }

  /// Get BLE state.
  BleHidState GetBleState() const { return bleService_.GetState(); }

  /// Get paired devices.
  std::vector<PairedDeviceInfo> GetPairedDevices() const {
    return pairingManager_.GetPairedDevices();
  }

  /// Set client display resolution.
  void SetClientDisplay(const ClientDisplay &display) {
    mouseBoundary_.SetClientDisplay(display);
  }

  /// Set exclusive mode.
  void SetExclusiveMode(bool exclusive);

private:
  /// Input event pipeline — routes events from capture to BT.
  void OnInputEvent(const InputEvent &event);

  /// Called when client output callback fires.
  void OnClientOutput(const InputEvent &event);

  /// Handle device connect/disconnect.
  void OnDeviceEvent(const ConnectedDevice &device, bool connected);

  /// Named pipe server thread (for UI communication).
  void PipeServerThread();

  /// Process pipeline stage for event forwarding.
  void ProcessInputPipeline();

  // Components
  RawInputCapture rawInput_;
  InputRouter router_;
  ShortcutManager shortcutManager_;
  MouseBoundary mouseBoundary_;
  BluetoothHidService btService_;
  BleHidService bleService_;
  BluetoothPairingManager pairingManager_;
  LatencyMonitor latencyMonitor_;

  // Configuration
  ServiceConfig config_;
  BluetoothValidationResult lastDiagnostics_;

  // State
  std::atomic<BroadcastState> broadcastState_{BroadcastState::Stopped};
  std::vector<PairedDeviceInfo> startupPairedDevices_;

  // Pipe server
  std::thread pipeThread_;
  std::atomic<bool> shouldStopPipe_{false};
};

} // namespace CursorShare
