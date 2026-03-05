// =============================================================================
// CursorShare — Latency Benchmark Tool
// Measures pipeline latency for performance validation.
// =============================================================================

#include "../src/common/hid_descriptors.h"
#include "../src/common/input_event.h"
#include "../src/common/logger.h"
#include "../src/input/raw_input_capture.h"
#include "../src/service/latency_monitor.h"
#include <chrono>
#include <conio.h>
#include <iostream>
#include <thread>
#include <windows.h>


using namespace CursorShare;

int main() {
  SetConsoleOutputCP(CP_UTF8);
  CursorShare::Logger::Instance().Init("", CursorShare::LogLevel::Trace, true);
  LOG_INFO("Benchmark", "Latency Benchmark Tool starting...");

  std::cout << "CursorShare Latency Benchmark" << std::endl;
  std::cout << "=============================" << std::endl;
  std::cout << std::endl;

  LatencyMonitor monitor;

  // Benchmark 1: Ring buffer throughput
  std::cout << "--- Ring Buffer Throughput ---" << std::endl;
  {
    RingBuffer<InputEvent, 4096> buffer;
    InputEvent event = {};
    event.type = InputEventType::MouseMove;
    event.data.mouse.dx = 10;
    event.data.mouse.dy = -5;

    LARGE_INTEGER start, end, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    const int iterations = 1000000;
    for (int i = 0; i < iterations; ++i) {
      buffer.TryPush(event);
      InputEvent out;
      buffer.TryPop(out);
    }

    QueryPerformanceCounter(&end);
    double totalMs = static_cast<double>(end.QuadPart - start.QuadPart) *
                     1000.0 / freq.QuadPart;
    double perOpNs = (totalMs * 1000000.0) / iterations;

    std::cout << "  " << iterations << " push+pop cycles in " << totalMs
              << " ms" << std::endl;
    std::cout << "  " << perOpNs << " ns per push+pop" << std::endl;
    std::cout << std::endl;
  }

  // Benchmark 2: QPC timestamp overhead
  std::cout << "--- QPC Timestamp Overhead ---" << std::endl;
  {
    LARGE_INTEGER start, end, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    const int iterations = 1000000;
    volatile int64_t dummy = 0;
    for (int i = 0; i < iterations; ++i) {
      dummy = GetQPCTimestamp();
    }

    QueryPerformanceCounter(&end);
    double totalMs = static_cast<double>(end.QuadPart - start.QuadPart) *
                     1000.0 / freq.QuadPart;
    double perCallNs = (totalMs * 1000000.0) / iterations;

    std::cout << "  " << iterations << " QPC calls in " << totalMs << " ms"
              << std::endl;
    std::cout << "  " << perCallNs << " ns per call" << std::endl;
    std::cout << std::endl;
  }

  // Benchmark 3: HID report encoding
  std::cout << "--- HID Report Encoding ---" << std::endl;
  {
    LARGE_INTEGER start, end, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    const int iterations = 1000000;
    uint8_t kbReport[8];
    uint8_t mouseReport[7];
    uint8_t keys[6] = {0x04, 0x05, 0x06, 0, 0, 0}; // A, B, C

    for (int i = 0; i < iterations; ++i) {
      EncodeKeyboardReport(0x01, keys, kbReport);
      EncodeMouseReport(0x01, 100, -50, 3, 0, mouseReport);
    }

    QueryPerformanceCounter(&end);
    double totalMs = static_cast<double>(end.QuadPart - start.QuadPart) *
                     1000.0 / freq.QuadPart;
    double perEncNs = (totalMs * 1000000.0) / (iterations * 2);

    std::cout << "  " << iterations * 2 << " encode operations in " << totalMs
              << " ms" << std::endl;
    std::cout << "  " << perEncNs << " ns per encode" << std::endl;
    std::cout << std::endl;
  }

  // Benchmark 4: Latency monitor recording overhead
  std::cout << "--- Latency Monitor Overhead ---" << std::endl;
  {
    LARGE_INTEGER start, end, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    const int iterations = 100000;
    for (int i = 0; i < iterations; ++i) {
      int64_t t1 = GetQPCTimestamp();
      int64_t t2 = GetQPCTimestamp();
      monitor.RecordSample(PipelineStage::InputCapture, t1, t2);
    }

    QueryPerformanceCounter(&end);
    double totalMs = static_cast<double>(end.QuadPart - start.QuadPart) *
                     1000.0 / freq.QuadPart;
    double perRecNs = (totalMs * 1000000.0) / iterations;

    std::cout << "  " << iterations << " samples in " << totalMs << " ms"
              << std::endl;
    std::cout << "  " << perRecNs << " ns per recording" << std::endl;
    std::cout << std::endl;
  }

  std::cout << "Benchmark complete." << std::endl;
  std::cout << "Press any key to exit..." << std::endl;
  getch();
  return 0;
}
