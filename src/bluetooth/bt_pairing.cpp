// =============================================================================
// CursorShare — Bluetooth Pairing Manager (Implementation)
// =============================================================================

#include "bt_pairing.h"
#include <algorithm>

namespace CursorShare {

static std::string WideToUtf8(const wchar_t *wide) {
  if (!wide || !wide[0])
    return "";
  int len =
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0)
    return "";
  std::string result(len - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], len, nullptr, nullptr);
  return result;
}

// ---------------------------------------------------------------------------
// SnapshotPairedDevices
// ---------------------------------------------------------------------------
std::vector<PairedDeviceInfo> BluetoothPairingManager::SnapshotPairedDevices() {
  startupSnapshot_ = GetPairedDevices();
  return startupSnapshot_;
}

// ---------------------------------------------------------------------------
// VerifyPairedDevices
// ---------------------------------------------------------------------------
bool BluetoothPairingManager::VerifyPairedDevices(
    const std::vector<PairedDeviceInfo> &snapshot) {
  auto current = GetPairedDevices();

  for (const auto &saved : snapshot) {
    bool found = false;
    for (const auto &dev : current) {
      if (dev.address == saved.address) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false; // A previously paired device is missing
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// GetPairedDevices
// ---------------------------------------------------------------------------
std::vector<PairedDeviceInfo>
BluetoothPairingManager::GetPairedDevices() const {
  std::vector<PairedDeviceInfo> devices;

  BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = {};
  searchParams.dwSize = sizeof(searchParams);
  searchParams.fReturnAuthenticated = TRUE;
  searchParams.fReturnRemembered = TRUE;
  searchParams.fReturnConnected = TRUE;
  searchParams.fReturnUnknown = FALSE;
  searchParams.fIssueInquiry = FALSE; // Don't do active scan
  searchParams.cTimeoutMultiplier = 0;

  // Use first radio
  BLUETOOTH_FIND_RADIO_PARAMS radioParams;
  radioParams.dwSize = sizeof(radioParams);
  HANDLE hRadio = nullptr;
  HBLUETOOTH_RADIO_FIND hRadioFind =
      BluetoothFindFirstRadio(&radioParams, &hRadio);
  if (hRadioFind) {
    searchParams.hRadio = hRadio;
    BluetoothFindRadioClose(hRadioFind);
  }

  BLUETOOTH_DEVICE_INFO devInfo = {};
  devInfo.dwSize = sizeof(devInfo);

  HBLUETOOTH_DEVICE_FIND hDevFind =
      BluetoothFindFirstDevice(&searchParams, &devInfo);
  if (hDevFind) {
    do {
      PairedDeviceInfo info;
      info.address = devInfo.Address.ullLong;
      info.name = WideToUtf8(devInfo.szName);
      info.classOfDevice = devInfo.ulClassofDevice;
      info.authenticated = devInfo.fAuthenticated != FALSE;
      info.connected = devInfo.fConnected != FALSE;
      info.remembered = devInfo.fRemembered != FALSE;
      devices.push_back(info);
    } while (BluetoothFindNextDevice(hDevFind, &devInfo));

    BluetoothFindDeviceClose(hDevFind);
  }

  if (hRadio)
    CloseHandle(hRadio);
  return devices;
}

// ---------------------------------------------------------------------------
// IsDevicePaired
// ---------------------------------------------------------------------------
bool BluetoothPairingManager::IsDevicePaired(BTH_ADDR address) const {
  BLUETOOTH_DEVICE_INFO devInfo = {};
  devInfo.dwSize = sizeof(devInfo);
  devInfo.Address.ullLong = address;

  DWORD result = BluetoothGetDeviceInfo(nullptr, &devInfo);
  if (result != ERROR_SUCCESS)
    return false;

  return devInfo.fAuthenticated || devInfo.fRemembered;
}

// ---------------------------------------------------------------------------
// PairDevice
// ---------------------------------------------------------------------------
bool BluetoothPairingManager::PairDevice(BTH_ADDR address, const wchar_t *pin) {
  BLUETOOTH_DEVICE_INFO devInfo = {};
  devInfo.dwSize = sizeof(devInfo);
  devInfo.Address.ullLong = address;

  DWORD result = BluetoothGetDeviceInfo(nullptr, &devInfo);
  if (result != ERROR_SUCCESS)
    return false;

  // Authenticate
  DWORD authResult;
  if (pin) {
    authResult = BluetoothAuthenticateDevice(nullptr, nullptr, &devInfo,
                                             const_cast<PWSTR>(pin),
                                             static_cast<ULONG>(wcslen(pin)));
  } else {
    // Use Secure Simple Pairing (no PIN)
    authResult = BluetoothAuthenticateDeviceEx(
        nullptr, nullptr, &devInfo, nullptr, MITMProtectionNotRequired);
  }

  return authResult == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// ScanForDevices
// ---------------------------------------------------------------------------
std::vector<BluetoothPairingManager::DiscoveredDevice>
BluetoothPairingManager::ScanForDevices(int timeoutSeconds) {
  std::vector<DiscoveredDevice> devices;

  BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = {};
  searchParams.dwSize = sizeof(searchParams);
  searchParams.fReturnAuthenticated = TRUE;
  searchParams.fReturnRemembered = TRUE;
  searchParams.fReturnConnected = TRUE;
  searchParams.fReturnUnknown = TRUE;
  searchParams.fIssueInquiry = TRUE; // Active discovery
  searchParams.cTimeoutMultiplier = static_cast<UCHAR>(
      std::clamp(timeoutSeconds * 100 / 128, 1, 48)); // Convert to 1.28s units

  BLUETOOTH_DEVICE_INFO devInfo = {};
  devInfo.dwSize = sizeof(devInfo);

  HBLUETOOTH_DEVICE_FIND hFind =
      BluetoothFindFirstDevice(&searchParams, &devInfo);
  if (hFind) {
    do {
      DiscoveredDevice dev;
      dev.address = devInfo.Address.ullLong;
      dev.name = WideToUtf8(devInfo.szName);
      dev.classOfDevice = devInfo.ulClassofDevice;
      dev.paired = devInfo.fAuthenticated || devInfo.fRemembered;
      devices.push_back(dev);
    } while (BluetoothFindNextDevice(hFind, &devInfo));

    BluetoothFindDeviceClose(hFind);
  }

  return devices;
}

} // namespace CursorShare
