// =============================================================================
// CursorShare — BLE HID over GATT Service (Implementation)
// Uses Windows 10+ GattServiceProvider to register as a BLE HID peripheral.
// =============================================================================

#include "ble_hid_service.h"
#include "../common/hid_descriptors.h"
#include "../common/logger.h"
#include "../common/scancode_to_hid.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;

namespace CursorShare {

// ---------------------------------------------------------------------------
// Standard Bluetooth GATT UUIDs
// ---------------------------------------------------------------------------
static const winrt::guid HID_SERVICE_UUID{
    0x00001812,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static const winrt::guid DEVICE_INFO_SERVICE_UUID{
    0x0000180A,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static const winrt::guid BATTERY_SERVICE_UUID{
    0x0000180F,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

// HID Service Characteristics
static const winrt::guid REPORT_MAP_UUID{
    0x00002A4B,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static const winrt::guid HID_INFORMATION_UUID{
    0x00002A4A,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static const winrt::guid HID_CONTROL_POINT_UUID{
    0x00002A4C,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static const winrt::guid PROTOCOL_MODE_UUID{
    0x00002A4E,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static const winrt::guid REPORT_UUID{
    0x00002A4D,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

// Report Reference Descriptor
static const winrt::guid REPORT_REFERENCE_DESCRIPTOR_UUID{
    0x00002908,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

// Device Information Characteristics
static const winrt::guid MANUFACTURER_NAME_UUID{
    0x00002A29,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static const winrt::guid MODEL_NUMBER_UUID{
    0x00002A24,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static const winrt::guid PNP_ID_UUID{
    0x00002A50,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

// Battery Characteristic
static const winrt::guid BATTERY_LEVEL_UUID{
    0x00002A19,
    0x0000,
    0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
BleHidService::BleHidService() {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
}

BleHidService::~BleHidService() { Shutdown(); }

// ---------------------------------------------------------------------------
// IsSupported
// ---------------------------------------------------------------------------
bool BleHidService::IsSupported() {
  try {
    auto adapter = BluetoothAdapter::GetDefaultAsync().get();
    if (!adapter)
      return false;
    return adapter.IsPeripheralRoleSupported();
  } catch (...) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// CreateBufferFromData
// ---------------------------------------------------------------------------
IBuffer BleHidService::CreateBufferFromData(const uint8_t *data,
                                            uint32_t size) {
  DataWriter writer;
  writer.ByteOrder(ByteOrder::LittleEndian);
  for (uint32_t i = 0; i < size; ++i) {
    writer.WriteByte(data[i]);
  }
  return writer.DetachBuffer();
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
bool BleHidService::Initialize() {
  if (state_.load(std::memory_order_acquire) != BleHidState::Stopped) {
    return false;
  }

  state_.store(BleHidState::Initializing, std::memory_order_release);

  try {
    // Check peripheral support
    if (!IsSupported()) {
      LOG_ERROR("BLE-HID",
                "Bluetooth adapter does not support peripheral role.");
      state_.store(BleHidState::Error, std::memory_order_release);
      return false;
    }

    // Create GATT services
    if (!CreateHidService()) {
      LOG_ERROR("BLE-HID", "Failed to create HID Service.");
      state_.store(BleHidState::Error, std::memory_order_release);
      return false;
    }

    if (!CreateDeviceInfoService()) {
      LOG_WARN("BLE-HID", "Failed to create Device Info Service.");
      // Non-critical, continue
    }

    if (!CreateBatteryService()) {
      LOG_WARN("BLE-HID", "Failed to create Battery Service.");
      // Non-critical, continue
    }

    LOG_INFO("BLE-HID", "GATT services initialized successfully.");
    return true;

  } catch (const winrt::hresult_error &ex) {
    LOG_ERROR_W("BLE-HID", ex.message().c_str());
    state_.store(BleHidState::Error, std::memory_order_release);
    return false;
  } catch (...) {
    LOG_ERROR("BLE-HID", "Unknown error during initialization.");
    state_.store(BleHidState::Error, std::memory_order_release);
    return false;
  }
}

// ---------------------------------------------------------------------------
// CreateHidService
// ---------------------------------------------------------------------------
bool BleHidService::CreateHidService() {
  // Create the HID GATT Service
  auto result = GattServiceProvider::CreateAsync(HID_SERVICE_UUID).get();
  if (result.Error() != BluetoothError::Success) {
    LOG_ERROR("BLE-HID", "Cannot create HID service provider: error %d",
              static_cast<int>(result.Error()));
    return false;
  }
  hidServiceProvider_ = result.ServiceProvider();
  auto service = hidServiceProvider_.Service();

  // --- Report Map Characteristic (read-only) ---
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(GattCharacteristicProperties::Read);
    params.ReadProtectionLevel(GattProtectionLevel::Plain);

    // Build combined HID descriptor (keyboard + mouse)
    std::vector<uint8_t> combinedDescriptor;
    combinedDescriptor.insert(combinedDescriptor.end(), kKeyboardHidDescriptor,
                              kKeyboardHidDescriptor +
                                  kKeyboardHidDescriptorSize);
    combinedDescriptor.insert(combinedDescriptor.end(), kMouseHidDescriptor,
                              kMouseHidDescriptor + kMouseHidDescriptorSize);

    params.StaticValue(
        CreateBufferFromData(combinedDescriptor.data(),
                             static_cast<uint32_t>(combinedDescriptor.size())));

    auto charResult =
        service.CreateCharacteristicAsync(REPORT_MAP_UUID, params).get();
    if (charResult.Error() != BluetoothError::Success) {
      LOG_ERROR("BLE-HID", "Failed to create Report Map characteristic.");
      return false;
    }
    reportMapChar_ = charResult.Characteristic();
  }

  // --- HID Information Characteristic (read-only) ---
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(GattCharacteristicProperties::Read);
    params.ReadProtectionLevel(GattProtectionLevel::Plain);

    // HID Information: bcdHID=0x0111 (HID 1.11), bCountryCode=0x00, Flags=0x02
    // (normally connectable)
    uint8_t hidInfo[] = {0x11, 0x01, 0x00, 0x02};
    params.StaticValue(CreateBufferFromData(hidInfo, sizeof(hidInfo)));

    auto charResult =
        service.CreateCharacteristicAsync(HID_INFORMATION_UUID, params).get();
    if (charResult.Error() != BluetoothError::Success) {
      LOG_ERROR("BLE-HID", "Failed to create HID Information characteristic.");
      return false;
    }
    hidInfoChar_ = charResult.Characteristic();
  }

  // --- HID Control Point Characteristic (write without response) ---
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(
        GattCharacteristicProperties::WriteWithoutResponse);
    params.WriteProtectionLevel(GattProtectionLevel::Plain);

    auto charResult =
        service.CreateCharacteristicAsync(HID_CONTROL_POINT_UUID, params).get();
    if (charResult.Error() != BluetoothError::Success) {
      LOG_ERROR("BLE-HID",
                "Failed to create HID Control Point characteristic.");
      return false;
    }
    hidControlPointChar_ = charResult.Characteristic();

    // Handle control point writes (suspend/resume)
    hidControlPointChar_.WriteRequested(
        [](GattLocalCharacteristic const &,
           GattWriteRequestedEventArgs const &args) {
          auto deferral = args.GetDeferral();
          auto request = args.GetRequestAsync().get();
          // 0x00 = Suspend, 0x01 = Exit Suspend — we accept both
          request.Respond();
          deferral.Complete();
        });
  }

  // --- Protocol Mode Characteristic (read + write without response) ---
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(
        GattCharacteristicProperties::Read |
        GattCharacteristicProperties::WriteWithoutResponse);
    params.ReadProtectionLevel(GattProtectionLevel::Plain);
    params.WriteProtectionLevel(GattProtectionLevel::Plain);

    // Default: Report Protocol Mode (0x01)
    uint8_t protocolMode = 0x01;
    params.StaticValue(CreateBufferFromData(&protocolMode, 1));

    auto charResult =
        service.CreateCharacteristicAsync(PROTOCOL_MODE_UUID, params).get();
    if (charResult.Error() != BluetoothError::Success) {
      LOG_ERROR("BLE-HID", "Failed to create Protocol Mode characteristic.");
      return false;
    }
    protocolModeChar_ = charResult.Characteristic();
  }

  // --- Keyboard Input Report Characteristic (read + notify) ---
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(GattCharacteristicProperties::Read |
                                    GattCharacteristicProperties::Notify);
    params.ReadProtectionLevel(GattProtectionLevel::Plain);

    // Report Reference Descriptor: Report ID=0x01, Report Type=Input(0x01)
    GattLocalDescriptorParameters descParams;
    uint8_t reportRef[] = {0x01, 0x01}; // Report ID 1, Input
    descParams.StaticValue(CreateBufferFromData(reportRef, sizeof(reportRef)));
    descParams.ReadProtectionLevel(GattProtectionLevel::Plain);

    auto charResult =
        service.CreateCharacteristicAsync(REPORT_UUID, params).get();
    if (charResult.Error() != BluetoothError::Success) {
      LOG_ERROR("BLE-HID", "Failed to create Keyboard Report characteristic.");
      return false;
    }
    keyboardReportChar_ = charResult.Characteristic();

    // Add Report Reference descriptor
    keyboardReportChar_
        .CreateDescriptorAsync(REPORT_REFERENCE_DESCRIPTOR_UUID, descParams)
        .get();

    // Track subscribers
    keyboardSubscribedToken_ = keyboardReportChar_.SubscribedClientsChanged(
        [this](GattLocalCharacteristic const &sender, auto const &) {
          auto clients = sender.SubscribedClients();
          std::lock_guard<std::mutex> lock(subscribersMutex_);
          subscribers_.clear();
          for (auto const &client : clients) {
            subscribers_.push_back(client);
          }
          int count = static_cast<int>(subscribers_.size());
          if (count > 0) {
            state_.store(BleHidState::Connected, std::memory_order_release);
            LOG_INFO("BLE-HID", "%d client(s) subscribed to keyboard reports.",
                     count);
          } else {
            state_.store(BleHidState::Advertising, std::memory_order_release);
            LOG_INFO("BLE-HID", "No clients subscribed.");
          }
          if (deviceCallback_) {
            deviceCallback_("BLE Client", count > 0);
          }
        });
  }

  // --- Mouse Input Report Characteristic (read + notify) ---
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(GattCharacteristicProperties::Read |
                                    GattCharacteristicProperties::Notify);
    params.ReadProtectionLevel(GattProtectionLevel::Plain);

    // Report Reference Descriptor: Report ID=0x02, Report Type=Input(0x01)
    GattLocalDescriptorParameters descParams;
    uint8_t reportRef[] = {0x02, 0x01}; // Report ID 2, Input
    descParams.StaticValue(CreateBufferFromData(reportRef, sizeof(reportRef)));
    descParams.ReadProtectionLevel(GattProtectionLevel::Plain);

    auto charResult =
        service.CreateCharacteristicAsync(REPORT_UUID, params).get();
    if (charResult.Error() != BluetoothError::Success) {
      LOG_ERROR("BLE-HID", "Failed to create Mouse Report characteristic.");
      return false;
    }
    mouseReportChar_ = charResult.Characteristic();

    // Add Report Reference descriptor
    mouseReportChar_
        .CreateDescriptorAsync(REPORT_REFERENCE_DESCRIPTOR_UUID, descParams)
        .get();

    // Track mouse subscribers too
    mouseSubscribedToken_ = mouseReportChar_.SubscribedClientsChanged(
        [this](GattLocalCharacteristic const &sender, auto const &) {
          auto clients = sender.SubscribedClients();
          LOG_INFO("BLE-HID", "%u client(s) subscribed to mouse reports.",
                   clients.Size());
        });
  }

  return true;
}

// ---------------------------------------------------------------------------
// CreateDeviceInfoService
// ---------------------------------------------------------------------------
bool BleHidService::CreateDeviceInfoService() {
  auto result =
      GattServiceProvider::CreateAsync(DEVICE_INFO_SERVICE_UUID).get();
  if (result.Error() != BluetoothError::Success) {
    return false;
  }
  deviceInfoProvider_ = result.ServiceProvider();
  auto service = deviceInfoProvider_.Service();

  // Manufacturer Name
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(GattCharacteristicProperties::Read);
    params.ReadProtectionLevel(GattProtectionLevel::Plain);

    const char *name = "CursorShare";
    params.StaticValue(
        CreateBufferFromData(reinterpret_cast<const uint8_t *>(name),
                             static_cast<uint32_t>(strlen(name))));

    service.CreateCharacteristicAsync(MANUFACTURER_NAME_UUID, params).get();
  }

  // Model Number
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(GattCharacteristicProperties::Read);
    params.ReadProtectionLevel(GattProtectionLevel::Plain);

    const char *model = "CS-HID-1";
    params.StaticValue(
        CreateBufferFromData(reinterpret_cast<const uint8_t *>(model),
                             static_cast<uint32_t>(strlen(model))));

    service.CreateCharacteristicAsync(MODEL_NUMBER_UUID, params).get();
  }

  // PnP ID
  {
    GattLocalCharacteristicParameters params;
    params.CharacteristicProperties(GattCharacteristicProperties::Read);
    params.ReadProtectionLevel(GattProtectionLevel::Plain);

    // PnP ID: Vendor Source=0x02(USB), Vendor ID=0x0000, Product ID=0x0001,
    // Version=0x0100
    uint8_t pnpId[] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01};
    params.StaticValue(CreateBufferFromData(pnpId, sizeof(pnpId)));

    service.CreateCharacteristicAsync(PNP_ID_UUID, params).get();
  }

  return true;
}

// ---------------------------------------------------------------------------
// CreateBatteryService
// ---------------------------------------------------------------------------
bool BleHidService::CreateBatteryService() {
  auto result = GattServiceProvider::CreateAsync(BATTERY_SERVICE_UUID).get();
  if (result.Error() != BluetoothError::Success) {
    return false;
  }
  batteryProvider_ = result.ServiceProvider();
  auto service = batteryProvider_.Service();

  // Battery Level (always report 100%)
  GattLocalCharacteristicParameters params;
  params.CharacteristicProperties(GattCharacteristicProperties::Read |
                                  GattCharacteristicProperties::Notify);
  params.ReadProtectionLevel(GattProtectionLevel::Plain);

  uint8_t level = 100;
  params.StaticValue(CreateBufferFromData(&level, 1));

  auto charResult =
      service.CreateCharacteristicAsync(BATTERY_LEVEL_UUID, params).get();
  if (charResult.Error() != BluetoothError::Success) {
    return false;
  }
  batteryLevelChar_ = charResult.Characteristic();

  return true;
}

// ---------------------------------------------------------------------------
// StartAdvertising
// ---------------------------------------------------------------------------
bool BleHidService::StartAdvertising() {
  try {
    // Advertising parameters — include HID appearance
    GattServiceProviderAdvertisingParameters advParams;
    advParams.IsConnectable(true);
    advParams.IsDiscoverable(true);

    // Start advertising HID service
    hidServiceProvider_.StartAdvertising(advParams);

    // Start Device Info and Battery (non-advertising, just published)
    if (deviceInfoProvider_) {
      GattServiceProviderAdvertisingParameters diParams;
      diParams.IsConnectable(false);
      diParams.IsDiscoverable(false);
      deviceInfoProvider_.StartAdvertising(diParams);
    }
    if (batteryProvider_) {
      GattServiceProviderAdvertisingParameters batParams;
      batParams.IsConnectable(false);
      batParams.IsDiscoverable(false);
      batteryProvider_.StartAdvertising(batParams);
    }

    state_.store(BleHidState::Advertising, std::memory_order_release);
    LOG_INFO("BLE-HID", "Advertising as BLE HID device 'CursorShare'...");
    return true;

  } catch (const winrt::hresult_error &ex) {
    LOG_ERROR_W("BLE-HID", ex.message().c_str());
    state_.store(BleHidState::Error, std::memory_order_release);
    return false;
  }
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void BleHidService::Shutdown() {
  if (state_.load(std::memory_order_acquire) == BleHidState::Stopped) {
    return;
  }

  try {
    // Stop advertising
    if (hidServiceProvider_) {
      hidServiceProvider_.StopAdvertising();
    }
    if (deviceInfoProvider_) {
      deviceInfoProvider_.StopAdvertising();
    }
    if (batteryProvider_) {
      batteryProvider_.StopAdvertising();
    }

    // Clear subscribers
    {
      std::lock_guard<std::mutex> lock(subscribersMutex_);
      subscribers_.clear();
    }

    // Reset characteristics
    keyboardReportChar_ = nullptr;
    mouseReportChar_ = nullptr;
    reportMapChar_ = nullptr;
    hidInfoChar_ = nullptr;
    hidControlPointChar_ = nullptr;
    protocolModeChar_ = nullptr;
    batteryLevelChar_ = nullptr;

    // Reset providers
    hidServiceProvider_ = nullptr;
    deviceInfoProvider_ = nullptr;
    batteryProvider_ = nullptr;

  } catch (...) {
    // Best-effort cleanup
  }

  LOG_INFO("BLE-HID", "Shutdown complete.");
}

// ---------------------------------------------------------------------------
// NotifySubscribers
// ---------------------------------------------------------------------------
void BleHidService::NotifySubscribers(
    GattLocalCharacteristic const &characteristic, const uint8_t *data,
    uint32_t size) {
  try {
    auto buffer = CreateBufferFromData(data, size);
    auto results = characteristic.NotifyValueAsync(buffer).get();
    for (auto const &result : results) {
      if (result.Status() != GattCommunicationStatus::Success) {
        LOG_WARN("BLE-HID", "Notify failed for a client: status %d",
                 static_cast<int>(result.Status()));
      }
    }
  } catch (const winrt::hresult_error &ex) {
    LOG_ERROR_W("BLE-HID", ex.message().c_str());
  }
}

// ---------------------------------------------------------------------------
// SendKeyboardReport
// ---------------------------------------------------------------------------
bool BleHidService::SendKeyboardReport(uint8_t modifiers,
                                       const uint8_t keys[6]) {
  if (state_.load(std::memory_order_acquire) != BleHidState::Connected) {
    return false;
  }

  if (!keyboardReportChar_)
    return false;

  // Keyboard report: modifiers(1) + reserved(1) + keys(6) = 8 bytes
  uint8_t report[8];
  EncodeKeyboardReport(modifiers, keys, report);

  NotifySubscribers(keyboardReportChar_, report, sizeof(report));
  return true;
}

// ---------------------------------------------------------------------------
// SendMouseReport
// ---------------------------------------------------------------------------
bool BleHidService::SendMouseReport(uint8_t buttons, int16_t dx, int16_t dy,
                                    int8_t wheel, int8_t hwheel) {
  if (state_.load(std::memory_order_acquire) != BleHidState::Connected) {
    return false;
  }

  if (!mouseReportChar_)
    return false;

  // Mouse report: buttons(1) + dx(2) + dy(2) + wheel(1) + hwheel(1) = 7 bytes
  uint8_t report[7];
  EncodeMouseReport(buttons, dx, dy, wheel, hwheel, report);

  NotifySubscribers(mouseReportChar_, report, sizeof(report));
  return true;
}

// ---------------------------------------------------------------------------
// SendKeyboardAllUp
// ---------------------------------------------------------------------------
bool BleHidService::SendKeyboardAllUp() {
  uint8_t keys[6] = {};
  return SendKeyboardReport(0, keys);
}

// ---------------------------------------------------------------------------
// SendMouseIdle
// ---------------------------------------------------------------------------
bool BleHidService::SendMouseIdle() { return SendMouseReport(0, 0, 0, 0, 0); }

// ---------------------------------------------------------------------------
// SendInputEvent
// ---------------------------------------------------------------------------
bool BleHidService::SendInputEvent(const InputEvent &event) {
  switch (event.type) {
  case InputEventType::KeyDown:
  case InputEventType::KeyUp: {
    // Convert Windows scan code → HID usage code
    uint8_t hidCode =
        ScanCodeToHid(event.data.keyboard.scanCode, event.data.keyboard.flags);

    if (hidCode == 0) {
      // Unmapped scan code — skip
      return false;
    }

    if (event.type == InputEventType::KeyDown) {
      if (IsHidModifier(hidCode)) {
        // Set modifier bit (HID spec: E0=LCtrl bit0 .. E7=RGUI bit7)
        currentModifiers_ |= HidModifierBit(hidCode);
      } else {
        // Add to 6KRO key array
        for (int i = 0; i < 6; ++i) {
          if (currentKeys_[i] == hidCode)
            break; // Already tracked
          if (currentKeys_[i] == 0) {
            currentKeys_[i] = hidCode;
            break;
          }
        }
      }
    } else { // KeyUp
      if (IsHidModifier(hidCode)) {
        currentModifiers_ &= ~HidModifierBit(hidCode);
      } else {
        for (int i = 0; i < 6; ++i) {
          if (currentKeys_[i] == hidCode) {
            currentKeys_[i] = 0;
            break;
          }
        }
      }
    }
    return SendKeyboardReport(currentModifiers_, currentKeys_);
  }

  case InputEventType::MouseMove:
    return SendMouseReport(0, event.data.mouse.dx, event.data.mouse.dy, 0, 0);

  case InputEventType::MouseButtonDown:
  case InputEventType::MouseButtonUp: {
    uint8_t btns = event.data.mouse.buttons;
    return SendMouseReport(btns, 0, 0, 0, 0);
  }

  case InputEventType::MouseWheel:
    return SendMouseReport(
        0, 0, 0, static_cast<int8_t>(event.data.mouse.wheelDelta / 120), 0);

  case InputEventType::MouseHWheel:
    return SendMouseReport(
        0, 0, 0, 0, static_cast<int8_t>(event.data.mouse.hWheelDelta / 120));

  default:
    return false;
  }
}

// ---------------------------------------------------------------------------
// GetConnectionCount
// ---------------------------------------------------------------------------
int BleHidService::GetConnectionCount() const {
  std::lock_guard<std::mutex> lock(subscribersMutex_);
  return static_cast<int>(subscribers_.size());
}

} // namespace CursorShare
