// =============================================================================
// CursorShare — Bluetooth Capability Validator (Implementation)
// =============================================================================

#include "bt_validator.h"
#include "../common/win_headers.h"

#include <bthsdpdef.h>
#include <devguid.h>
#include <iomanip>
#include <setupapi.h>
#include <sstream>


#pragma comment(lib, "SetupAPI.lib")

namespace CursorShare {

// ---------------------------------------------------------------------------
// BluetoothValidationResult::GetSummary
// ---------------------------------------------------------------------------
std::string BluetoothValidationResult::GetSummary() const {
  std::ostringstream ss;
  ss << "=== CursorShare Bluetooth Diagnostics ===" << std::endl;
  ss << "Overall: " << (allPassed ? "PASS" : "FAIL") << std::endl;

  if (!adapterName.empty()) {
    ss << "Adapter: " << adapterName << std::endl;
    ss << "Address: " << adapterAddress << std::endl;
    ss << "Class:   0x" << std::hex << std::setfill('0') << std::setw(6)
       << adapterClass << std::dec << std::endl;
  }

  ss << std::endl;
  for (const auto &check : checks) {
    ss << (check.passed ? "[PASS]" : "[FAIL]") << " " << check.name;
    if (!check.detail.empty()) {
      ss << " — " << check.detail;
    }
    ss << std::endl;
  }
  return ss.str();
}

// ---------------------------------------------------------------------------
// Helper: Format BT address as string
// ---------------------------------------------------------------------------
static std::string FormatBluetoothAddress(const BLUETOOTH_ADDRESS &addr) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (int i = 5; i >= 0; --i) {
    ss << std::setw(2) << static_cast<int>(addr.rgBytes[i]);
    if (i > 0)
      ss << ":";
  }
  return ss.str();
}

// ---------------------------------------------------------------------------
// Helper: Wide string to UTF-8
// ---------------------------------------------------------------------------
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
// BluetoothValidator::Validate
// ---------------------------------------------------------------------------
BluetoothValidationResult BluetoothValidator::Validate() {
  BluetoothValidationResult result;
  result.allPassed = true;
  result.adapterClass = 0;
  result.adapterManufacturer = 0;
  result.adapterSubversion = 0;

  // Run checks in order — later checks depend on adapter being present
  if (!CheckAdapterPresent(result))
    return result;
  CheckAdapterEnabled(result);
  CheckClassicSupport(result);
  CheckDiscoverability(result);
  CheckDriverStack(result);
  CheckPolicyRestrictions(result);

  return result;
}

// ---------------------------------------------------------------------------
// BluetoothValidator::IsBluetoothAvailable
// ---------------------------------------------------------------------------
bool BluetoothValidator::IsBluetoothAvailable() {
  BLUETOOTH_FIND_RADIO_PARAMS findParams;
  findParams.dwSize = sizeof(findParams);
  HANDLE hRadio = nullptr;

  HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&findParams, &hRadio);
  if (hFind == nullptr)
    return false;

  CloseHandle(hRadio);
  BluetoothFindRadioClose(hFind);
  return true;
}

// ---------------------------------------------------------------------------
// Check: Adapter Present
// ---------------------------------------------------------------------------
bool BluetoothValidator::CheckAdapterPresent(
    BluetoothValidationResult &result) {
  BLUETOOTH_FIND_RADIO_PARAMS findParams;
  findParams.dwSize = sizeof(findParams);
  HANDLE hRadio = nullptr;

  HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&findParams, &hRadio);
  if (hFind == nullptr) {
    result.AddCheck(
        "Bluetooth Adapter Present", false,
        "No Bluetooth radio found. Ensure a Bluetooth adapter is connected.");
    return false;
  }

  // Get radio info
  BLUETOOTH_RADIO_INFO radioInfo;
  radioInfo.dwSize = sizeof(radioInfo);
  DWORD dwResult = BluetoothGetRadioInfo(hRadio, &radioInfo);

  if (dwResult == ERROR_SUCCESS) {
    result.adapterName = WideToUtf8(radioInfo.szName);
    result.adapterAddress = FormatBluetoothAddress(radioInfo.address);
    result.adapterClass = radioInfo.ulClassofDevice;
    result.adapterManufacturer = radioInfo.manufacturer;
    result.adapterSubversion = radioInfo.lmpSubversion;

    result.AddCheck("Bluetooth Adapter Present", true,
                    "Found: " + result.adapterName + " [" +
                        result.adapterAddress + "]");
  } else {
    result.AddCheck("Bluetooth Adapter Present", true,
                    "Radio found but could not query info.");
  }

  CloseHandle(hRadio);
  BluetoothFindRadioClose(hFind);
  return true;
}

// ---------------------------------------------------------------------------
// Check: Adapter Enabled
// ---------------------------------------------------------------------------
bool BluetoothValidator::CheckAdapterEnabled(
    BluetoothValidationResult &result) {
  BLUETOOTH_FIND_RADIO_PARAMS findParams;
  findParams.dwSize = sizeof(findParams);
  HANDLE hRadio = nullptr;

  HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&findParams, &hRadio);
  if (hFind == nullptr) {
    result.AddCheck("Bluetooth Adapter Enabled", false, "No radio to check.");
    return false;
  }

  // Check if adapter is connectable (which implies enabled)
  BOOL connectable = BluetoothIsConnectable(hRadio);
  CloseHandle(hRadio);
  BluetoothFindRadioClose(hFind);

  if (connectable) {
    result.AddCheck("Bluetooth Adapter Enabled", true,
                    "Adapter is connectable.");
    return true;
  } else {
    result.AddCheck(
        "Bluetooth Adapter Enabled", false,
        "Adapter is not connectable. Enable Bluetooth in Settings.");
    return false;
  }
}

