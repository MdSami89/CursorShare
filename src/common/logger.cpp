// =============================================================================
// CursorShare — Centralized Logger (Implementation)
// =============================================================================

#include "logger.h"
#include "constants.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace CursorShare {

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
Logger &Logger::Instance() {
  static Logger instance;
  return instance;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
Logger::~Logger() { Shutdown(); }

// ---------------------------------------------------------------------------
// DefaultLogPath — resolve to exe directory / cursorshare.log
// ---------------------------------------------------------------------------
std::string Logger::DefaultLogPath() {
#ifdef _WIN32
  char path[MAX_PATH] = {};
  DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (len > 0) {
    std::filesystem::path exePath(path);
    return (exePath.parent_path() / "cursorshare.log").string();
  }
#endif
  return "cursorshare.log";
}

// ---------------------------------------------------------------------------
// Timestamp — "YYYY-MM-DD HH:MM:SS.mmm"
// ---------------------------------------------------------------------------
std::string Logger::Timestamp() {
  using namespace std::chrono;

  auto now = system_clock::now();
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  auto time_t_now = system_clock::to_time_t(now);

  struct tm tm_buf{};
#ifdef _WIN32
  localtime_s(&tm_buf, &time_t_now);
#else
  localtime_r(&time_t_now, &tm_buf);
#endif

  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                static_cast<int>(ms.count()));
  return std::string(buf);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void Logger::Init(const std::string &filePath, LogLevel minLevel, bool console,
                  size_t maxFileBytes) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    return; // Already initialized
  }

  minLevel_ = minLevel;
  consoleOutput_ = console;
  maxFileBytes_ = maxFileBytes;
  filePath_ = filePath.empty() ? DefaultLogPath() : filePath;

  // Open log file in append mode
  file_.open(filePath_, std::ios::app | std::ios::out);
  if (!file_.is_open()) {
    std::cerr << "[Logger] WARNING: Could not open log file: " << filePath_
              << std::endl;
    // Logger will still work for console output
  } else {
    // Get current file size
    file_.seekp(0, std::ios::end);
    currentFileBytes_ = static_cast<size_t>(file_.tellp());
  }

  initialized_ = true;

  // Write startup banner
  std::string banner =
      "===============================================================\n";
  std::string header = "[" + Timestamp() + "] [INFO ] [Logger] CursorShare v" +
                       std::string(kAppVersion) +
                       " — Logger initialized (file: " + filePath_ + ")\n";

  if (file_.is_open()) {
    file_ << banner << header << banner;
    file_.flush();
    currentFileBytes_ += banner.size() * 2 + header.size();
  }
  if (consoleOutput_) {
    std::cout << banner << header << banner;
  }
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void Logger::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_)
    return;

  if (file_.is_open()) {
    file_ << "[" << Timestamp()
          << "] [INFO ] [Logger] Shutdown — closing log file.\n";
    file_.flush();
    file_.close();
  }

  initialized_ = false;
}

// ---------------------------------------------------------------------------
// RotateIfNeeded
// ---------------------------------------------------------------------------
void Logger::RotateIfNeeded() {
  // Must be called under lock
  if (!file_.is_open() || currentFileBytes_ < maxFileBytes_) {
    return;
  }

  file_.close();

  namespace fs = std::filesystem;

  // Rotate: .log.3 -> delete, .log.2 -> .log.3, .log.1 -> .log.2, .log ->
  // .log.1
  for (int i = kMaxRotatedFiles; i >= 1; --i) {
    std::string src =
        (i == 1) ? filePath_ : filePath_ + "." + std::to_string(i - 1);
    std::string dst = filePath_ + "." + std::to_string(i);

    std::error_code ec;
    if (fs::exists(src, ec)) {
      if (i == kMaxRotatedFiles) {
        fs::remove(dst, ec); // Delete oldest
      }
      fs::rename(src, dst, ec);
    }
  }

  // Re-open a fresh log file
  file_.open(filePath_, std::ios::out | std::ios::trunc);
  currentFileBytes_ = 0;

  if (file_.is_open()) {
    std::string msg =
        "[" + Timestamp() +
        "] [INFO ] [Logger] Log rotated — previous logs archived.\n";
    file_ << msg;
    file_.flush();
    currentFileBytes_ = msg.size();
  }
}

// ---------------------------------------------------------------------------
// Log (printf-style)
// ---------------------------------------------------------------------------
void Logger::Log(LogLevel level, const char *tag, const char *fmt, ...) {
  if (level < minLevel_) {
    return;
  }

  // Format the user message
  char msgBuf[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
  va_end(args);

  // Build the full log line
  std::string ts = Timestamp();
  char lineBuf[2200];
  std::snprintf(lineBuf, sizeof(lineBuf), "[%s] [%s] [%s] %s\n", ts.c_str(),
                LogLevelToString(level), tag, msgBuf);

  std::string line(lineBuf);

  std::lock_guard<std::mutex> lock(mutex_);

  // Write to file
  if (file_.is_open()) {
    RotateIfNeeded();
    file_ << line;
    file_.flush();
    currentFileBytes_ += line.size();
  }

  // Write to console
  if (consoleOutput_) {
    if (level >= LogLevel::Error) {
      std::cerr << line;
    } else {
      std::cout << line;
    }
  }
}

// ---------------------------------------------------------------------------
// LogW — Wide string variant (for WinRT error messages)
// ---------------------------------------------------------------------------
void Logger::LogW(LogLevel level, const char *tag, const wchar_t *msg) {
  if (level < minLevel_) {
    return;
  }

  // Convert wide string to narrow (UTF-8)
  char narrow[1024];
#ifdef _WIN32
  int len = WideCharToMultiByte(CP_UTF8, 0, msg, -1, narrow, sizeof(narrow),
                                nullptr, nullptr);
  if (len <= 0) {
    std::snprintf(narrow, sizeof(narrow), "(wide string conversion failed)");
  }
#else
  std::snprintf(narrow, sizeof(narrow), "%ls", msg);
#endif

  Log(level, tag, "%s", narrow);
}

} // namespace CursorShare
