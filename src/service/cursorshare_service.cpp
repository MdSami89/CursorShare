// =============================================================================
// CursorShare — Main Service (Implementation)
// =============================================================================

#include "cursorshare_service.h"
#include "../common/constants.h"
#include "../common/logger.h"
#include <sstream>

namespace CursorShare {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
CursorShareService::CursorShareService() = default;

CursorShareService::~CursorShareService() { Shutdown(); }

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
bool CursorShareService::Initialize(const ServiceConfig &config) {
  config_ = config;

  // Step 1: Validate Bluetooth capability
  lastDiagnostics_ = BluetoothValidator::Validate();
  if (!lastDiagnostics_.allPassed) {
    // Log diagnostics but don't prevent startup for non-critical failures
    LOG_WARN("Service", "BT diagnostics: %s",
             lastDiagnostics_.GetSummary().c_str());

    // Check for critical failures
    bool hasCriticalFailure = false;
    for (const auto &check : lastDiagnostics_.checks) {
      if (!check.passed && (check.name == "Bluetooth Adapter Present" ||
                            check.name == "Bluetooth Adapter Enabled")) {
        hasCriticalFailure = true;
        break;
      }
    }
    if (hasCriticalFailure) {
      return false;
    }
  }

  // Step 2: Snapshot paired devices for preservation
  startupPairedDevices_ = pairingManager_.SnapshotPairedDevices();

  // Step 3: Initialize Bluetooth HID service
  if (!btService_.Initialize()) {
    LOG_ERROR("Service", "Failed to initialize Bluetooth HID service");
    return false;
  }

  // Step 4: Set up input router callbacks
  router_.SetClientCallback(
      [this](const InputEvent &event) { OnClientOutput(event); });

  // Step 5: Set up device event callback
  btService_.SetDeviceCallback(
      [this](const ConnectedDevice &dev, bool connected) {
        OnDeviceEvent(dev, connected);
      });

  // Step 6: Set up shortcut manager
  shortcutManager_.SetCallback([this]() { ToggleTarget(); });

  // Step 7: Set default client display
  mouseBoundary_.SetClientDisplay(config_.defaultDisplay);

  return true;
}

// ---------------------------------------------------------------------------
// StartBroadcast
// ---------------------------------------------------------------------------
bool CursorShareService::StartBroadcast() {
  if (broadcastState_.load(std::memory_order_acquire) !=
      BroadcastState::Stopped) {
    return false;
  }

  broadcastState_.store(BroadcastState::Starting, std::memory_order_release);

  // Start Bluetooth listening
  if (!btService_.StartListening()) {
    LOG_ERROR("Service", "Failed to start BT listening");
    broadcastState_.store(BroadcastState::Error, std::memory_order_release);
    return false;
  }

  // Start input capture
  RawInputConfig riConfig;
  riConfig.captureKeyboard = true;
  riConfig.captureMouse = true;
  riConfig.exclusiveMode = config_.exclusiveMode;
  riConfig.backgroundCapture = true;

  rawInput_.SetCallback(
      [this](const InputEvent &event) { OnInputEvent(event); });

  if (!rawInput_.Start(riConfig)) {
    LOG_ERROR("Service", "Failed to start Raw Input capture");
    btService_.Shutdown();
    broadcastState_.store(BroadcastState::Error, std::memory_order_release);
    return false;
  }

  // Start shortcut listener
  shortcutManager_.Start(config_.shortcut);

  // Start named pipe server for UI
  shouldStopPipe_.store(false, std::memory_order_release);
  pipeThread_ = std::thread([this]() { PipeServerThread(); });

  broadcastState_.store(BroadcastState::Running, std::memory_order_release);
  return true;
}

// ---------------------------------------------------------------------------
// StartBleBroadcast
// ---------------------------------------------------------------------------
bool CursorShareService::StartBleBroadcast() {
  // Initialize BLE HID service
  if (!bleService_.Initialize()) {
    LOG_ERROR("Service", "BLE HID initialization failed.");
    return false;
  }

  // Set BLE device event callback
  bleService_.SetDeviceCallback(
      [this](const std::string &deviceId, bool connected) {
        if (connected) {
          router_.OnClientConnected();
          // Enable exclusive mode — input goes only to client
          rawInput_.SetExclusiveMode(true);
          LOG_INFO("Service", "BLE device connected: %s", deviceId.c_str());
        } else {
          router_.OnClientDisconnected();
          // Disable exclusive mode — input returns to host
          rawInput_.SetExclusiveMode(false);
          LOG_INFO("Service", "BLE device disconnected: %s", deviceId.c_str());
        }
      });

  // Start advertising
  if (!bleService_.StartAdvertising()) {
    LOG_ERROR("Service", "BLE advertising failed.");
    return false;
  }

  // Start input capture if not already running
  if (!rawInput_.IsRunning()) {
    RawInputConfig riConfig;
    riConfig.captureKeyboard = true;
    riConfig.captureMouse = true;
    riConfig.exclusiveMode = config_.exclusiveMode;
    riConfig.backgroundCapture = true;

    rawInput_.SetCallback(
        [this](const InputEvent &event) { OnInputEvent(event); });

    if (!rawInput_.Start(riConfig)) {
      LOG_ERROR("Service", "Failed to start Raw Input capture");
      bleService_.Shutdown();
      return false;
    }
  }

  // Start shortcut listener if not running
  if (!shortcutManager_.IsRunning()) {
    shortcutManager_.Start(config_.shortcut);
  }

  // Set up input router to forward to BLE
  router_.SetClientCallback(
      [this](const InputEvent &event) { OnClientOutput(event); });

  broadcastState_.store(BroadcastState::Running, std::memory_order_release);
  return true;
}

// ---------------------------------------------------------------------------
// StopBroadcast
// ---------------------------------------------------------------------------
void CursorShareService::StopBroadcast() {
  if (broadcastState_.load(std::memory_order_acquire) ==
      BroadcastState::Stopped) {
    return;
  }

  broadcastState_.store(BroadcastState::Stopping, std::memory_order_release);

  // Switch to host mode first (flush state)
  router_.SwitchToHost();

  // Stop input capture
  rawInput_.Stop();

  // Stop shortcut listener
  shortcutManager_.Stop();

  // Stop pipe server
  shouldStopPipe_.store(true, std::memory_order_release);
  if (pipeThread_.joinable()) {
    pipeThread_.join();
  }

  // Shutdown Bluetooth Classic
  btService_.Shutdown();

  // Shutdown BLE HID
  bleService_.Shutdown();

  broadcastState_.store(BroadcastState::Stopped, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void CursorShareService::Shutdown() {
  StopBroadcast();

  // Verify paired devices are preserved
  if (!startupPairedDevices_.empty()) {
    bool preserved = pairingManager_.VerifyPairedDevices(startupPairedDevices_);
    if (!preserved) {
      LOG_WARN("Service",
               "Some paired devices may have been modified during operation.");
    }
  }

  // Reset latency monitor
  latencyMonitor_.Reset();
}

// ---------------------------------------------------------------------------
// GetStatus
// ---------------------------------------------------------------------------
StatusPayload CursorShareService::GetStatus() const {
  StatusPayload status = {};
  status.broadcastState = broadcastState_.load(std::memory_order_acquire);
  status.targetMode = (router_.GetTarget() == RouteTarget::Host)
                          ? TargetMode::Host
                          : TargetMode::Client;
  status.inputMode = config_.inputMode;
  status.connectedDeviceCount =
      static_cast<uint8_t>(btService_.GetConnectionCount());
  status.btAdapterPresent = BluetoothValidator::IsBluetoothAvailable() ? 1 : 0;
  status.btAdapterEnabled = status.btAdapterPresent; // Simplified
  status.driverInstalled = 0; // TODO: Check driver installation status
  return status;
}

// ---------------------------------------------------------------------------
// SetExclusiveMode
// ---------------------------------------------------------------------------
void CursorShareService::SetExclusiveMode(bool exclusive) {
  config_.exclusiveMode = exclusive;
  // If currently running, need to restart input capture with new mode
  if (rawInput_.IsRunning()) {
    rawInput_.Stop();
    RawInputConfig riConfig;
    riConfig.captureKeyboard = true;
    riConfig.captureMouse = true;
    riConfig.exclusiveMode = exclusive;
    riConfig.backgroundCapture = true;
    rawInput_.Start(riConfig);
  }
}

// ---------------------------------------------------------------------------
// OnInputEvent
// ---------------------------------------------------------------------------
void CursorShareService::OnInputEvent(const InputEvent &event) {
  int64_t captureTime = GetQPCTimestamp();

  // Record capture latency
  latencyMonitor_.RecordSample(PipelineStage::InputCapture, event.timestamp,
                               captureTime);

  // Route the event
  int64_t routeStart = captureTime;
  router_.RouteEvent(event);
  int64_t routeEnd = GetQPCTimestamp();

  latencyMonitor_.RecordSample(PipelineStage::RoutingDecision, routeStart,
                               routeEnd);
}

// ---------------------------------------------------------------------------
// OnClientOutput
// ---------------------------------------------------------------------------
void CursorShareService::OnClientOutput(const InputEvent &event) {
  int64_t encodeStart = GetQPCTimestamp();

  // Apply mouse boundary clamping for mouse events
  InputEvent processedEvent = event;
  if (event.type == InputEventType::MouseMove) {
    int16_t clampedDx, clampedDy;
    mouseBoundary_.ApplyMovement(event.data.mouse.dx, event.data.mouse.dy,
                                 clampedDx, clampedDy);
    processedEvent.data.mouse.dx = clampedDx;
    processedEvent.data.mouse.dy = clampedDy;
  }

  int64_t encodeEnd = GetQPCTimestamp();
  latencyMonitor_.RecordSample(PipelineStage::HidEncoding, encodeStart,
                               encodeEnd);

  // Transmit over Bluetooth Classic
  int64_t txStart = encodeEnd;
  btService_.SendInputEvent(processedEvent);

  // Also transmit over BLE if connected
  if (bleService_.GetState() == BleHidState::Connected) {
    bleService_.SendInputEvent(processedEvent);
  }

  int64_t txEnd = GetQPCTimestamp();

  latencyMonitor_.RecordSample(PipelineStage::BtTransmit, txStart, txEnd);

  // Record end-to-end
  latencyMonitor_.RecordSample(PipelineStage::EndToEnd, event.timestamp, txEnd);
}

// ---------------------------------------------------------------------------
// OnDeviceEvent
// ---------------------------------------------------------------------------
void CursorShareService::OnDeviceEvent(const ConnectedDevice &device,
                                       bool connected) {
  if (connected) {
    router_.OnClientConnected();
    // Enable exclusive mode — input goes only to client
    rawInput_.SetExclusiveMode(true);
    LOG_INFO("Service", "Device connected: %s", device.name.c_str());
  } else {
    router_.OnClientDisconnected();
    // Disable exclusive mode — input returns to host
    rawInput_.SetExclusiveMode(false);
    LOG_INFO("Service", "Device disconnected: %s", device.name.c_str());
  }
}

// ---------------------------------------------------------------------------
// PipeServerThread
// ---------------------------------------------------------------------------
void CursorShareService::PipeServerThread() {
  while (!shouldStopPipe_.load(std::memory_order_acquire)) {
    // Create named pipe instance
    HANDLE hPipe = CreateNamedPipeW(
        kServicePipeName, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, kMaxPipePayload + sizeof(PipeMessageHeader),
        kMaxPipePayload + sizeof(PipeMessageHeader),
        1000, // 1 second timeout
        nullptr);

    if (hPipe == INVALID_HANDLE_VALUE) {
      Sleep(1000);
      continue;
    }

    // Wait for client connection with periodic check for shutdown
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    BOOL connected = ConnectNamedPipe(hPipe, &overlapped);
    if (!connected) {
      DWORD err = GetLastError();
      if (err == ERROR_IO_PENDING) {
        // Wait with timeout
        while (!shouldStopPipe_.load(std::memory_order_acquire)) {
          DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 500);
          if (waitResult == WAIT_OBJECT_0) {
            break;
          }
        }
      } else if (err != ERROR_PIPE_CONNECTED) {
        CloseHandle(overlapped.hEvent);
        CloseHandle(hPipe);
        continue;
      }
    }

    CloseHandle(overlapped.hEvent);

    if (shouldStopPipe_.load(std::memory_order_acquire)) {
      CloseHandle(hPipe);
      break;
    }

    // Handle messages from UI client
    while (!shouldStopPipe_.load(std::memory_order_acquire)) {
      PipeMessageHeader header;
      DWORD bytesRead;

      BOOL readOk =
          ReadFile(hPipe, &header, sizeof(header), &bytesRead, nullptr);
      if (!readOk || bytesRead != sizeof(header))
        break;

      // Process request
      PipeMessageHeader response;
      response.requestId = header.requestId;
      response.reserved = 0;

      switch (header.type) {
      case PipeMessageType::GetStatus: {
        response.type = PipeMessageType::StatusResponse;
        StatusPayload status = GetStatus();
        response.payloadSize = sizeof(status);

        WriteFile(hPipe, &response, sizeof(response), nullptr, nullptr);
        WriteFile(hPipe, &status, sizeof(status), nullptr, nullptr);
        break;
      }

      case PipeMessageType::StartBroadcast: {
        StartBroadcast();
        response.type = PipeMessageType::AckResponse;
        response.payloadSize = 0;
        WriteFile(hPipe, &response, sizeof(response), nullptr, nullptr);
        break;
      }

      case PipeMessageType::StopBroadcast: {
        StopBroadcast();
        response.type = PipeMessageType::AckResponse;
        response.payloadSize = 0;
        WriteFile(hPipe, &response, sizeof(response), nullptr, nullptr);
        break;
      }

      case PipeMessageType::SwitchTarget: {
        ToggleTarget();
        response.type = PipeMessageType::AckResponse;
        response.payloadSize = 0;
        WriteFile(hPipe, &response, sizeof(response), nullptr, nullptr);
        break;
      }

      default: {
        response.type = PipeMessageType::ErrorResponse;
        response.payloadSize = 0;
        WriteFile(hPipe, &response, sizeof(response), nullptr, nullptr);
        break;
      }
      }

      FlushFileBuffers(hPipe);
    }

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
  }
}

} // namespace CursorShare
