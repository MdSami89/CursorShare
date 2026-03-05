// =============================================================================
// CursorShare — Main Entry Point
// Console application for the CursorShare service.
//
// Copyright (c) 2026 Mohammad Sami (MdSami89)
// GitHub: https://github.com/MdSami89/CursorShare
//
// Licensed under the CursorShare Custom License (CSCL v1.0).
// Commercial use without written permission is prohibited.
// See LICENSE file for full terms.
// =============================================================================

#include "bluetooth/bt_validator.h"
#include "common/constants.h"
#include "common/logger.h"
#include "common/watermark.h"
#include "service/cursorshare_service.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <windows.h>

using namespace CursorShare;

static std::atomic<bool> g_shouldExit{false};
static CursorShareService *g_service = nullptr;

// Signal handler for clean shutdown
BOOL WINAPI ConsoleHandler(DWORD ctrlType) {
  switch (ctrlType) {
  case CTRL_C_EVENT:
  case CTRL_CLOSE_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_LOGOFF_EVENT:
  case CTRL_SHUTDOWN_EVENT:
    LOG_INFO("Main", "Shutting down (signal %lu)...", ctrlType);
    std::cout << "\n[CursorShare] Shutting down..." << std::endl;
    g_shouldExit.store(true, std::memory_order_release);
    if (g_service) {
      g_service->Shutdown();
    }
    return TRUE;
  }
  return FALSE;
}

void PrintBanner() {
  std::cout << R"(
   ____                           ____  _
  / ___|   _ _ __ ___  ___  _ __/ ___|| |__   __ _ _ __ ___
 | |  | | | | '__/ __|/ _ \| '__\___ \| '_ \ / _` | '__/ _ \
 | |__| |_| | |  \__ \ (_) | |   ___) | | | | (_| | | |  __/
  \____\__,_|_|  |___/\___/|_|  |____/|_| |_|\__,_|_|  \___|
)" << std::endl;
  std::cout << "  Kernel-Assisted Bluetooth HID Redirect System" << std::endl;
  std::cout << "  Version " << kAppVersion << std::endl;
  std::cout << "  ================================================"
            << std::endl;
  std::cout << std::endl;
}

void PrintUsage() {
  std::cout << "Commands:" << std::endl;
  std::cout << "  start     - Start broadcasting (enable BT Classic HID)"
            << std::endl;
  std::cout << "  ble       - Start BLE HID broadcasting (recommended)"
            << std::endl;
  std::cout << "  stop      - Stop broadcasting" << std::endl;
  std::cout << "  switch    - Toggle input routing (Host <-> Client)"
            << std::endl;
  std::cout << "  status    - Show current status" << std::endl;
  std::cout << "  diag      - Run Bluetooth diagnostics" << std::endl;
  std::cout << "  latency   - Show latency statistics" << std::endl;
  std::cout << "  devices   - List connected devices" << std::endl;
  std::cout << "  paired    - List paired devices" << std::endl;
  std::cout << "  help      - Show this help" << std::endl;
  std::cout << "  quit      - Shutdown and exit" << std::endl;
  std::cout << std::endl;
  std::cout << "Global shortcut: Ctrl+Alt+S (toggle input routing)"
            << std::endl;
  std::cout << std::endl;
}

void PrintStatus(const CursorShareService &service) {
  auto status = service.GetStatus();

  std::cout << "\n--- CursorShare Status ---" << std::endl;

  const char *stateStr = "Unknown";
  switch (status.broadcastState) {
  case BroadcastState::Stopped:
    stateStr = "Stopped";
    break;
  case BroadcastState::Starting:
    stateStr = "Starting";
    break;
  case BroadcastState::Running:
    stateStr = "Running";
    break;
  case BroadcastState::Stopping:
    stateStr = "Stopping";
    break;
  case BroadcastState::Error:
    stateStr = "Error";
    break;
  }
  std::cout << "  Broadcast:       " << stateStr << std::endl;
  std::cout << "  Target:          "
            << (status.targetMode == TargetMode::Host ? "HOST" : "CLIENT")
            << std::endl;
  std::cout << "  Input Mode:      "
            << (status.inputMode == InputMode::RawInput ? "Raw Input API"
                                                        : "Kernel Driver")
            << std::endl;
  std::cout << "  Connected:       "
            << static_cast<int>(status.connectedDeviceCount) << " device(s)"
            << std::endl;
  std::cout << "  BT Adapter:      "
            << (status.btAdapterPresent ? "Present" : "Not Found") << std::endl;
  std::cout << "  Driver:          "
            << (status.driverInstalled ? "Installed" : "Not Installed")
            << std::endl;

  // BLE HID status
  auto bleState = service.GetBleState();
  const char *bleStr = "OFF";
  switch (bleState) {
  case BleHidState::Advertising:
    bleStr = "Advertising";
    break;
  case BleHidState::Connected:
    bleStr = "Connected";
    break;
  case BleHidState::Error:
    bleStr = "Error";
    break;
  default:
    break;
  }
  std::cout << "  BLE HID:         " << bleStr;
  if (bleState == BleHidState::Connected) {
    std::cout << " (" << service.GetBleConnectionCount() << " client(s))";
  }
  std::cout << std::endl;
  std::cout << std::endl;
}

