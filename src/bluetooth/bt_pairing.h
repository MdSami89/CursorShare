#pragma once
// =============================================================================
// CursorShare — Bluetooth Pairing Manager
// Manages pairing lifecycle and preserves existing paired devices.
// =============================================================================

#include "../common/win_headers.h"
#include <cstdint>
#include <string>
#include <vector>

namespace CursorShare {

/// Snapshot of a paired device.
struct PairedDeviceInfo {
  BTH_ADDR address;
  std::string name;
  uint32_t classOfDevice;
  bool authenticated;
  bool connected;
  bool remembered;
};

/// Manages Bluetooth pairing and preserves the host's paired device list.
class BluetoothPairingManager {
public:
  BluetoothPairingManager() = default;
  ~BluetoothPairingManager() = default;

  /// Take a snapshot of all currently paired devices.
  /// Call on startup to establish the baseline for preservation.
  std::vector<PairedDeviceInfo> SnapshotPairedDevices();

  /// Verify that paired devices match the given snapshot.
  /// Returns true if all devices in the snapshot are still present.
  bool VerifyPairedDevices(const std::vector<PairedDeviceInfo> &snapshot);

  /// Get the current list of paired devices.
  std::vector<PairedDeviceInfo> GetPairedDevices() const;

  /// Check if a specific device is paired.
  bool IsDevicePaired(BTH_ADDR address) const;

  /// Request pairing with a device (initiates authentication).
  bool PairDevice(BTH_ADDR address, const wchar_t *pin = nullptr);

  /// Scan for nearby discoverable devices.
  struct DiscoveredDevice {
    BTH_ADDR address;
    std::string name;
    uint32_t classOfDevice;
    bool paired;
  };
  std::vector<DiscoveredDevice> ScanForDevices(int timeoutSeconds = 10);

private:
  std::vector<PairedDeviceInfo> startupSnapshot_;
};

} // namespace CursorShare
