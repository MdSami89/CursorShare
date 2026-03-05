#pragma once
// =============================================================================
// CursorShare — Centralized Logger
// Thread-safe file + console logger with log levels and rotation.
// =============================================================================

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace CursorShare {

// ---------------------------------------------------------------------------
// Log Levels
// ---------------------------------------------------------------------------
enum class LogLevel : uint8_t {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
  Fatal = 5,
};

/// Convert LogLevel to short fixed-width string.
inline const char *LogLevelToString(LogLevel level) {
  switch (level) {
  case LogLevel::Trace:
    return "TRACE";
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO ";
  case LogLevel::Warn:
    return "WARN ";
  case LogLevel::Error:
    return "ERROR";
  case LogLevel::Fatal:
    return "FATAL";
  default:
    return "?????";
  }
}

// ---------------------------------------------------------------------------
// Logger — Thread-safe singleton
// ---------------------------------------------------------------------------
class Logger {
public:
  /// Get the global logger instance.
  static Logger &Instance();

  /// Initialize with file path. Call once at startup.
  /// @param filePath  Log file path (default: "cursorshare.log" next to exe)
  /// @param minLevel  Minimum level to log (default: Info)
  /// @param console   Also print to console (default: true)
  /// @param maxFileBytes  Max log file size before rotation (default: 5 MB)
  void Init(const std::string &filePath = "",
            LogLevel minLevel = LogLevel::Info, bool console = true,
            size_t maxFileBytes = 5 * 1024 * 1024);

  /// Shut down the logger (flushes and closes file).
  void Shutdown();

  /// Set minimum log level at runtime.
  void SetLevel(LogLevel level) { minLevel_ = level; }

  /// Get current minimum log level.
  LogLevel GetLevel() const { return minLevel_; }

  /// Enable/disable console output at runtime.
  void SetConsoleOutput(bool enable) { consoleOutput_ = enable; }

  /// Log a message.
  void Log(LogLevel level, const char *tag, const char *fmt, ...);

  /// Log a wide-string message (for WinRT errors etc.).
  void LogW(LogLevel level, const char *tag, const wchar_t *msg);

private:
  Logger() = default;
  ~Logger();

  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  /// Rotate log files if current file exceeds maxFileBytes_.
  void RotateIfNeeded();

  /// Get current timestamp string: "YYYY-MM-DD HH:MM:SS.mmm"
  static std::string Timestamp();

  /// Resolve default log file path (next to the executable).
  static std::string DefaultLogPath();

  std::mutex mutex_;
  std::ofstream file_;
  std::string filePath_;
  LogLevel minLevel_ = LogLevel::Info;
  bool consoleOutput_ = true;
  bool initialized_ = false;
  size_t maxFileBytes_ = 5 * 1024 * 1024;
  size_t currentFileBytes_ = 0;
  int rotationCount_ = 0;
  static constexpr int kMaxRotatedFiles = 3;
};

} // namespace CursorShare

// =============================================================================
// Convenience Macros — Usage:  LOG_INFO("BLE-HID", "Port %d open", port);
// =============================================================================
#define LOG_TRACE(tag, ...)                                                    \
  CursorShare::Logger::Instance().Log(CursorShare::LogLevel::Trace, tag,       \
                                      __VA_ARGS__)
#define LOG_DEBUG(tag, ...)                                                    \
  CursorShare::Logger::Instance().Log(CursorShare::LogLevel::Debug, tag,       \
                                      __VA_ARGS__)
#define LOG_INFO(tag, ...)                                                     \
  CursorShare::Logger::Instance().Log(CursorShare::LogLevel::Info, tag,        \
                                      __VA_ARGS__)
#define LOG_WARN(tag, ...)                                                     \
  CursorShare::Logger::Instance().Log(CursorShare::LogLevel::Warn, tag,        \
                                      __VA_ARGS__)
#define LOG_ERROR(tag, ...)                                                    \
  CursorShare::Logger::Instance().Log(CursorShare::LogLevel::Error, tag,       \
                                      __VA_ARGS__)
#define LOG_FATAL(tag, ...)                                                    \
  CursorShare::Logger::Instance().Log(CursorShare::LogLevel::Fatal, tag,       \
                                      __VA_ARGS__)

// Wide-string variant for WinRT error messages
#define LOG_ERROR_W(tag, msg)                                                  \
  CursorShare::Logger::Instance().LogW(CursorShare::LogLevel::Error, tag, msg)
#define LOG_WARN_W(tag, msg)                                                   \
  CursorShare::Logger::Instance().LogW(CursorShare::LogLevel::Warn, tag, msg)
