// =============================================================================
// CursorShare — Raw Input Capture (Implementation)
// High-performance input capture via Windows Raw Input API.
// =============================================================================

#include "raw_input_capture.h"
#include "../common/logger.h"
#include <hidusage.h>
#include <vector>

namespace CursorShare {

// Window class name for the hidden message-only window
static const wchar_t *kWindowClassName = L"CursorShareRawInputSink";

// Custom window messages to marshal hook install/remove to message pump thread
static constexpr UINT WM_INSTALL_HOOKS = WM_APP + 1;
static constexpr UINT WM_REMOVE_HOOKS = WM_APP + 2;

// Store 'this' pointer for the static WndProc and LL hook callbacks.
// NOT thread_local — LL hooks are called from the OS on the thread that
// installed them, and we need them to find the instance from any callsite.
static RawInputCapture *g_instance = nullptr;

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
RawInputCapture::RawInputCapture() = default;

RawInputCapture::~RawInputCapture() { Stop(); }

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------
bool RawInputCapture::Start(const RawInputConfig &config) {
  if (running_.load(std::memory_order_acquire)) {
    return false; // Already running
  }

  config_ = config;
  shouldStop_.store(false, std::memory_order_release);
  totalEvents_.store(0, std::memory_order_relaxed);
  sequenceCounter_.store(0, std::memory_order_relaxed);
  ringBuffer_.Reset();

  thread_ = std::thread([this]() { MessagePumpThread(); });

  // Wait for the thread to become ready
  int timeout = 100; // ~1 second max
  while (!running_.load(std::memory_order_acquire) && timeout-- > 0) {
    Sleep(10);
  }

  bool ok = running_.load(std::memory_order_acquire);
  if (ok) {
    LOG_INFO("Input", "Raw Input capture started (kbd=%s mouse=%s bg=%s)",
             config.captureKeyboard ? "yes" : "no",
             config.captureMouse ? "yes" : "no",
             config.backgroundCapture ? "yes" : "no");
  } else {
    LOG_ERROR("Input", "Raw Input capture failed to start.");
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Stop
// ---------------------------------------------------------------------------
void RawInputCapture::Stop() {
  if (!running_.load(std::memory_order_acquire))
    return;

  shouldStop_.store(true, std::memory_order_release);

  // Remove hooks if active
  RemoveHooks();

  // Post WM_QUIT to the message pump
  if (hwnd_) {
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  running_.store(false, std::memory_order_release);
  LOG_INFO("Input", "Raw Input capture stopped.");
}

// ---------------------------------------------------------------------------
// Message Pump Thread
// ---------------------------------------------------------------------------
void RawInputCapture::MessagePumpThread() {
  g_instance = this;

  // Boost thread priority for low latency
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

  // Register window class
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kWindowClassName;
  RegisterClassExW(&wc);

  // Create message-only window (HWND_MESSAGE parent)
  hwnd_ = CreateWindowExW(0, kWindowClassName, L"CursorShare RawInput", 0, 0, 0,
                          0, 0, HWND_MESSAGE, nullptr,
                          GetModuleHandleW(nullptr), nullptr);

  if (!hwnd_) {
    return;
  }

  // Register raw input devices
  if (!RegisterDevices(hwnd_)) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    return;
  }

  running_.store(true, std::memory_order_release);

  // Message pump
  MSG msg;
  while (!shouldStop_.load(std::memory_order_acquire)) {
    BOOL result = GetMessageW(&msg, nullptr, 0, 0);
    if (result == 0 || result == -1)
      break;

    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  // Cleanup
  UnregisterDevices();
  DestroyWindow(hwnd_);
  hwnd_ = nullptr;
  UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));

  running_.store(false, std::memory_order_release);
  g_instance = nullptr;
}

// ---------------------------------------------------------------------------
// WndProc (static)
// ---------------------------------------------------------------------------
LRESULT CALLBACK RawInputCapture::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam) {
  switch (msg) {
  case WM_INPUT: {
    if (g_instance) {
      g_instance->ProcessRawInput(reinterpret_cast<HRAWINPUT>(lParam));
    }
    return 0;
  }
  case WM_INSTALL_HOOKS:
    if (g_instance) {
      g_instance->InstallHooks();
    }
    return 0;
  case WM_REMOVE_HOOKS:
    if (g_instance) {
      g_instance->RemoveHooks();
    }
    return 0;
  case WM_CLOSE:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

// ---------------------------------------------------------------------------
// RegisterDevices
// ---------------------------------------------------------------------------
bool RawInputCapture::RegisterDevices(HWND hwnd) {
  std::vector<RAWINPUTDEVICE> devices;

  if (config_.captureKeyboard) {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid.usUsage = HID_USAGE_GENERIC_KEYBOARD;
    rid.dwFlags = 0;
    if (config_.backgroundCapture)
      rid.dwFlags |= RIDEV_INPUTSINK;
    if (config_.exclusiveMode)
      rid.dwFlags |= RIDEV_NOLEGACY;
    rid.hwndTarget = hwnd;
    devices.push_back(rid);
  }

  if (config_.captureMouse) {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid.usUsage = HID_USAGE_GENERIC_MOUSE;
    rid.dwFlags = 0;
    if (config_.backgroundCapture)
      rid.dwFlags |= RIDEV_INPUTSINK;
    if (config_.exclusiveMode)
      rid.dwFlags |= RIDEV_NOLEGACY;
    rid.hwndTarget = hwnd;
    devices.push_back(rid);
  }

  if (devices.empty())
    return false;

  return RegisterRawInputDevices(devices.data(),
                                 static_cast<UINT>(devices.size()),
                                 sizeof(RAWINPUTDEVICE)) != FALSE;
}

// ---------------------------------------------------------------------------
// UnregisterDevices
// ---------------------------------------------------------------------------
void RawInputCapture::UnregisterDevices() {
  std::vector<RAWINPUTDEVICE> devices;

  if (config_.captureKeyboard) {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid.usUsage = HID_USAGE_GENERIC_KEYBOARD;
    rid.dwFlags = RIDEV_REMOVE;
    rid.hwndTarget = nullptr;
    devices.push_back(rid);
  }

  if (config_.captureMouse) {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid.usUsage = HID_USAGE_GENERIC_MOUSE;
    rid.dwFlags = RIDEV_REMOVE;
    rid.hwndTarget = nullptr;
    devices.push_back(rid);
  }

  if (!devices.empty()) {
    RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()),
                            sizeof(RAWINPUTDEVICE));
  }
}

// ---------------------------------------------------------------------------
// ProcessRawInput
// ---------------------------------------------------------------------------
void RawInputCapture::ProcessRawInput(HRAWINPUT hRawInput) {
  UINT size = 0;
  GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
  if (size == 0)
    return;

  // Use stack buffer for small inputs (typical case)
  alignas(8) uint8_t stackBuffer[256];
  uint8_t *buffer = stackBuffer;
  std::vector<uint8_t> heapBuffer;

  if (size > sizeof(stackBuffer)) {
    heapBuffer.resize(size);
    buffer = heapBuffer.data();
  }

  if (GetRawInputData(hRawInput, RID_INPUT, buffer, &size,
                      sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
    return;
  }

  RAWINPUT *raw = reinterpret_cast<RAWINPUT *>(buffer);

  switch (raw->header.dwType) {
  case RIM_TYPEKEYBOARD:
    ProcessKeyboard(raw->data.keyboard);
    break;
  case RIM_TYPEMOUSE:
    ProcessMouse(raw->data.mouse);
    break;
  }
}

// ---------------------------------------------------------------------------
// ProcessKeyboard
// ---------------------------------------------------------------------------
void RawInputCapture::ProcessKeyboard(const RAWKEYBOARD &kb) {
  // In exclusive mode, keyboard events are captured directly by the LL hook
  // callback to avoid the WM_INPUT suppression issue. Skip here to prevent
  // duplicate events.
  if (exclusiveActive_.load(std::memory_order_acquire)) {
    return;
  }

  InputEvent event = {};
  event.timestamp = GetQPCTimestamp();
  event.sequence = sequenceCounter_.fetch_add(1, std::memory_order_relaxed);

  // Determine key up/down
  bool isKeyUp = (kb.Flags & RI_KEY_BREAK) != 0;
  event.type = isKeyUp ? InputEventType::KeyUp : InputEventType::KeyDown;

  event.data.keyboard.scanCode = kb.MakeCode;
  event.data.keyboard.virtualKey = kb.VKey;
  event.data.keyboard.flags = 0;

  // E0/E1 prefix flags
  if (kb.Flags & RI_KEY_E0)
    event.data.keyboard.flags |= 0x01;
  if (kb.Flags & RI_KEY_E1)
    event.data.keyboard.flags |= 0x02;

  // Enqueue to ring buffer (never blocks)
  ringBuffer_.TryPush(event);
  totalEvents_.fetch_add(1, std::memory_order_relaxed);

  // Invoke callback if set
  if (callback_) {
    callback_(event);
  }
}

// ---------------------------------------------------------------------------
// ProcessMouse
// ---------------------------------------------------------------------------
void RawInputCapture::ProcessMouse(const RAWMOUSE &mouse) {
  int64_t timestamp = GetQPCTimestamp();
  uint16_t seq = sequenceCounter_.fetch_add(1, std::memory_order_relaxed);

  // --- Button state changes ---
  if (mouse.usButtonFlags != 0) {
    // Update accumulated button state
    if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
      mouseButtons_ |= static_cast<uint8_t>(MouseButton::Left);
    if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
      mouseButtons_ &= ~static_cast<uint8_t>(MouseButton::Left);
    if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
      mouseButtons_ |= static_cast<uint8_t>(MouseButton::Right);
    if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
      mouseButtons_ &= ~static_cast<uint8_t>(MouseButton::Right);
    if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
      mouseButtons_ |= static_cast<uint8_t>(MouseButton::Middle);
    if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
      mouseButtons_ &= ~static_cast<uint8_t>(MouseButton::Middle);
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN)
      mouseButtons_ |= static_cast<uint8_t>(MouseButton::X1);
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP)
      mouseButtons_ &= ~static_cast<uint8_t>(MouseButton::X1);
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN)
      mouseButtons_ |= static_cast<uint8_t>(MouseButton::X2);
    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP)
      mouseButtons_ &= ~static_cast<uint8_t>(MouseButton::X2);

    // Emit button events
    // We check each button flag pair and emit individual events
    auto emitButton = [&](USHORT downFlag, USHORT upFlag) {
      if (mouse.usButtonFlags & downFlag) {
        InputEvent event = {};
        event.type = InputEventType::MouseButtonDown;
        event.timestamp = timestamp;
        event.sequence = seq;
        event.data.mouse.buttons = mouseButtons_;
        ringBuffer_.TryPush(event);
        totalEvents_.fetch_add(1, std::memory_order_relaxed);
        if (callback_)
          callback_(event);
      }
      if (mouse.usButtonFlags & upFlag) {
        InputEvent event = {};
        event.type = InputEventType::MouseButtonUp;
        event.timestamp = timestamp;
        event.sequence = seq;
        event.data.mouse.buttons = mouseButtons_;
        ringBuffer_.TryPush(event);
        totalEvents_.fetch_add(1, std::memory_order_relaxed);
        if (callback_)
          callback_(event);
      }
    };

    emitButton(RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP);
    emitButton(RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP);
    emitButton(RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP);
    emitButton(RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP);
    emitButton(RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP);
  }

  // --- Mouse movement ---
  if (mouse.lLastX != 0 || mouse.lLastY != 0) {
    // Only handle relative mouse movement
    if (!(mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
      InputEvent event = {};
      event.type = InputEventType::MouseMove;
      event.timestamp = timestamp;
      event.sequence = seq;
      event.data.mouse.dx =
          static_cast<int16_t>((mouse.lLastX > 32767)    ? 32767
                               : (mouse.lLastX < -32767) ? -32767
                                                         : mouse.lLastX);
      event.data.mouse.dy =
          static_cast<int16_t>((mouse.lLastY > 32767)    ? 32767
                               : (mouse.lLastY < -32767) ? -32767
                                                         : mouse.lLastY);
      event.data.mouse.buttons = mouseButtons_;

      ringBuffer_.TryPush(event);
      totalEvents_.fetch_add(1, std::memory_order_relaxed);
      if (callback_)
        callback_(event);
    }
  }

  // --- Scroll wheel ---
  if (mouse.usButtonFlags & RI_MOUSE_WHEEL) {
    InputEvent event = {};
    event.type = InputEventType::MouseWheel;
    event.timestamp = timestamp;
    event.sequence = seq;
    event.data.mouse.wheelDelta =
        static_cast<int16_t>(static_cast<SHORT>(mouse.usButtonData));
    event.data.mouse.buttons = mouseButtons_;

    ringBuffer_.TryPush(event);
    totalEvents_.fetch_add(1, std::memory_order_relaxed);
    if (callback_)
      callback_(event);
  }

  // --- Horizontal scroll ---
  if (mouse.usButtonFlags & RI_MOUSE_HWHEEL) {
    InputEvent event = {};
    event.type = InputEventType::MouseHWheel;
    event.timestamp = timestamp;
    event.sequence = seq;
    event.data.mouse.hWheelDelta =
        static_cast<int16_t>(static_cast<SHORT>(mouse.usButtonData));
    event.data.mouse.buttons = mouseButtons_;

    ringBuffer_.TryPush(event);
    totalEvents_.fetch_add(1, std::memory_order_relaxed);
    if (callback_)
      callback_(event);
  }
}

