#pragma once
// =============================================================================
// CursorShare — Input Router
// Routes input events between host (local) and client (Bluetooth HID).
// =============================================================================

#include "../common/input_event.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>


namespace CursorShare {

enum class RouteTarget : uint8_t {
  Host = 0,   // Input stays on local machine
  Client = 1, // Input is forwarded over Bluetooth
};

/// Callback type for events routed to client.
using ClientOutputCallback = std::function<void(const InputEvent &)>;

/// Input Router — decides where input events go.
class InputRouter {
public:
  InputRouter();
  ~InputRouter() = default;

  /// Process an incoming input event and route it.
  void RouteEvent(const InputEvent &event);

  /// Switch to host mode (stop forwarding to BT).
  void SwitchToHost();

  /// Switch to client mode (forward to BT).
  void SwitchToClient();

  /// Toggle between host and client.
  void Toggle();

  /// Get current target.
  RouteTarget GetTarget() const {
    return target_.load(std::memory_order_acquire);
  }

  /// Set callback for events routed to client (Bluetooth).
  void SetClientCallback(ClientOutputCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    clientCallback_ = std::move(callback);
  }

  /// Notify router that Bluetooth client disconnected.
  void OnClientDisconnected();

  /// Notify router that Bluetooth client connected.
  void OnClientConnected();

  /// Is a client currently connected?
  bool IsClientConnected() const {
    return clientConnected_.load(std::memory_order_acquire);
  }

private:
  /// Send "all keys up" keyboard report to client.
  void FlushKeyboardState();

  /// Send idle mouse report to client.
  void FlushMouseState();

  /// Emit event to client callback.
  void EmitToClient(const InputEvent &event);

  std::atomic<RouteTarget> target_{RouteTarget::Host};
  std::atomic<bool> clientConnected_{false};

  std::mutex callbackMutex_;
  ClientOutputCallback clientCallback_;

  // Tracked keyboard state for flush
  uint8_t activeModifiers_ = 0;
  uint8_t activeKeys_[6] = {};
  int activeKeyCount_ = 0;
};

} // namespace CursorShare
