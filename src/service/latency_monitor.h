#pragma once
// =============================================================================
// CursorShare — Latency Monitor
// QPC-based timestamp tracking for each pipeline stage.
// =============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>


#ifdef _WIN32
#include <windows.h>
#endif

namespace CursorShare {

/// Latency statistics for a measurement window.
struct LatencyStats {
  double minUs = 0.0;
  double maxUs = 0.0;
  double avgUs = 0.0;
  double p99Us = 0.0;
  uint64_t sampleCount = 0;
};

/// Pipeline stages for latency measurement.
enum class PipelineStage : uint8_t {
  InputCapture = 0,    // Hardware event → capture
  RoutingDecision = 1, // Routing logic
  HidEncoding = 2,     // HID report encoding
  BtTransmit = 3,      // Bluetooth transmission
  EndToEnd = 4,        // Total pipeline
  StageCount = 5,
};

/// Real-time latency monitor with rolling statistics.
class LatencyMonitor {
public:
  LatencyMonitor();

  /// Record a latency sample for a pipeline stage.
  /// @param stage   Which part of the pipeline
  /// @param startQpc  QPC value at start
  /// @param endQpc    QPC value at end
  void RecordSample(PipelineStage stage, int64_t startQpc, int64_t endQpc);

  /// Record latency in microseconds directly.
  void RecordSampleUs(PipelineStage stage, double microseconds);

  /// Get current statistics for a stage.
  LatencyStats GetStats(PipelineStage stage) const;

  /// Reset all statistics.
  void Reset();

  /// Get QPC frequency (ticks per second).
  double GetQpcFrequency() const { return qpcFrequency_; }

  /// Convert QPC delta to microseconds.
  double QpcToMicroseconds(int64_t delta) const {
    return (static_cast<double>(delta) * 1000000.0) / qpcFrequency_;
  }

private:
  static constexpr int kMaxSamples = 1024;
  static constexpr int kStageCount =
      static_cast<int>(PipelineStage::StageCount);

  struct StageSamples {
    std::array<double, kMaxSamples> samples;
    int writeIndex = 0;
    int count = 0;
    double runningSum = 0.0;
    double minVal = 1e9;
    double maxVal = 0.0;
    mutable std::mutex mutex;
  };

  std::array<StageSamples, kStageCount> stages_;
  double qpcFrequency_;
};

} // namespace CursorShare
