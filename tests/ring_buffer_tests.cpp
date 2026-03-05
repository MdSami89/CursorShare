// =============================================================================
// CursorShare — Ring Buffer Tests
// =============================================================================

#include "../src/common/input_event.h"
#include "../src/common/ring_buffer.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>


using namespace CursorShare;

void TestBasicPushPop() {
  RingBuffer<InputEvent, 16> rb;

  // Empty buffer
  assert(rb.Empty());
  assert(rb.Size() == 0);

  InputEvent event = {};
  event.type = InputEventType::KeyDown;
  event.data.keyboard.scanCode = 0x1E; // 'A'
  event.timestamp = 12345;

  // Push one
  assert(rb.TryPush(event));
  assert(!rb.Empty());
  assert(rb.Size() == 1);

  // Pop one
  InputEvent out;
  assert(rb.TryPop(out));
  assert(out.type == InputEventType::KeyDown);
  assert(out.data.keyboard.scanCode == 0x1E);
  assert(out.timestamp == 12345);
  assert(rb.Empty());

  std::cout << "[PASS] TestBasicPushPop" << std::endl;
}

void TestFull() {
  RingBuffer<InputEvent, 4> rb; // Capacity 4, usable = 3

  InputEvent event = {};
  event.type = InputEventType::MouseMove;

  // Fill buffer
  assert(rb.TryPush(event));
  assert(rb.TryPush(event));
  assert(rb.TryPush(event));

  // Buffer should be full (capacity-1 usable slots)
  assert(rb.Full());
  assert(!rb.TryPush(event));

  // Pop one
  InputEvent out;
  assert(rb.TryPop(out));
  assert(!rb.Full());

  // Now can push again
  assert(rb.TryPush(event));

  std::cout << "[PASS] TestFull" << std::endl;
}

void TestConcurrent() {
  RingBuffer<InputEvent, 4096> rb;
  const int count = 100000;
  std::atomic<int> consumed{0};

  // Producer thread
  std::thread producer([&]() {
    for (int i = 0; i < count; ++i) {
      InputEvent event = {};
      event.type = InputEventType::KeyDown;
      event.sequence = static_cast<uint16_t>(i & 0xFFFF);
      event.timestamp = i;

      while (!rb.TryPush(event)) {
        // Busy wait (spin)
      }
    }
  });

  // Consumer thread
  std::thread consumer([&]() {
    int expected = 0;
    while (expected < count) {
      InputEvent event;
      if (rb.TryPop(event)) {
        assert(event.timestamp == expected);
        expected++;
        consumed.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  producer.join();
  consumer.join();

  assert(consumed.load() == count);
  assert(rb.Empty());

  std::cout << "[PASS] TestConcurrent (" << count << " events)" << std::endl;
}

void TestReset() {
  RingBuffer<InputEvent, 16> rb;
  InputEvent event = {};

  rb.TryPush(event);
  rb.TryPush(event);
  assert(rb.Size() == 2);

  rb.Reset();
  assert(rb.Empty());
  assert(rb.Size() == 0);

  std::cout << "[PASS] TestReset" << std::endl;
}

int main() {
  std::cout << "=== Ring Buffer Tests ===" << std::endl;
  TestBasicPushPop();
  TestFull();
  TestConcurrent();
  TestReset();
  std::cout << "All ring buffer tests passed!" << std::endl;
  return 0;
}
