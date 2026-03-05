// =============================================================================
// CursorShare — Bluetooth HID Service (Implementation)
// =============================================================================

#include "bt_hid_service.h"
#include "../common/constants.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>

namespace CursorShare {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
BluetoothHidService::BluetoothHidService() {
  std::memset(keyboardReportBuf_, 0, sizeof(keyboardReportBuf_));
  std::memset(mouseReportBuf_, 0, sizeof(mouseReportBuf_));
  keyboardReportBuf_[0] = kReportIdKeyboard;
  mouseReportBuf_[0] = kReportIdMouse;
}

BluetoothHidService::~BluetoothHidService() { Shutdown(); }

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
bool BluetoothHidService::Initialize() {
  if (state_.load(std::memory_order_acquire) != BtHidState::Stopped) {
    return false;
  }

  state_.store(BtHidState::Initializing, std::memory_order_release);

  if (!InitWinsock()) {
    state_.store(BtHidState::Error, std::memory_order_release);
    return false;
  }

  // Save original Bluetooth state for restoration
  BLUETOOTH_FIND_RADIO_PARAMS findParams;
  findParams.dwSize = sizeof(findParams);
  HANDLE hRadio = nullptr;
  HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&findParams, &hRadio);
  if (hFind) {
    savedRadioHandle_ = hRadio;
    originalDiscoverable_ = BluetoothIsDiscoverable(hRadio);
    originalConnectable_ = BluetoothIsConnectable(hRadio);
    BluetoothFindRadioClose(hFind);
  }

  return true;
}

