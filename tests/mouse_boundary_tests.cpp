// =============================================================================
// CursorShare — Mouse Boundary Tests
// =============================================================================

#include "../src/input/mouse_boundary.h"
#include <cassert>
#include <cmath>
#include <iostream>


using namespace CursorShare;

void TestBasicClamping() {
  MouseBoundary mb;
  mb.SetClientDisplay({100, 100, true});
  mb.ResetToCenter(); // Position: (50, 50)

  int16_t dxOut, dyOut;

  // Normal movement within bounds
  mb.ApplyMovement(10, 10, dxOut, dyOut);
  assert(dxOut == 10 && dyOut == 10);

  // Check position
  int32_t x, y;
  mb.GetPosition(x, y);
  assert(x == 60 && y == 60);

  std::cout << "[PASS] TestBasicClamping" << std::endl;
}

void TestEdgeClamping() {
  MouseBoundary mb;
  mb.SetClientDisplay({100, 100, true});
  mb.SetDeadZone(0); // Disable dead zone for this test

  // Start at bottom-right corner
  int16_t dxOut, dyOut;

  // Move far to the right (should clamp)
  mb.ApplyMovement(10000, 10000, dxOut, dyOut);

  int32_t x, y;
  mb.GetPosition(x, y);
  assert(x == 99 && y == 99); // Clamped to max

  // Try moving further right (should produce 0 delta)
  mb.ApplyMovement(100, 100, dxOut, dyOut);
  assert(dxOut == 0 && dyOut == 0);

  // Move left (should work)
  mb.ApplyMovement(-50, -50, dxOut, dyOut);
  assert(dxOut == -50 && dyOut == -50);

  mb.GetPosition(x, y);
  assert(x == 49 && y == 49);

  std::cout << "[PASS] TestEdgeClamping" << std::endl;
}

void TestResolutionChange() {
  MouseBoundary mb;
  mb.SetClientDisplay({1920, 1080, true});
  mb.SetDeadZone(0);

  // Move to (1900, 1050)
  int16_t dxOut, dyOut;
  mb.ApplyMovement(10000, 10000, dxOut, dyOut);

  // Change to smaller resolution
  mb.SetClientDisplay({800, 600, true});

  // Position should be clamped to new bounds
  int32_t x, y;
  mb.GetPosition(x, y);
  assert(x <= 799 && y <= 599);

  std::cout << "[PASS] TestResolutionChange" << std::endl;
}

void TestOrientationChange() {
  MouseBoundary mb;
  mb.SetClientDisplay({1920, 1080, true});
  mb.ResetToCenter(); // (960, 540)

  // Switch to portrait
  mb.SetOrientation(false);

  auto display = mb.GetClientDisplay();
  assert(display.width == 1080);
  assert(display.height == 1920);
  assert(!display.landscape);

  // Switch back to landscape
  mb.SetOrientation(true);

  display = mb.GetClientDisplay();
  assert(display.width == 1920);
  assert(display.height == 1080);
  assert(display.landscape);

  std::cout << "[PASS] TestOrientationChange" << std::endl;
}

void TestNegativeMovement() {
  MouseBoundary mb;
  mb.SetClientDisplay({100, 100, true});
  mb.SetDeadZone(0);

  // Start at origin (move far negative)
  int16_t dxOut, dyOut;
  mb.ApplyMovement(-10000, -10000, dxOut, dyOut);

  int32_t x, y;
  mb.GetPosition(x, y);
  assert(x == 0 && y == 0);

  // Further negative movement should produce 0 delta
  mb.ApplyMovement(-10, -10, dxOut, dyOut);
  assert(dxOut == 0 && dyOut == 0);

  std::cout << "[PASS] TestNegativeMovement" << std::endl;
}

int main() {
  std::cout << "=== Mouse Boundary Tests ===" << std::endl;
  TestBasicClamping();
  TestEdgeClamping();
  TestResolutionChange();
  TestOrientationChange();
  TestNegativeMovement();
  std::cout << "All mouse boundary tests passed!" << std::endl;
  return 0;
}
