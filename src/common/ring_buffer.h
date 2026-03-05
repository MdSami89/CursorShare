#pragma once
// =============================================================================
// CursorShare — Lock-Free SPSC Ring Buffer
// Single-producer, single-consumer, wait-free ring buffer.
// Zero heap allocation during operation. Pre-allocated fixed-size store.
// =============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

namespace CursorShare {

/// Lock-free single-producer single-consumer ring buffer.
/// T must be trivially copyable. Capacity must be a power of two.
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert(std::is_trivially_copyable_v<T>,
                  "RingBuffer element must be trivially copyable");
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    RingBuffer() : head_(0), tail_(0) {
        std::memset(buffer_, 0, sizeof(buffer_));
    }

    /// Try to enqueue an element. Returns false if buffer is full.
    /// Called by the PRODUCER only.
    bool TryPush(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & kMask;

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // Buffer full
        }

        std::memcpy(&buffer_[head], &item, sizeof(T));
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Try to dequeue an element. Returns false if buffer is empty.
    /// Called by the CONSUMER only.
    bool TryPop(T& item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // Buffer empty
        }

        std::memcpy(&item, &buffer_[tail], sizeof(T));
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    /// Peek at the front element without removing it.
    bool TryPeek(T& item) const noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        std::memcpy(&item, &buffer_[tail], sizeof(T));
        return true;
    }

    /// Number of elements currently in the buffer.
    size_t Size() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & kMask;
    }

    /// Check if the buffer is empty.
    bool Empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /// Check if the buffer is full.
    bool Full() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & kMask;
        return next == tail_.load(std::memory_order_acquire);
    }

    /// Reset the buffer (not thread-safe — call only when idle).
    void Reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    static constexpr size_t capacity() { return Capacity; }

private:
    static constexpr size_t kMask = Capacity - 1;

    // Ensure head and tail are on separate cache lines to avoid false sharing
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) T buffer_[Capacity];
};

// Pre-configured ring buffer type for input events
// Forward declaration — actual InputEvent is in input_event.h
struct InputEvent;
using InputRingBuffer = RingBuffer<InputEvent, 4096>;

}  // namespace CursorShare
