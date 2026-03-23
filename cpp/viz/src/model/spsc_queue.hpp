/* spsc_queue.hpp — Single-Producer Single-Consumer bounded ring buffer
 *
 * Template: SpscQueue<T, N>
 *   T — element type; must be default-constructible and copyable.
 *   N — queue capacity; must be a power of 2 and >= 2.
 *
 * Threading contract:
 *   Exactly one producer thread calls try_push().
 *   Exactly one consumer thread calls front() / pop() / size_approx().
 *   size_approx() may be called from any thread.
 *
 * Memory ordering:
 *   tail_ store: memory_order_release  (synchronises with front()'s acquire load)
 *   tail_ load:  memory_order_acquire  (in try_push, full check)
 *   head_ store: memory_order_release  (synchronises with try_push()'s acquire load)
 *   head_ load:  memory_order_acquire  (in try_push, full check)
 *   head_ load:  memory_order_relaxed  (in front, private to consumer)
 *
 * No heap allocation. No exceptions. -fno-exceptions compliant.
 * Namespace: viz::model
 *
 * Idiom 2 (template sizing), Idiom 3 (static_assert layout),
 * Idiom 6 (class-body inline), Idiom 9 (build flags).
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace viz::model {

// ---------------------------------------------------------------------------
// SpscQueue<T, N>
// ---------------------------------------------------------------------------

template<typename T, uint32_t N>
class SpscQueue {
public:
    // Compile-time preconditions (Idiom 3).
    static_assert((N & (N - 1U)) == 0U, "SpscQueue N must be a power of 2");
    static_assert(N >= 2U,              "SpscQueue N must be at least 2");

    SpscQueue()  = default;
    ~SpscQueue() = default;

    // Not copyable or movable (contains atomics and a potentially large buf_).
    SpscQueue(const SpscQueue&)            = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&)                 = delete;
    SpscQueue& operator=(SpscQueue&&)      = delete;

    // -----------------------------------------------------------------------
    // try_push — producer thread only
    //
    // Returns true and copies item into the queue on success.
    // Returns false (no mutation) if the queue is full.
    // -----------------------------------------------------------------------
    bool try_push(const T& item) {
        // Load head with acquire to synchronise with pop()'s release store.
        uint32_t head_val = head_.slot.load(std::memory_order_acquire);
        uint32_t tail_val = tail_.slot.load(std::memory_order_relaxed);

        // Full when outstanding count == N.  uint32_t subtraction wraps correctly.
        if ((tail_val - head_val) >= N) {
            return false;
        }

        buf_[tail_val & (N - 1U)] = item;

        // Release store: makes the written element visible to the consumer.
        tail_.slot.store(tail_val + 1U, std::memory_order_release);
        return true;
    }

    // -----------------------------------------------------------------------
    // front — consumer thread only
    //
    // Returns a pointer to the front element, or nullptr if empty.
    // The pointer is valid until pop() is called.
    // -----------------------------------------------------------------------
    T* front() {
        uint32_t head_val = head_.slot.load(std::memory_order_relaxed);
        // Acquire load of tail_ synchronises with try_push()'s release store.
        uint32_t tail_val = tail_.slot.load(std::memory_order_acquire);

        if (head_val == tail_val) {
            return nullptr;
        }
        return &buf_[head_val & (N - 1U)];
    }

    // -----------------------------------------------------------------------
    // pop — consumer thread only
    //
    // Precondition: front() returned non-null immediately prior.
    // Undefined behaviour if called on an empty queue.
    // -----------------------------------------------------------------------
    void pop() {
        uint32_t head_val = head_.slot.load(std::memory_order_relaxed);
        // Release store: makes freed slot visible to producer for full-check.
        head_.slot.store(head_val + 1U, std::memory_order_release);
    }

    // -----------------------------------------------------------------------
    // size_approx — any thread
    //
    // Returns an approximate element count in [0, N].
    // May be stale; not guaranteed exact at the moment of return.
    // -----------------------------------------------------------------------
    uint32_t size_approx() const {
        uint32_t head_val = head_.slot.load(std::memory_order_relaxed);
        uint32_t tail_val = tail_.slot.load(std::memory_order_relaxed);
        // uint32_t subtraction wraps; clamp to N.
        uint32_t diff = tail_val - head_val;
        return (diff <= N) ? diff : N;
    }

private:
    // Each atomic counter occupies its own cache line to prevent false sharing
    // between the producer (which writes tail_) and the consumer (which writes
    // head_).  alignas(64) ensures 64-byte cache-line alignment.
    //
    // The struct wrapper with char _pad[] gives exactly 64 bytes per slot
    // regardless of std::hardware_destructive_interference_size availability.
    struct alignas(64) Slot {
        std::atomic<uint32_t> slot{0U};
        char _pad[60];   // pad to 64 bytes: sizeof(atomic<uint32_t>) == 4

        // Layout check inside the struct where it is in scope.
        static_assert(sizeof(std::atomic<uint32_t>) + 60U == 64U,
                      "SpscQueue::Slot _pad size incorrect");
    };

    // Verify Slot is exactly one cache line.
    static_assert(sizeof(Slot) == 64U,
                  "SpscQueue::Slot layout changed — cache-line padding incorrect");

    Slot   head_{};   // consumer writes (cache line 0)
    Slot   tail_{};   // producer writes (cache line 1)
    T      buf_[N];   // ring buffer storage (cache lines 2+)
};

} // namespace viz::model
