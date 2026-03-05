// =============================================================================
// CursorShare — Bluetooth Diagnostic Tool
// Standalone tool to validate Bluetooth adapter compatibility.
// =============================================================================

#include "../src/bluetooth/bt_validator.h"
#include "../src/common/logger.h"
#include <iostream>
#include <windows.h>

int main() {
  SetConsoleOutputCP(CP_UTF8);
  CursorShare::Logger::Instance().Init("", CursorShare::LogLevel::Trace, true);
  LOG_INFO("Diagnostic", "Bluetooth Diagnostic Tool starting...");

  std::cout << "CursorShare Bluetooth Diagnostic Tool" << std::endl;
  std::cout << "======================================" << std::endl;
  std::cout << std::endl;

  auto result = CursorShare::BluetoothValidator::Validate();
  std::cout << result.GetSummary() << std::endl;

  if (result.allPassed) {
    std::cout << "Result: Your system meets all Bluetooth requirements for "
                 "CursorShare."
              << std::endl;
  } else {
    std::cout << "Result: Some checks failed. Please resolve the issues above."
              << std::endl;
  }

  return result.allPassed ? 0 : 1;
}
