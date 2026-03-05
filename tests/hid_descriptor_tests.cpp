// =============================================================================
// CursorShare — HID Descriptor Tests
// =============================================================================

#include "../src/common/hid_descriptors.h"
#include <cassert>
#include <cstring>
#include <iostream>


using namespace CursorShare;

void TestKeyboardDescriptorSize() {
  // Keyboard descriptor should be non-empty and reasonable size
  assert(kKeyboardHidDescriptorSize > 0);
  assert(kKeyboardHidDescriptorSize < 256);

  // Check it starts with Usage Page (Generic Desktop)
  assert(kKeyboardHidDescriptor[0] == 0x05);
  assert(kKeyboardHidDescriptor[1] == 0x01);

  // Check Usage (Keyboard)
  assert(kKeyboardHidDescriptor[2] == 0x09);
  assert(kKeyboardHidDescriptor[3] == 0x06);

  std::cout << "[PASS] TestKeyboardDescriptorSize ("
            << kKeyboardHidDescriptorSize << " bytes)" << std::endl;
}

void TestMouseDescriptorSize() {
  assert(kMouseHidDescriptorSize > 0);
  assert(kMouseHidDescriptorSize < 256);

  // Starts with Usage Page (Generic Desktop)
  assert(kMouseHidDescriptor[0] == 0x05);
  assert(kMouseHidDescriptor[1] == 0x01);

  // Usage (Mouse)
  assert(kMouseHidDescriptor[2] == 0x09);
  assert(kMouseHidDescriptor[3] == 0x02);

  std::cout << "[PASS] TestMouseDescriptorSize (" << kMouseHidDescriptorSize
            << " bytes)" << std::endl;
}

void TestKeyboardReportEncoding() {
  uint8_t report[8];
  uint8_t keys[6] = {0x04, 0x05, 0x06, 0, 0, 0}; // A, B, C

  EncodeKeyboardReport(0x01, keys, report); // Left Ctrl

  assert(report[0] == 0x01); // Modifiers
  assert(report[1] == 0x00); // Reserved
  assert(report[2] == 0x04); // Key A
  assert(report[3] == 0x05); // Key B
  assert(report[4] == 0x06); // Key C
  assert(report[5] == 0x00);
  assert(report[6] == 0x00);
  assert(report[7] == 0x00);

  std::cout << "[PASS] TestKeyboardReportEncoding" << std::endl;
}

void TestMouseReportEncoding() {
  uint8_t report[7];

  EncodeMouseReport(0x01, 100, -50, 3, -1, report);

  assert(report[0] == 0x01); // Left button

  // X = 100 = 0x0064 (little-endian)
  assert(report[1] == 0x64);
  assert(report[2] == 0x00);

  // Y = -50 = 0xFFCE (little-endian)
  assert(report[3] == 0xCE);
  assert(report[4] == 0xFF);

  // Wheel = 3
  assert(report[5] == 0x03);

  // HWheel = -1
  assert(report[6] == 0xFF);

  std::cout << "[PASS] TestMouseReportEncoding" << std::endl;
}

void TestAllKeysUp() {
  uint8_t report[8];
  EncodeKeyboardAllUp(report);

  for (int i = 0; i < 8; ++i) {
    assert(report[i] == 0);
  }

  std::cout << "[PASS] TestAllKeysUp" << std::endl;
}

void TestMouseIdle() {
  uint8_t report[7];
  EncodeMouseIdle(report);

  for (int i = 0; i < 7; ++i) {
    assert(report[i] == 0);
  }

  std::cout << "[PASS] TestMouseIdle" << std::endl;
}

int main() {
  std::cout << "=== HID Descriptor Tests ===" << std::endl;
  TestKeyboardDescriptorSize();
  TestMouseDescriptorSize();
  TestKeyboardReportEncoding();
  TestMouseReportEncoding();
  TestAllKeysUp();
  TestMouseIdle();
  std::cout << "All HID descriptor tests passed!" << std::endl;
  return 0;
}