// ---------------------------------------------------------------------------
// Check: Bluetooth Classic (BR/EDR) Support
// ---------------------------------------------------------------------------
bool BluetoothValidator::CheckClassicSupport(
    BluetoothValidationResult &result) {
  // If we found a radio via BluetoothFindFirstRadio, it supports Classic.
  // BLE-only adapters may still show up, but standard Windows BT APIs
  // primarily work with Classic radios.

  BLUETOOTH_FIND_RADIO_PARAMS findParams;
  findParams.dwSize = sizeof(findParams);
  HANDLE hRadio = nullptr;

  HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&findParams, &hRadio);
  if (hFind == nullptr) {
    result.AddCheck("Bluetooth Classic Support", false, "No radio found.");
    return false;
  }

  BLUETOOTH_RADIO_INFO radioInfo;
  radioInfo.dwSize = sizeof(radioInfo);
  DWORD dwResult = BluetoothGetRadioInfo(hRadio, &radioInfo);

  bool classicSupported = true;
  std::string detail;

  if (dwResult == ERROR_SUCCESS) {
    // Check LMP version — Classic support is implied by standard radios
    // LMP version >= 3 means Bluetooth 2.0+ which supports EDR
    detail = "LMP version: " + std::to_string(radioInfo.lmpSubversion);

    // Check class of device — ensure it's not a BLE-only device
    if (radioInfo.ulClassofDevice == 0) {
      detail += " (CoD is zero — may be BLE-only)";
      // Don't fail; some adapters report CoD=0 but still support Classic
    }
  } else {
    detail = "Could not query radio info.";
  }

  result.AddCheck("Bluetooth Classic (BR/EDR) Support", classicSupported,
                  detail);

  CloseHandle(hRadio);
  BluetoothFindRadioClose(hFind);
  return classicSupported;
}

// ---------------------------------------------------------------------------
// Check: Discoverability
// ---------------------------------------------------------------------------
bool BluetoothValidator::CheckDiscoverability(
    BluetoothValidationResult &result) {
  BLUETOOTH_FIND_RADIO_PARAMS findParams;
  findParams.dwSize = sizeof(findParams);
  HANDLE hRadio = nullptr;

  HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&findParams, &hRadio);
  if (hFind == nullptr) {
    result.AddCheck("Bluetooth Discoverable", false, "No radio found.");
    return false;
  }

  BOOL discoverable = BluetoothIsDiscoverable(hRadio);
  CloseHandle(hRadio);
  BluetoothFindRadioClose(hFind);

  if (discoverable) {
    result.AddCheck("Bluetooth Discoverable", true,
                    "Adapter is discoverable by other devices.");
  } else {
    // Not a hard failure — we can enable discoverability programmatically
    result.AddCheck(
        "Bluetooth Discoverable", true,
        "Not currently discoverable (will be enabled when broadcasting).");
  }

  return true;
}

// ---------------------------------------------------------------------------
// Check: Driver Stack
// ---------------------------------------------------------------------------
bool BluetoothValidator::CheckDriverStack(BluetoothValidationResult &result) {
  // Check for presence of BthPort.sys driver via SetupAPI
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_BLUETOOTH, nullptr,
                                           nullptr, DIGCF_PRESENT);

  if (hDevInfo == INVALID_HANDLE_VALUE) {
    result.AddCheck("Bluetooth Driver Stack", false,
                    "Could not enumerate Bluetooth device class.");
    return false;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(devInfoData);
  int deviceCount = 0;

  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
    deviceCount++;
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);

  if (deviceCount > 0) {
    result.AddCheck("Bluetooth Driver Stack", true,
                    std::to_string(deviceCount) +
                        " Bluetooth device(s) found in device manager.");
    return true;
  } else {
    result.AddCheck(
        "Bluetooth Driver Stack", false,
        "No Bluetooth devices found in device manager. Check drivers.");
    return false;
  }
}

// ---------------------------------------------------------------------------
// Check: Policy Restrictions
// ---------------------------------------------------------------------------
bool BluetoothValidator::CheckPolicyRestrictions(
    BluetoothValidationResult &result) {
  // Check Group Policy / MDM restrictions on Bluetooth
  HKEY hKey;
  LONG regResult = RegOpenKeyExW(
      HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Bluetooth",
      0, KEY_READ, &hKey);

  if (regResult != ERROR_SUCCESS) {
    // No policy key — no restrictions
    result.AddCheck("Bluetooth Policy Restrictions", true,
                    "No Bluetooth group policy restrictions detected.");
    return true;
  }

  // Check for specific restriction values
  DWORD disableBluetooth = 0;
  DWORD dataSize = sizeof(disableBluetooth);
  LONG queryResult =
      RegQueryValueExW(hKey, L"DisableBluetooth", nullptr, nullptr,
                       reinterpret_cast<LPBYTE>(&disableBluetooth), &dataSize);

  RegCloseKey(hKey);

  if (queryResult == ERROR_SUCCESS && disableBluetooth != 0) {
    result.AddCheck(
        "Bluetooth Policy Restrictions", false,
        "Bluetooth is disabled by group policy (DisableBluetooth=1).");
    return false;
  }

  result.AddCheck("Bluetooth Policy Restrictions", true,
                  "No restrictive policies detected.");
  return true;
}

} // namespace CursorShare