// ---------------------------------------------------------------------------
// SetExclusiveMode — enable/disable input suppression on the host
// ---------------------------------------------------------------------------
void RawInputCapture::SetExclusiveMode(bool exclusive) {
  if (exclusive && !exclusiveActive_.load(std::memory_order_acquire)) {
    // Marshal to the message pump thread — LL hooks MUST be installed on a
    // thread with an active message loop, otherwise they silently fail.
    if (hwnd_) {
      PostMessageW(hwnd_, WM_INSTALL_HOOKS, 0, 0);
    } else {
      InstallHooks(); // Fallback if window not yet created
    }
    LOG_INFO("Input", "Exclusive mode ENABLED — host input suppressed.");
  } else if (!exclusive && exclusiveActive_.load(std::memory_order_acquire)) {
    if (hwnd_) {
      PostMessageW(hwnd_, WM_REMOVE_HOOKS, 0, 0);
    } else {
      RemoveHooks();
    }
    LOG_INFO("Input", "Exclusive mode DISABLED — host input restored.");
  }
}

// ---------------------------------------------------------------------------
// InstallHooks
// ---------------------------------------------------------------------------
void RawInputCapture::InstallHooks() {
  if (!keyboardHook_) {
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                      GetModuleHandleW(nullptr), 0);
    if (!keyboardHook_) {
      LOG_ERROR("Input", "Failed to install keyboard hook (err=%lu).",
                GetLastError());
    }
  }
  if (!mouseHook_) {
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc,
                                   GetModuleHandleW(nullptr), 0);
    if (!mouseHook_) {
      LOG_ERROR("Input", "Failed to install mouse hook (err=%lu).",
                GetLastError());
    }
  }
  exclusiveActive_.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// RemoveHooks
