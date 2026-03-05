// =============================================================================
// CursorShare — Shortcut Manager (Implementation)
// =============================================================================

#include "shortcut_manager.h"

namespace CursorShare {

ShortcutManager::ShortcutManager() = default;

ShortcutManager::~ShortcutManager() { Stop(); }

bool ShortcutManager::Start(const ShortcutConfig &config) {
  if (running_.load(std::memory_order_acquire))
    return false;

  config_ = config;
  shouldStop_.store(false, std::memory_order_release);

  thread_ = std::thread([this]() { HotkeyThread(); });

  int timeout = 100;
  while (!running_.load(std::memory_order_acquire) && timeout-- > 0) {
    Sleep(10);
  }

  return running_.load(std::memory_order_acquire);
}

void ShortcutManager::Stop() {
  if (!running_.load(std::memory_order_acquire))
    return;

  shouldStop_.store(true, std::memory_order_release);

  // Post a dummy message to unblock GetMessage
  // We use PostThreadMessage since we know the thread ID
  if (thread_.joinable()) {
    // GetMessage will return false on WM_QUIT
    // We need to get the thread ID — simplify by using a flag and timeout
    thread_.join();
  }

  running_.store(false, std::memory_order_release);
}

bool ShortcutManager::UpdateShortcut(UINT modifiers, UINT virtualKey) {
  // This requires re-registering on the hotkey thread.
  // For simplicity, stop and restart with new config.
  if (running_.load(std::memory_order_acquire)) {
    Stop();
    config_.modifiers = modifiers;
    config_.virtualKey = virtualKey;
    return Start(config_);
  }
  config_.modifiers = modifiers;
  config_.virtualKey = virtualKey;
  return true;
}

void ShortcutManager::HotkeyThread() {
  // Register the hotkey on this thread
  if (!RegisterHotKey(nullptr, config_.hotkeyId,
                      config_.modifiers | MOD_NOREPEAT, config_.virtualKey)) {
    // Failed to register
    return;
  }

  running_.store(true, std::memory_order_release);

  MSG msg;
  while (!shouldStop_.load(std::memory_order_acquire)) {
    // Use MsgWaitForMultipleObjects with a short timeout to allow checking
    // shouldStop_
    DWORD result = MsgWaitForMultipleObjects(0, nullptr, FALSE, 100,
                                             QS_HOTKEY | QS_POSTMESSAGE);

    if (result == WAIT_OBJECT_0) {
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_HOTKEY &&
            msg.wParam == static_cast<WPARAM>(config_.hotkeyId)) {
          if (callback_) {
            callback_();
          }
        }
        if (msg.message == WM_QUIT) {
          shouldStop_.store(true, std::memory_order_release);
          break;
        }
      }
    }
  }

  UnregisterHotKey(nullptr, config_.hotkeyId);
  running_.store(false, std::memory_order_release);
}

} // namespace CursorShare
