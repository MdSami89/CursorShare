// =============================================================================
// CursorShare — Input Router (Implementation)
// =============================================================================

#include "input_router.h"
#include "../common/logger.h"
#include <algorithm>
#include <cstring>

namespace CursorShare {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
InputRouter::InputRouter() { std::memset(activeKeys_, 0, sizeof(activeKeys_)); }

// ---------------------------------------------------------------------------
// RouteEvent
// ---------------------------------------------------------------------------
void InputRouter::RouteEvent(const InputEvent &event) {
  RouteTarget target = target_.load(std::memory_order_acquire);

  if (target == RouteTarget::Host) {
    // Input stays on host — nothing to do (OS handles it normally)
    return;
  }

  if (!clientConnected_.load(std::memory_order_acquire)) {
    // No client connected — don't forward, silently drop
    // (Auto-fallback: input stays on host via OS default path)
    return;
  }

  // Track keyboard state for clean switching
  if (event.type == InputEventType::KeyDown) {
    // Track modifier keys (E0-E7 range)
    uint16_t vk = event.data.keyboard.virtualKey;
    if (vk >= 0xA0 && vk <= 0xA5) { // VK_LSHIFT..VK_RMENU
      // Map VK to modifier bit position
      int bit = -1;
      switch (vk) {
      case 0xA2:
        bit = 0;
        break; // VK_LCONTROL -> Left Ctrl
      case 0xA0:
        bit = 1;
        break; // VK_LSHIFT -> Left Shift
      case 0xA4:
        bit = 2;
        break; // VK_LMENU -> Left Alt
      case 0x5B:
        bit = 3;
        break; // VK_LWIN -> Left GUI
      case 0xA3:
        bit = 4;
        break; // VK_RCONTROL -> Right Ctrl
      case 0xA1:
        bit = 5;
        break; // VK_RSHIFT -> Right Shift
      case 0xA5:
        bit = 6;
        break; // VK_RMENU -> Right Alt
      case 0x5C:
        bit = 7;
        break; // VK_RWIN -> Right GUI
      }
      if (bit >= 0) {
        activeModifiers_ |= (1 << bit);
      }
    }

    // Track regular keys (up to 6KRO)
    if (activeKeyCount_ < 6) {
      uint8_t scanCode = static_cast<uint8_t>(event.data.keyboard.scanCode);
      // Check if key is already tracked
      bool found = false;
      for (int i = 0; i < activeKeyCount_; ++i) {
        if (activeKeys_[i] == scanCode) {
          found = true;
          break;
        }
      }
      if (!found) {
        activeKeys_[activeKeyCount_++] = scanCode;
      }
    }
  } else if (event.type == InputEventType::KeyUp) {
    uint16_t vk = event.data.keyboard.virtualKey;
    if (vk >= 0xA0 && vk <= 0xA5) {
      int bit = -1;
      switch (vk) {
      case 0xA2:
        bit = 0;
        break;
      case 0xA0:
        bit = 1;
        break;
      case 0xA4:
        bit = 2;
        break;
      case 0x5B:
        bit = 3;
        break;
      case 0xA3:
        bit = 4;
        break;
      case 0xA1:
        bit = 5;
        break;
      case 0xA5:
        bit = 6;
        break;
      case 0x5C:
        bit = 7;
        break;
      }
      if (bit >= 0) {
        activeModifiers_ &= ~(1 << bit);
      }
    }

    // Remove key from tracking
    uint8_t scanCode = static_cast<uint8_t>(event.data.keyboard.scanCode);
    for (int i = 0; i < activeKeyCount_; ++i) {
      if (activeKeys_[i] == scanCode) {
        // Shift remaining keys down
        for (int j = i; j < activeKeyCount_ - 1; ++j) {
          activeKeys_[j] = activeKeys_[j + 1];
        }
        activeKeys_[--activeKeyCount_] = 0;
        break;
      }
    }
  }

  // Forward event to Bluetooth
  EmitToClient(event);
}

// ---------------------------------------------------------------------------
// SwitchToHost
// ---------------------------------------------------------------------------
void InputRouter::SwitchToHost() {
  RouteTarget prev =
      target_.exchange(RouteTarget::Host, std::memory_order_acq_rel);

  if (prev == RouteTarget::Client) {
    // Flush state to prevent stuck keys on client
    FlushKeyboardState();
    FlushMouseState();
    LOG_INFO("Router", "Switched to HOST mode (flushed client state).");
  }
}

// ---------------------------------------------------------------------------
// SwitchToClient
// ---------------------------------------------------------------------------
void InputRouter::SwitchToClient() {
  if (!clientConnected_.load(std::memory_order_acquire)) {
    return; // Can't switch if no client
  }

  target_.store(RouteTarget::Client, std::memory_order_release);
  LOG_INFO("Router", "Switched to CLIENT mode.");
}

// ---------------------------------------------------------------------------
// Toggle
// ---------------------------------------------------------------------------
void InputRouter::Toggle() {
  RouteTarget current = target_.load(std::memory_order_acquire);
  if (current == RouteTarget::Host) {
    SwitchToClient();
  } else {
    SwitchToHost();
  }
}

// ---------------------------------------------------------------------------
// OnClientDisconnected
// ---------------------------------------------------------------------------
void InputRouter::OnClientDisconnected() {
  clientConnected_.store(false, std::memory_order_release);

  // Auto-fallback to host mode
  target_.store(RouteTarget::Host, std::memory_order_release);
  LOG_INFO("Router", "Client disconnected — auto-switched to HOST.");
}

// ---------------------------------------------------------------------------
// OnClientConnected
// ---------------------------------------------------------------------------
void InputRouter::OnClientConnected() {
  clientConnected_.store(true, std::memory_order_release);
  LOG_INFO("Router", "Client connected.");
}

// ---------------------------------------------------------------------------
// FlushKeyboardState
// ---------------------------------------------------------------------------
void InputRouter::FlushKeyboardState() {
  // Send "all keys up" event
  InputEvent event = {};
  event.type = InputEventType::KeyUp;
  event.timestamp = GetQPCTimestamp();
  event.data.keyboard.scanCode = 0;
  event.data.keyboard.virtualKey = 0;
  event.data.keyboard.flags = 0xFF; // Special flag: "all keys up"

  EmitToClient(event);

  // Reset tracked state
  activeModifiers_ = 0;
  std::memset(activeKeys_, 0, sizeof(activeKeys_));
  activeKeyCount_ = 0;
}

// ---------------------------------------------------------------------------
// FlushMouseState
// ---------------------------------------------------------------------------
void InputRouter::FlushMouseState() {
  // Send idle mouse event (no buttons, no movement)
  InputEvent event = {};
  event.type = InputEventType::MouseButtonUp;
  event.timestamp = GetQPCTimestamp();
  event.data.mouse.dx = 0;
  event.data.mouse.dy = 0;
  event.data.mouse.buttons = 0;
  event.data.mouse.wheelDelta = 0;
  event.data.mouse.hWheelDelta = 0;

  EmitToClient(event);
}

// ---------------------------------------------------------------------------
// EmitToClient
// ---------------------------------------------------------------------------
void InputRouter::EmitToClient(const InputEvent &event) {
  std::lock_guard<std::mutex> lock(callbackMutex_);
  if (clientCallback_) {
    clientCallback_(event);
  }
}

} // namespace CursorShare