// ---------------------------------------------------------------------------
// StartListening
// ---------------------------------------------------------------------------
bool BluetoothHidService::StartListening() {
  if (state_.load(std::memory_order_acquire) != BtHidState::Initializing) {
    return false;
  }

  if (!RegisterSdpRecord()) {
    state_.store(BtHidState::Error, std::memory_order_release);
    return false;
  }

  if (!CreateListeningSockets()) {
    UnregisterSdpRecord();
    state_.store(BtHidState::Error, std::memory_order_release);
    return false;
  }

  // Enable discoverability
  SetDiscoverable(true);

  shouldStop_.store(false, std::memory_order_release);
  state_.store(BtHidState::Listening, std::memory_order_release);

  // Start accept thread
  acceptThread_ = std::thread([this]() { AcceptLoop(); });

  return true;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void BluetoothHidService::Shutdown() {
  shouldStop_.store(true, std::memory_order_release);

  // Close listening sockets to unblock accept()
  if (controlListenSocket_ != INVALID_SOCKET) {
    closesocket(controlListenSocket_);
    controlListenSocket_ = INVALID_SOCKET;
  }
  if (interruptListenSocket_ != INVALID_SOCKET) {
    closesocket(interruptListenSocket_);
    interruptListenSocket_ = INVALID_SOCKET;
  }

  // Wait for accept thread
  if (acceptThread_.joinable()) {
    acceptThread_.join();
  }

  // Disconnect all clients
  {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    for (auto &dev : connectedDevices_) {
      if (dev.controlSocket != INVALID_SOCKET) {
        closesocket(dev.controlSocket);
      }
      if (dev.interruptSocket != INVALID_SOCKET) {
        closesocket(dev.interruptSocket);
      }
      if (deviceCallback_) {
        deviceCallback_(dev, false);
      }
    }
    connectedDevices_.clear();
  }

  // Unregister SDP
  UnregisterSdpRecord();

  // Restore original Bluetooth state
  if (savedRadioHandle_) {
    BluetoothEnableDiscovery(savedRadioHandle_, originalDiscoverable_);
    BluetoothEnableIncomingConnections(savedRadioHandle_, originalConnectable_);
    CloseHandle(savedRadioHandle_);
    savedRadioHandle_ = nullptr;
  }

  // Cleanup Winsock
  if (winsockInitialized_) {
    WSACleanup();
    winsockInitialized_ = false;
  }

  state_.store(BtHidState::Stopped, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// SendKeyboardReport
// ---------------------------------------------------------------------------
bool BluetoothHidService::SendKeyboardReport(uint8_t modifiers,
                                             const uint8_t keys[6]) {
  // Encode into pre-allocated buffer (Report ID already set)
  EncodeKeyboardReport(modifiers, keys, keyboardReportBuf_ + 1);

  std::lock_guard<std::mutex> lock(devicesMutex_);
  bool success = true;
  for (auto &dev : connectedDevices_) {
    if (dev.interruptSocket != INVALID_SOCKET) {
      if (!SendReport(dev.interruptSocket, keyboardReportBuf_,
                      sizeof(keyboardReportBuf_))) {
        success = false;
      }
    }
  }
  return success;
}

// ---------------------------------------------------------------------------
// SendMouseReport
// ---------------------------------------------------------------------------
bool BluetoothHidService::SendMouseReport(uint8_t buttons, int16_t dx,
                                          int16_t dy, int8_t wheel,
                                          int8_t hwheel) {
  EncodeMouseReport(buttons, dx, dy, wheel, hwheel, mouseReportBuf_ + 1);

  std::lock_guard<std::mutex> lock(devicesMutex_);
  bool success = true;
  for (auto &dev : connectedDevices_) {
    if (dev.interruptSocket != INVALID_SOCKET) {
      if (!SendReport(dev.interruptSocket, mouseReportBuf_,
                      sizeof(mouseReportBuf_))) {
        success = false;
      }
    }
  }
  return success;
}

// ---------------------------------------------------------------------------
// SendKeyboardAllUp
// ---------------------------------------------------------------------------
bool BluetoothHidService::SendKeyboardAllUp() {
  uint8_t noKeys[6] = {};
  return SendKeyboardReport(0, noKeys);
}

// ---------------------------------------------------------------------------
// SendMouseIdle
// ---------------------------------------------------------------------------
bool BluetoothHidService::SendMouseIdle() {
  return SendMouseReport(0, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// SendInputEvent
// ---------------------------------------------------------------------------
bool BluetoothHidService::SendInputEvent(const InputEvent &event) {
  switch (event.type) {
  case InputEventType::KeyDown:
  case InputEventType::KeyUp: {
    // Handle "all keys up" special flag
    if (event.data.keyboard.flags == 0xFF) {
      return SendKeyboardAllUp();
    }

    // Update tracked key state
    // In a production system we'd maintain a full key map;
    // for now we track modifiers and current key array
    if (event.type == InputEventType::KeyDown) {
      // Add key to currentKeys_ if not already present
      bool found = false;
      for (int i = 0; i < 6; ++i) {
        if (currentKeys_[i] == event.data.keyboard.scanCode) {
          found = true;
          break;
        }
      }
      if (!found) {
        for (int i = 0; i < 6; ++i) {
          if (currentKeys_[i] == 0) {
            currentKeys_[i] =
                static_cast<uint8_t>(event.data.keyboard.scanCode);
            break;
          }
        }
      }
    } else {
      // Remove key from currentKeys_
      for (int i = 0; i < 6; ++i) {
        if (currentKeys_[i] == event.data.keyboard.scanCode) {
          currentKeys_[i] = 0;
          break;
        }
      }
    }

    return SendKeyboardReport(currentModifiers_, currentKeys_);
  }

  case InputEventType::MouseMove:
    return SendMouseReport(event.data.mouse.buttons, event.data.mouse.dx,
                           event.data.mouse.dy, 0, 0);

  case InputEventType::MouseButtonDown:
  case InputEventType::MouseButtonUp:
    return SendMouseReport(event.data.mouse.buttons, 0, 0, 0, 0);

  case InputEventType::MouseWheel:
    return SendMouseReport(
        event.data.mouse.buttons, 0, 0,
        static_cast<int8_t>(std::clamp(
            static_cast<int>(event.data.mouse.wheelDelta / 120), -127, 127)),
        0);

  case InputEventType::MouseHWheel:
    return SendMouseReport(
        event.data.mouse.buttons, 0, 0, 0,
        static_cast<int8_t>(std::clamp(
            static_cast<int>(event.data.mouse.hWheelDelta / 120), -127, 127)));

  default:
    return false;
  }
}

// ---------------------------------------------------------------------------
// GetConnectedDevices
// ---------------------------------------------------------------------------
std::vector<ConnectedDevice> BluetoothHidService::GetConnectedDevices() const {
  std::lock_guard<std::mutex> lock(devicesMutex_);
  return connectedDevices_;
}

// ---------------------------------------------------------------------------
// SetDiscoverable
// ---------------------------------------------------------------------------
bool BluetoothHidService::SetDiscoverable(bool discoverable) {
  BLUETOOTH_FIND_RADIO_PARAMS findParams;
  findParams.dwSize = sizeof(findParams);
  HANDLE hRadio = nullptr;

  HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&findParams, &hRadio);
  if (!hFind)
    return false;

  BOOL result = BluetoothEnableDiscovery(hRadio, discoverable ? TRUE : FALSE);
  if (discoverable) {
    BluetoothEnableIncomingConnections(hRadio, TRUE);
  }

  CloseHandle(hRadio);
  BluetoothFindRadioClose(hFind);
  return result != FALSE;
}

// ---------------------------------------------------------------------------
// GetConnectionCount
// ---------------------------------------------------------------------------
int BluetoothHidService::GetConnectionCount() const {
  std::lock_guard<std::mutex> lock(devicesMutex_);
  return static_cast<int>(connectedDevices_.size());
}

// ---------------------------------------------------------------------------
// InitWinsock
// ---------------------------------------------------------------------------
bool BluetoothHidService::InitWinsock() {
  WSADATA wsaData;
  int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (result != 0)
    return false;
  winsockInitialized_ = true;
  return true;
}

// ---------------------------------------------------------------------------
// RegisterSdpRecord
// ---------------------------------------------------------------------------
bool BluetoothHidService::RegisterSdpRecord() {
  // Build Bluetooth SDP record for HID service
  // This registers CursorShare as a HID keyboard+mouse device
  // Uses WSASetService with Bluetooth-specific service info

  CSADDR_INFO csAddr = {};
  SOCKADDR_BTH localAddr = {};
  localAddr.addressFamily = AF_BTH;
  localAddr.btAddr = 0; // Local adapter
  localAddr.port = BT_PORT_ANY;

  csAddr.LocalAddr.lpSockaddr = reinterpret_cast<SOCKADDR *>(&localAddr);
  csAddr.LocalAddr.iSockaddrLength = sizeof(localAddr);
  csAddr.iSocketType = SOCK_STREAM;
  csAddr.iProtocol = BTHPROTO_L2CAP;

  // HID Service Class UUID
  GUID hidServiceGuid = {0x00001124,
                         0x0000,
                         0x1000,
                         {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

  WSAQUERYSETW wsaQuerySet = {};
  wsaQuerySet.dwSize = sizeof(wsaQuerySet);
  wsaQuerySet.lpServiceClassId = &hidServiceGuid;
  wsaQuerySet.lpszServiceInstanceName = const_cast<LPWSTR>(L"CursorShare HID");
  wsaQuerySet.lpszComment =
      const_cast<LPWSTR>(L"CursorShare Bluetooth HID Keyboard+Mouse");
  wsaQuerySet.dwNameSpace = NS_BTH;
  wsaQuerySet.dwNumberOfCsAddrs = 1;
  wsaQuerySet.lpcsaBuffer = &csAddr;

  INT result = WSASetServiceW(&wsaQuerySet, RNRSERVICE_REGISTER, 0);
  if (result == SOCKET_ERROR) {
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// UnregisterSdpRecord
// ---------------------------------------------------------------------------
void BluetoothHidService::UnregisterSdpRecord() {
  GUID hidServiceGuid = {0x00001124,
                         0x0000,
                         0x1000,
                         {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

  WSAQUERYSETW wsaQuerySet = {};
  wsaQuerySet.dwSize = sizeof(wsaQuerySet);
  wsaQuerySet.lpServiceClassId = &hidServiceGuid;
  wsaQuerySet.lpszServiceInstanceName = const_cast<LPWSTR>(L"CursorShare HID");
  wsaQuerySet.dwNameSpace = NS_BTH;

  WSASetServiceW(&wsaQuerySet, RNRSERVICE_DELETE, 0);
}

// ---------------------------------------------------------------------------
// CreateListeningSockets
// ---------------------------------------------------------------------------
bool BluetoothHidService::CreateListeningSockets() {
  // Create L2CAP listening socket for control channel
  controlListenSocket_ = socket(AF_BTH, SOCK_STREAM, BTHPROTO_L2CAP);
  if (controlListenSocket_ == INVALID_SOCKET)
    return false;

  SOCKADDR_BTH addr = {};
  addr.addressFamily = AF_BTH;
  addr.btAddr = 0;
  addr.port = BT_PORT_ANY; // Let system assign

  if (bind(controlListenSocket_, reinterpret_cast<SOCKADDR *>(&addr),
           sizeof(addr)) == SOCKET_ERROR) {
    closesocket(controlListenSocket_);
    controlListenSocket_ = INVALID_SOCKET;
    return false;
  }

  if (listen(controlListenSocket_, 4) == SOCKET_ERROR) {
    closesocket(controlListenSocket_);
    controlListenSocket_ = INVALID_SOCKET;
    return false;
  }

  // Create interrupt channel socket
  interruptListenSocket_ = socket(AF_BTH, SOCK_STREAM, BTHPROTO_L2CAP);
  if (interruptListenSocket_ == INVALID_SOCKET) {
    closesocket(controlListenSocket_);
    controlListenSocket_ = INVALID_SOCKET;
    return false;
  }

  SOCKADDR_BTH intAddr = {};
  intAddr.addressFamily = AF_BTH;
  intAddr.btAddr = 0;
  intAddr.port = BT_PORT_ANY;

  if (bind(interruptListenSocket_, reinterpret_cast<SOCKADDR *>(&intAddr),
           sizeof(intAddr)) == SOCKET_ERROR) {
    closesocket(interruptListenSocket_);
    interruptListenSocket_ = INVALID_SOCKET;
    closesocket(controlListenSocket_);
    controlListenSocket_ = INVALID_SOCKET;
    return false;
  }

  if (listen(interruptListenSocket_, 4) == SOCKET_ERROR) {
    closesocket(interruptListenSocket_);
    interruptListenSocket_ = INVALID_SOCKET;
    closesocket(controlListenSocket_);
    controlListenSocket_ = INVALID_SOCKET;
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// AcceptLoop
// ---------------------------------------------------------------------------
void BluetoothHidService::AcceptLoop() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

  while (!shouldStop_.load(std::memory_order_acquire)) {
    // Use select() with timeout to allow periodic shouldStop_ check
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(controlListenSocket_, &readSet);

    timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
    if (selectResult <= 0)
      continue;

    // Accept control channel connection
    SOCKADDR_BTH clientAddr = {};
    int addrLen = sizeof(clientAddr);

    SOCKET controlClient =
        accept(controlListenSocket_, reinterpret_cast<SOCKADDR *>(&clientAddr),
               &addrLen);
    if (controlClient == INVALID_SOCKET)
      continue;

    // Now accept interrupt channel (client should connect both)
    fd_set intReadSet;
    FD_ZERO(&intReadSet);
    FD_SET(interruptListenSocket_, &intReadSet);

    timeval intTimeout;
    intTimeout.tv_sec = 5; // Wait up to 5 seconds for interrupt channel
    intTimeout.tv_usec = 0;

    SOCKET interruptClient = INVALID_SOCKET;
    int intSelectResult = select(0, &intReadSet, nullptr, nullptr, &intTimeout);
    if (intSelectResult > 0) {
      SOCKADDR_BTH intClientAddr = {};
      int intAddrLen = sizeof(intClientAddr);
      interruptClient =
          accept(interruptListenSocket_,
                 reinterpret_cast<SOCKADDR *>(&intClientAddr), &intAddrLen);
    }

    // Set socket options for low latency
    BOOL noDelay = TRUE;
    setsockopt(controlClient, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
    if (interruptClient != INVALID_SOCKET) {
      setsockopt(interruptClient, SOL_SOCKET, SO_SNDBUF,
                 reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
    }

    // Build device info
    ConnectedDevice device;
    device.address = clientAddr.btAddr;
    device.controlSocket = controlClient;
    device.interruptSocket = interruptClient;
    device.authenticated = false;
    device.connectedAt = GetQPCTimestamp();

    // Try to get device name
    BLUETOOTH_DEVICE_INFO devInfo;
    devInfo.dwSize = sizeof(devInfo);
    devInfo.Address.ullLong = clientAddr.btAddr;
    if (BluetoothGetDeviceInfo(nullptr, &devInfo) == ERROR_SUCCESS) {
      int len = WideCharToMultiByte(CP_UTF8, 0, devInfo.szName, -1, nullptr, 0,
                                    nullptr, nullptr);
      if (len > 0) {
        device.name.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, devInfo.szName, -1, &device.name[0],
                            len, nullptr, nullptr);
      }
    }

    {
      std::lock_guard<std::mutex> lock(devicesMutex_);
      connectedDevices_.push_back(device);
    }

    state_.store(BtHidState::Connected, std::memory_order_release);

    if (deviceCallback_) {
      deviceCallback_(device, true);
    }
  }
}

// ---------------------------------------------------------------------------
// SendReport
// ---------------------------------------------------------------------------
bool BluetoothHidService::SendReport(SOCKET sock, const uint8_t *data,
                                     int len) {
  if (sock == INVALID_SOCKET)
    return false;

  // L2CAP HID reports are prefixed with a transaction header byte
  // For DATA reports on interrupt channel: header = 0xA1 (DATA | INPUT)
  uint8_t header = 0xA1;

  // Use scatter/gather I/O for zero-copy
  WSABUF bufs[2];
  bufs[0].buf = reinterpret_cast<char *>(&header);
  bufs[0].len = 1;
  bufs[1].buf = const_cast<char *>(reinterpret_cast<const char *>(data));
  bufs[1].len = static_cast<ULONG>(len);

  DWORD bytesSent = 0;
  int result = WSASend(sock, bufs, 2, &bytesSent, 0, nullptr, nullptr);
  return (result == 0);
}

} // namespace CursorShare