void PrintLatency(const CursorShareService &service) {
  std::cout << "\n--- Latency Statistics ---" << std::endl;

  const char *stageNames[] = {
      "Input Capture   ", "Routing Decision", "HID Encoding    ",
      "BT Transmit     ", "End-to-End      ",
  };

  std::cout << "  Stage              | Min (us) | Avg (us) | P99 (us) | Max "
               "(us) | Samples"
            << std::endl;
  std::cout << "  "
               "-------------------|----------|----------|----------|----------"
               "|--------"
            << std::endl;

  for (int i = 0; i < static_cast<int>(PipelineStage::StageCount); ++i) {
    auto stats = service.GetLatencyStats(static_cast<PipelineStage>(i));
    if (stats.sampleCount > 0) {
      printf("  %s | %8.1f | %8.1f | %8.1f | %8.1f | %llu\n", stageNames[i],
             stats.minUs, stats.avgUs, stats.p99Us, stats.maxUs,
             stats.sampleCount);
    } else {
      printf("  %s |      --- |      --- |      --- |      --- |       0\n",
             stageNames[i]);
    }
  }
  std::cout << std::endl;
}

int main(int argc, char *argv[]) {
  // Set console for UTF-8
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(ConsoleHandler, TRUE);

  // Initialize logger first
  CursorShare::Logger::Instance().Init("", CursorShare::LogLevel::Trace, true);
  LOG_INFO("Main", "Application starting...");

  PrintBanner();

  // Create service
  CursorShareService service;
  g_service = &service;

  // Initialize
  std::cout << "[CursorShare] Initializing..." << std::endl;

  ServiceConfig config;
  config.inputMode = InputMode::RawInput;
  config.exclusiveMode = false;
  config.defaultDisplay = {1920, 1080, true};

  if (!service.Initialize(config)) {
    LOG_FATAL("Main", "Initialization failed! Check Bluetooth adapter.");
    std::cerr << "[CursorShare] Initialization failed!" << std::endl;
    std::cerr << "Please check Bluetooth adapter availability." << std::endl;
    CursorShare::Logger::Instance().Shutdown();
    return 1;
  }

  std::cout << "[CursorShare] Initialized successfully." << std::endl;
  PrintUsage();

  // Interactive command loop
  std::string line;
  while (!g_shouldExit.load(std::memory_order_acquire)) {
    std::cout << "CursorShare> ";
    if (!std::getline(std::cin, line))
      break;

    // Trim whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
      continue;
    line = line.substr(start);

    if (line == "start") {
      std::cout << "[CursorShare] Starting broadcast..." << std::endl;
      if (service.StartBroadcast()) {
        std::cout
            << "[CursorShare] Broadcast started. Waiting for connections..."
            << std::endl;
      } else {
        LOG_ERROR("Main", "Failed to start broadcast.");
        std::cerr << "[CursorShare] Failed to start broadcast." << std::endl;
      }
    } else if (line == "ble") {
      std::cout << "[CursorShare] Starting BLE HID broadcast..." << std::endl;
      if (service.StartBleBroadcast()) {
        std::cout
            << "[CursorShare] BLE HID advertising. Pair from your device's"
            << " Bluetooth settings." << std::endl;
      } else {
        LOG_ERROR("Main", "Failed to start BLE HID. Ensure BT adapter supports "
                          "peripheral role.");
        std::cerr << "[CursorShare] Failed to start BLE HID." << std::endl;
        std::cerr << "  Ensure your BT adapter supports peripheral role."
                  << std::endl;
      }
    } else if (line == "stop") {
      std::cout << "[CursorShare] Stopping broadcast..." << std::endl;
      service.StopBroadcast();
      std::cout << "[CursorShare] Broadcast stopped." << std::endl;
    } else if (line == "switch") {
      service.ToggleTarget();
      auto status = service.GetStatus();
      std::cout << "[CursorShare] Target: "
                << (status.targetMode == TargetMode::Host ? "HOST" : "CLIENT")
                << std::endl;
    } else if (line == "status") {
      PrintStatus(service);
    } else if (line == "diag") {
      auto diag = BluetoothValidator::Validate();
      std::cout << diag.GetSummary() << std::endl;
    } else if (line == "latency") {
      PrintLatency(service);
    } else if (line == "devices") {
      auto devices = service.GetConnectedDevices();
      std::cout << "\n--- Connected Devices ---" << std::endl;
      if (devices.empty()) {
        std::cout << "  (none)" << std::endl;
      }
      for (const auto &dev : devices) {
        std::cout << "  " << dev.name << " [" << std::hex << dev.address
                  << std::dec << "]" << std::endl;
      }
      std::cout << std::endl;
    } else if (line == "paired") {
      auto devices = service.GetPairedDevices();
      std::cout << "\n--- Paired Devices ---" << std::endl;
      if (devices.empty()) {
        std::cout << "  (none)" << std::endl;
      }
      for (const auto &dev : devices) {
        std::cout << "  " << dev.name << (dev.connected ? " [Connected]" : "")
                  << (dev.authenticated ? " [Authenticated]" : "") << std::endl;
      }
      std::cout << std::endl;
    } else if (line == "help") {
      PrintUsage();
    } else if (line == "quit" || line == "exit") {
      break;
    } else if (!line.empty()) {
      std::cout << "Unknown command: " << line << ". Type 'help' for commands."
                << std::endl;
    }
  }

  // Clean shutdown
  LOG_INFO("Main", "Normal shutdown initiated.");
  std::cout << "[CursorShare] Shutting down..." << std::endl;
  service.Shutdown();
  std::cout << "[CursorShare] Shutdown complete. Goodbye!" << std::endl;

  CursorShare::Logger::Instance().Shutdown();
  g_service = nullptr;
  return 0;
}
