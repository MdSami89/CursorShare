// =============================================================================
// CursorShare — Latency Monitor (Implementation)
// =============================================================================

#include "latency_monitor.h"
#include <cmath>
#include <numeric>


namespace CursorShare {

LatencyMonitor::LatencyMonitor() {
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  qpcFrequency_ = static_cast<double>(freq.QuadPart);

  for (auto &stage : stages_) {
    stage.samples.fill(0.0);
  }
}

void LatencyMonitor::RecordSample(PipelineStage stage, int64_t startQpc,
                                  int64_t endQpc) {
  double us = QpcToMicroseconds(endQpc - startQpc);
  RecordSampleUs(stage, us);
}

void LatencyMonitor::RecordSampleUs(PipelineStage stage, double microseconds) {
  int idx = static_cast<int>(stage);
  if (idx < 0 || idx >= kStageCount)
    return;

  auto &s = stages_[idx];
  std::lock_guard<std::mutex> lock(s.mutex);

  // Update running statistics
  if (s.count > 0) {
    // Remove oldest sample from running sum if buffer is full
    if (s.count >= kMaxSamples) {
      s.runningSum -= s.samples[s.writeIndex];
    }
  }

  s.samples[s.writeIndex] = microseconds;
  s.runningSum += microseconds;
  s.writeIndex = (s.writeIndex + 1) % kMaxSamples;
  if (s.count < kMaxSamples)
    s.count++;

  s.minVal = std::min(s.minVal, microseconds);
  s.maxVal = std::max(s.maxVal, microseconds);
}

LatencyStats LatencyMonitor::GetStats(PipelineStage stage) const {
  int idx = static_cast<int>(stage);
  if (idx < 0 || idx >= kStageCount)
    return {};

  auto &s = stages_[idx];
  std::lock_guard<std::mutex> lock(s.mutex);

  LatencyStats stats;
  stats.sampleCount = s.count;

  if (s.count == 0)
    return stats;

  stats.minUs = s.minVal;
  stats.maxUs = s.maxVal;
  stats.avgUs = s.runningSum / s.count;

  // Calculate p99
  // Copy samples and sort for percentile calculation
  std::array<double, kMaxSamples> sorted;
  int sampleCount = s.count;
  for (int i = 0; i < sampleCount; ++i) {
    sorted[i] = s.samples[i];
  }
  std::sort(sorted.begin(), sorted.begin() + sampleCount);

  int p99Index = static_cast<int>(std::ceil(sampleCount * 0.99)) - 1;
  p99Index = std::clamp(p99Index, 0, sampleCount - 1);
  stats.p99Us = sorted[p99Index];

  return stats;
}

void LatencyMonitor::Reset() {
  for (auto &s : stages_) {
    std::lock_guard<std::mutex> lock(s.mutex);
    s.samples.fill(0.0);
    s.writeIndex = 0;
    s.count = 0;
    s.runningSum = 0.0;
    s.minVal = 1e9;
    s.maxVal = 0.0;
  }
}

} // namespace CursorShare
