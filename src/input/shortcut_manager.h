#pragma once
// =============================================================================
// CursorShare — Shortcut Manager
// Configurable global shortcut for instant routing switch.
// =============================================================================

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <windows.h>


namespace CursorShare {

/// Global shortcut configuration.
struct ShortcutConfig {
  UINT modifiers = MOD_CONTROL | MOD_ALT; // Ctrl+Alt
  UINT virtualKey = 'S';                  // + S key
  int hotkeyId = 1;                       // RegisterHotKey ID
};

/// Callback when shortcut is triggered.
using ShortcutCallback = std::function<void()>;

/// Global shortcut manager — listens for hotkeys system-wide.
class ShortcutManager {
public:
  ShortcutManager();
  ~ShortcutManager();

  ShortcutManager(const ShortcutManager &) = delete;
  ShortcutManager &operator=(const ShortcutManager &) = delete;

  /// Start listening for the configured shortcut.
  bool Start(const ShortcutConfig &config = {});

  /// Stop listening.
  void Stop();

  bool IsRunning() const { return running_.load(std::memory_order_acquire); }

  /// Set the callback invoked when the shortcut is pressed.
  void SetCallback(ShortcutCallback callback) {
    callback_ = std::move(callback);
  }

  /// Update the shortcut key while running.
  bool UpdateShortcut(UINT modifiers, UINT virtualKey);

private:
  void HotkeyThread();

  ShortcutConfig config_;
  ShortcutCallback callback_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> shouldStop_{false};
};

} // namespace CursorShare
