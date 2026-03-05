#pragma once
// =============================================================================
// CursorShare — BLE HID over GATT Service
// Uses Windows 10+ GattServiceProvider to register as a BLE HID peripheral.
// Requires Windows 10 build 19044+ (HID Service restriction lifted).
// =============================================================================

// clang-format off
// C++/WinRT requires specific include order
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Storage.Streams.h>
// clang-format on

#include "../common/input_event.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace CursorShare {

namespace winrt_gatt =
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
namespace winrt_bt = winrt::Windows::Devices::Bluetooth;
namespace winrt_ble_adv = winrt::Windows::Devices::Bluetooth::Advertisement;
namespace winrt_streams = winrt::Windows::Storage::Streams;

/// BLE connection info.
struct BleConnectedDevice {
  std::string deviceId;
  winrt_gatt::GattSession session{nullptr};
  bool subscribed = false;
};

/// BLE HID Service state.
enum class BleHidState : uint8_t {
  Stopped = 0,
  Initializing = 1,
  Advertising = 2,
  Connected = 3,
  Error = 4,
};

/// Callback for BLE device events.
using BleDeviceEventCallback =
    std::function<void(const std::string &deviceId, bool connected)>;

/// BLE HID over GATT Service.
/// Creates a GATT server with HID Service (0x1812), Device Information
/// (0x180A), and Battery Service (0x180F).
class BleHidService {
public:
  BleHidService();
  ~BleHidService();

  BleHidService(const BleHidService &) = delete;
  BleHidService &operator=(const BleHidService &) = delete;

  /// Initialize GATT services and characteristics.
  bool Initialize();

  /// Start BLE advertising as an HID device.
  bool StartAdvertising();

  /// Stop advertising and disconnect all clients.
  void Shutdown();

  /// Send keyboard HID report to subscribed clients.
  bool SendKeyboardReport(uint8_t modifiers, const uint8_t keys[6]);

  /// Send mouse HID report to subscribed clients.
  bool SendMouseReport(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel,
                       int8_t hwheel);

  /// Send all-keys-up report.
  bool SendKeyboardAllUp();

  /// Send idle mouse report.
  bool SendMouseIdle();

  /// Process an InputEvent and send appropriate HID report.
  bool SendInputEvent(const InputEvent &event);

  /// Get current state.
  BleHidState GetState() const {
    return state_.load(std::memory_order_acquire);
  }

  /// Set device event callback.
  void SetDeviceCallback(BleDeviceEventCallback callback) {
    deviceCallback_ = std::move(callback);
  }

  /// Get number of subscribed clients.
  int GetConnectionCount() const;

  /// Check if BLE HID is supported on this system.
  static bool IsSupported();

private:
  // GATT service setup
  bool CreateHidService();
  bool CreateDeviceInfoService();
  bool CreateBatteryService();

  // Helpers
  winrt_streams::IBuffer CreateBufferFromData(const uint8_t *data,
                                              uint32_t size);
  void
  NotifySubscribers(winrt_gatt::GattLocalCharacteristic const &characteristic,
                    const uint8_t *data, uint32_t size);

  std::atomic<BleHidState> state_{BleHidState::Stopped};
  BleDeviceEventCallback deviceCallback_;

  // GATT Service Providers
  winrt_gatt::GattServiceProvider hidServiceProvider_{nullptr};
  winrt_gatt::GattServiceProvider deviceInfoProvider_{nullptr};
  winrt_gatt::GattServiceProvider batteryProvider_{nullptr};

  // HID Characteristics (for sending reports)
  winrt_gatt::GattLocalCharacteristic keyboardReportChar_{nullptr};
  winrt_gatt::GattLocalCharacteristic mouseReportChar_{nullptr};
  winrt_gatt::GattLocalCharacteristic reportMapChar_{nullptr};
  winrt_gatt::GattLocalCharacteristic hidInfoChar_{nullptr};
  winrt_gatt::GattLocalCharacteristic hidControlPointChar_{nullptr};
  winrt_gatt::GattLocalCharacteristic protocolModeChar_{nullptr};

  // Battery characteristic
  winrt_gatt::GattLocalCharacteristic batteryLevelChar_{nullptr};

  // Subscribers tracking
  mutable std::mutex subscribersMutex_;
  std::vector<winrt_gatt::GattSubscribedClient> subscribers_;

  // Keyboard state tracking
  uint8_t currentModifiers_ = 0;
  uint8_t currentKeys_[6] = {};

  // WinRT event tokens for cleanup
  winrt::event_token keyboardSubscribedToken_;
  winrt::event_token mouseSubscribedToken_;

  // BLE name advertisement publisher
  winrt_ble_adv::BluetoothLEAdvertisementPublisher namePublisher_{nullptr};
};

} // namespace CursorShare
