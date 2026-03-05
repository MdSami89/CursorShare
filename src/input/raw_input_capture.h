#pragma once
// =============================================================================
// CursorShare — Raw Input Capture
// User-mode input capture using Windows Raw Input API.
// This is the fallback mode when kernel filter drivers are not installed.
// =============================================================================

#include "../common/input_event.h"
#include "../common/ring_buffer.h"
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <windows.h>

namespace CursorShare {

/// Configuration for the Raw Input capture system.
struct RawInputConfig {
  bool captureKeyboard = true;
  bool captureMouse = true;
  bool exclusiveMode = false; // Suppress local input (RIDEV_NOLEGACY)
  bool backgroundCapture =
      true; // Capture even when not focused (RIDEV_INPUTSINK)
};

/// Callback type for input events.
using InputCallback = std::function<void(const InputEvent &)>;

/// Raw Input capture system — captures keyboard and mouse via WM_INPUT.
/// Runs on a dedicated message pump thread for lowest latency.
class RawInputCapture {
public:
  RawInputCapture();
  ~RawInputCapture();

  // Non-copyable, non-movable
  RawInputCapture(const RawInputCapture &) = delete;
  RawInputCapture &operator=(const RawInputCapture &) = delete;

  /// Start capturing input. Spawns a dedicated thread.
  bool Start(const RawInputConfig &config = {});

  /// Stop capturing input. Joins the message pump thread.
  void Stop();

  /// Is capture currently active?
  bool IsRunning() const { return running_.load(std::memory_order_acquire); }

  /// Set callback for incoming events (optional, alternative to ring buffer).
  void SetCallback(InputCallback callback) { callback_ = std::move(callback); }

  /// Enable/disable exclusive mode at runtime (suppresses host input).
  void SetExclusiveMode(bool exclusive);

  /// Is exclusive mode currently active?
  bool IsExclusive() const {
    return exclusiveActive_.load(std::memory_order_acquire);
  }

  /// Get the internal ring buffer for direct access.
  RingBuffer<InputEvent, 4096> &GetRingBuffer() { return ringBuffer_; }

  /// Get total events captured since last Start().
  uint64_t GetTotalEvents() const {
    return totalEvents_.load(std::memory_order_relaxed);
  }

private:
  void MessagePumpThread();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam);

  void ProcessRawInput(HRAWINPUT hRawInput);
  void ProcessKeyboard(const RAWKEYBOARD &kb);
  void ProcessMouse(const RAWMOUSE &mouse);

  bool RegisterDevices(HWND hwnd);
  void UnregisterDevices();

  RawInputConfig config_;
  InputCallback callback_;
  RingBuffer<InputEvent, 4096> ringBuffer_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> shouldStop_{false};
  std::atomic<uint64_t> totalEvents_{0};
  std::atomic<uint16_t> sequenceCounter_{0};

  HWND hwnd_ = nullptr;

  // Current mouse button state (accumulated)
  uint8_t mouseButtons_ = 0;

  // Low-level hooks for exclusive mode
  std::atomic<bool> exclusiveActive_{false};
  HHOOK keyboardHook_ = nullptr;
  HHOOK mouseHook_ = nullptr;

  void InstallHooks();
  void RemoveHooks();
  static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam,
                                               LPARAM lParam);
  static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam,
                                            LPARAM lParam);
};

} // namespace CursorShare
