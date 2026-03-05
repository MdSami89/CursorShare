// =============================================================================
// CursorShare — Input Router Tests
// =============================================================================

#include "../src/input/input_router.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace CursorShare;

void TestDefaultHostMode() {
  InputRouter router;

  assert(router.GetTarget() == RouteTarget::Host);
  assert(!router.IsClientConnected());

  // Events in host mode shouldn't trigger callback
  bool callbackCalled = false;
  router.SetClientCallback([&](const InputEvent &) { callbackCalled = true; });

  InputEvent event = {};
  event.type = InputEventType::KeyDown;
  event.data.keyboard.scanCode = 0x1E;

  router.RouteEvent(event);
  assert(!callbackCalled);

  std::cout << "[PASS] TestDefaultHostMode" << std::endl;
}

void TestSwitchToClient() {
  InputRouter router;

  // Can't switch without client connected
  router.SwitchToClient();
  assert(router.GetTarget() == RouteTarget::Host);

  // Connect client
  router.OnClientConnected();
  assert(router.IsClientConnected());

  // Now switch should work
  router.SwitchToClient();
  assert(router.GetTarget() == RouteTarget::Client);

  // Events should trigger callback
  std::vector<InputEvent> received;
  router.SetClientCallback([&](const InputEvent &e) { received.push_back(e); });

  InputEvent event = {};
  event.type = InputEventType::MouseMove;
  event.data.mouse.dx = 42;

  router.RouteEvent(event);
  assert(!received.empty());
  assert(received.back().data.mouse.dx == 42);

  std::cout << "[PASS] TestSwitchToClient" << std::endl;
}

void TestAutoFallback() {
  InputRouter router;

  router.OnClientConnected();
  router.SwitchToClient();
  assert(router.GetTarget() == RouteTarget::Client);

  // Disconnect client — should auto-fallback to host
  router.OnClientDisconnected();
  assert(router.GetTarget() == RouteTarget::Host);
  assert(!router.IsClientConnected());

  std::cout << "[PASS] TestAutoFallback" << std::endl;
}

void TestToggle() {
  InputRouter router;

  router.OnClientConnected();

  // Start at host
  assert(router.GetTarget() == RouteTarget::Host);

  // Toggle to client
  router.Toggle();
  assert(router.GetTarget() == RouteTarget::Client);

  // Toggle back to host
  router.Toggle();
  assert(router.GetTarget() == RouteTarget::Host);

  std::cout << "[PASS] TestToggle" << std::endl;
}

void TestStateFlushing() {
  InputRouter router;

  // Track flush events
  std::vector<InputEvent> flushed;
  router.SetClientCallback([&](const InputEvent &e) { flushed.push_back(e); });

  router.OnClientConnected();
  router.SwitchToClient();

  // Send some key events
  InputEvent keyDown = {};
  keyDown.type = InputEventType::KeyDown;
  keyDown.data.keyboard.scanCode = 0x1E;
  keyDown.data.keyboard.virtualKey = 0x41; // 'A'
  router.RouteEvent(keyDown);

  flushed.clear();

  // Switch back to host — should flush
  router.SwitchToHost();

  // Should have received flush events (all-keys-up + mouse idle)
  assert(flushed.size() >= 2);

  // Check for all-keys-up (flags == 0xFF)
  bool foundAllUp = false;
  for (const auto &e : flushed) {
    if (e.type == InputEventType::KeyUp && e.data.keyboard.flags == 0xFF) {
      foundAllUp = true;
      break;
    }
  }
  assert(foundAllUp);

  std::cout << "[PASS] TestStateFlushing" << std::endl;
}

int main() {
  std::cout << "=== Input Router Tests ===" << std::endl;
  TestDefaultHostMode();
  TestSwitchToClient();
  TestAutoFallback();
  TestToggle();
  TestStateFlushing();
  std::cout << "All input router tests passed!" << std::endl;
  return 0;
}