// ---------------------------------------------------------------------------
void RawInputCapture::RemoveHooks() {
  if (keyboardHook_) {
    UnhookWindowsHookEx(keyboardHook_);
    keyboardHook_ = nullptr;
  }
  if (mouseHook_) {
    UnhookWindowsHookEx(mouseHook_);
    mouseHook_ = nullptr;
  }
  exclusiveActive_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// LowLevelKeyboardProc — In exclusive mode, captures keyboard events directly
// and feeds them to the BLE pipeline (bypassing WM_INPUT which gets blocked
// when the hook swallows input). Then returns 1 to suppress on the host.
// ---------------------------------------------------------------------------
LRESULT CALLBACK RawInputCapture::LowLevelKeyboardProc(int nCode, WPARAM wParam,
                                                       LPARAM lParam) {
  if (nCode == HC_ACTION && g_instance &&
      g_instance->exclusiveActive_.load(std::memory_order_acquire)) {

    KBDLLHOOKSTRUCT *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

    // Use GetAsyncKeyState for reliable modifier detection
    bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    // === SAFETY: Ctrl+Alt+S — toggle host/client ===
    if (ctrlHeld && altHeld && kb->vkCode == 'S') {
      return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // === SAFETY: Ctrl+Alt+Q — EMERGENCY KILL ===
    if (ctrlHeld && altHeld && kb->vkCode == 'Q' && isDown) {
      g_instance->exclusiveActive_.store(false, std::memory_order_release);
      if (g_instance->hwnd_) {
        PostMessageW(g_instance->hwnd_, WM_REMOVE_HOOKS, 0, 0);
      }
      LOG_WARN("Input",
               "EMERGENCY: Exclusive mode force-disabled via Ctrl+Alt+Q");
      return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // === SAFETY: Ctrl+Shift+Esc — Task Manager ===
    bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (ctrlHeld && shiftHeld && kb->vkCode == VK_ESCAPE) {
      return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // --- Capture the keyboard event directly from the hook ---
    // Since swallowing (return 1) blocks WM_INPUT on some Windows versions,
    // we create the InputEvent here and feed it to the callback ourselves.
    InputEvent event = {};
    event.timestamp = GetQPCTimestamp();
    event.sequence =
        g_instance->sequenceCounter_.fetch_add(1, std::memory_order_relaxed);
    event.type = isDown ? InputEventType::KeyDown : InputEventType::KeyUp;
    event.data.keyboard.scanCode = static_cast<uint16_t>(kb->scanCode);
    event.data.keyboard.virtualKey = static_cast<uint16_t>(kb->vkCode);
    event.data.keyboard.flags = 0;
    // LLKHF_EXTENDED corresponds to the E0 scan code prefix
    if (kb->flags & LLKHF_EXTENDED) {
      event.data.keyboard.flags |= 0x01;
    }

    g_instance->ringBuffer_.TryPush(event);
    g_instance->totalEvents_.fetch_add(1, std::memory_order_relaxed);
    if (g_instance->callback_) {
      g_instance->callback_(event);
    }

    // === MODIFIER KEYS: must pass through to OS to prevent stuck state ===
    // When Ctrl+Alt+S is pressed to switch to client mode, hooks are installed
    // while Ctrl/Alt are still held. If we swallow their key-UP events, the OS
    // thinks they're permanently held down, causing any later 'S' press to
    // look like Ctrl+Alt+S and re-trigger the toggle.
    // Fix: capture modifiers for BLE (done above) but ALSO let them through.
    if (kb->vkCode == VK_LCONTROL || kb->vkCode == VK_RCONTROL ||
        kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU ||
        kb->vkCode == VK_LSHIFT || kb->vkCode == VK_RSHIFT ||
        kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN ||
        kb->vkCode == VK_CONTROL || kb->vkCode == VK_MENU ||
        kb->vkCode == VK_SHIFT) {
      return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // Swallow regular keys on the host — keyboard goes to client only
    return 1;
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// LowLevelMouseProc — In exclusive mode, captures button and wheel events
// directly (since swallowing may prevent WM_INPUT for those on some systems).
// Mouse MOVEMENT is left to WM_INPUT/ProcessMouse since computing deltas
// from the hook's absolute pt coordinates is unreliable when the cursor
// is frozen by swallowing.
// ---------------------------------------------------------------------------
LRESULT CALLBACK RawInputCapture::LowLevelMouseProc(int nCode, WPARAM wParam,
                                                    LPARAM lParam) {
  if (nCode == HC_ACTION && g_instance &&
      g_instance->exclusiveActive_.load(std::memory_order_acquire)) {

    MSLLHOOKSTRUCT *ms = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
    int64_t timestamp = GetQPCTimestamp();
    uint16_t seq =
        g_instance->sequenceCounter_.fetch_add(1, std::memory_order_relaxed);

    // --- Track button state from hook wParam ---
    bool isButtonEvent = false;
    switch (wParam) {
    case WM_LBUTTONDOWN:
      g_instance->mouseButtons_ |= 0x01;
      isButtonEvent = true;
      break;
    case WM_LBUTTONUP:
      g_instance->mouseButtons_ &= ~0x01;
      isButtonEvent = true;
      break;
    case WM_RBUTTONDOWN:
      g_instance->mouseButtons_ |= 0x02;
      isButtonEvent = true;
      break;
    case WM_RBUTTONUP:
      g_instance->mouseButtons_ &= ~0x02;
      isButtonEvent = true;
      break;
    case WM_MBUTTONDOWN:
      g_instance->mouseButtons_ |= 0x04;
      isButtonEvent = true;
      break;
    case WM_MBUTTONUP:
      g_instance->mouseButtons_ &= ~0x04;
      isButtonEvent = true;
      break;
    case WM_XBUTTONDOWN:
      if (HIWORD(ms->mouseData) == XBUTTON1)
        g_instance->mouseButtons_ |= 0x08;
      else if (HIWORD(ms->mouseData) == XBUTTON2)
        g_instance->mouseButtons_ |= 0x10;
      isButtonEvent = true;
      break;
    case WM_XBUTTONUP:
      if (HIWORD(ms->mouseData) == XBUTTON1)
        g_instance->mouseButtons_ &= ~0x08;
      else if (HIWORD(ms->mouseData) == XBUTTON2)
        g_instance->mouseButtons_ &= ~0x10;
      isButtonEvent = true;
      break;
    }

    // Emit button events directly from hook
    if (isButtonEvent) {
      InputEvent event = {};
      event.type = (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
                    wParam == WM_MBUTTONDOWN || wParam == WM_XBUTTONDOWN)
                       ? InputEventType::MouseButtonDown
                       : InputEventType::MouseButtonUp;
      event.timestamp = timestamp;
      event.sequence = seq;
      event.data.mouse.buttons = g_instance->mouseButtons_;
      g_instance->ringBuffer_.TryPush(event);
      g_instance->totalEvents_.fetch_add(1, std::memory_order_relaxed);
      if (g_instance->callback_)
        g_instance->callback_(event);
    }

    // Emit scroll wheel directly from hook
    if (wParam == WM_MOUSEWHEEL) {
      InputEvent event = {};
      event.type = InputEventType::MouseWheel;
      event.timestamp = timestamp;
      event.sequence = seq;
      event.data.mouse.wheelDelta =
          static_cast<int16_t>(static_cast<SHORT>(HIWORD(ms->mouseData)));
      event.data.mouse.buttons = g_instance->mouseButtons_;
      g_instance->ringBuffer_.TryPush(event);
      g_instance->totalEvents_.fetch_add(1, std::memory_order_relaxed);
      if (g_instance->callback_)
        g_instance->callback_(event);
    }

    // Mouse MOVEMENT is NOT captured here — it flows through
    // WM_INPUT → ProcessMouse which has proper relative delta data.
    // The hook only needs to swallow it on the host.
    return 1;
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace CursorShare
