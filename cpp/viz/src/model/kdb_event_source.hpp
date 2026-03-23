/* kdb_event_source.hpp — KdbEventSource: live KDB+/q TCP push feed
 *
 * Receives raw ReplayEvent structs from a KDB+/q simulated exchange over a
 * POSIX TCP socket.  The KDB+ side sends byte vectors via neg[h]; C++ reads
 * fixed 48-byte IPC frames and extracts the 32-byte ReplayEvent payload at
 * offset 16.  No k.h; no IPC parsing beyond a fixed-offset memcpy.
 *
 * Connection topology: C++ bind()/listen()/accept(); KDB+ connects outbound.
 * Backpressure: drop-on-full.  L2 MBP data is self-healing.
 * Wire format: little-endian matching Linux x86-64 C++ struct layout.
 *
 * Threading: one receive thread (producer), sim thread (consumer).
 * No virtual on hot-path structs (Pitfall 4).  No STL allocator on hot path.
 *
 * Build flags: -std=c++17 -O2 -march=native -Wall -Wextra
 *              -Wconversion -Wsign-conversion -Werror -fno-exceptions
 * Namespace: viz::model
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "event_source.hpp"
#include "replay_event.hpp"
#include "spsc_queue.hpp"

namespace viz::model {

// ---------------------------------------------------------------------------
// Named constants
// ---------------------------------------------------------------------------

static constexpr uint16_t    KDB_DEFAULT_PORT       = 7890U;
static constexpr uint32_t    KDB_QUEUE_CAPACITY      = 4096U;
static constexpr std::size_t KDB_IPC_FRAME_SIZE      = 48U;
static constexpr std::size_t KDB_IPC_PAYLOAD_OFFSET  = 16U;

// ---------------------------------------------------------------------------
// KdbEventSourceConfig
//
// Plain aggregate; zero-initialise or use designated initialisers.
// ---------------------------------------------------------------------------

struct KdbEventSourceConfig {
    uint16_t port         = KDB_DEFAULT_PORT;
    bool     drop_on_full = true;   // always true; field documents the policy
};

// ---------------------------------------------------------------------------
// KdbEventSource final : public IEventSource
//
// Hot path: next_event() — front() + assignment + pop(); O(1) L1 hit.
// is_live() is defined inline (Idiom 6): GCC inlines regardless of LTO.
// ---------------------------------------------------------------------------

class KdbEventSource final : public IEventSource {
public:
    // Constructor: creates socket, sets SO_REUSEADDR, bind()s, listen()s.
    // Calls std::terminate() on socket/bind/listen failure (fno-exceptions).
    // Does NOT call accept() — returns immediately after listen().
    explicit KdbEventSource(KdbEventSourceConfig cfg);

    // Destructor: calls stop().  Idempotent — safe even if stop() already called.
    ~KdbEventSource() override;

    // Not copyable or movable (owns a thread, atomics, and an fd).
    KdbEventSource(const KdbEventSource&)            = delete;
    KdbEventSource& operator=(const KdbEventSource&) = delete;
    KdbEventSource(KdbEventSource&&)                 = delete;
    KdbEventSource& operator=(KdbEventSource&&)      = delete;

    // start() — launches receive thread.  Must be called once after construction.
    // Returns immediately; accept() blocks inside the receive thread.
    void start();

    // IEventSource overrides -------------------------------------------------

    // Returns true and pops the front event into out.
    // Returns false immediately (no spin) if the queue is empty.
    bool next_event(ReplayEvent& out) override;

    // No-op: live feed cannot be rewound.  Safe to call at any time.
    void reset() override;

    // Returns SIZE_MAX: event count is unbounded for a live feed.
    std::size_t event_count() const override;

    // Returns true: this is a live feed; SimEngine skips virtual-clock pacing.
    // Defined inline so GCC always inlines it (Idiom 6, Pitfall 8).
    bool is_live() const noexcept override { return true; }

    // stop() — signals receive thread to exit, closes fds, joins thread.
    // Idempotent: safe to call multiple times.
    void stop();

    // Diagnostic: approximate count of frames dropped due to full queue.
    uint64_t events_dropped() const {
        return events_dropped_.load(std::memory_order_relaxed);
    }

private:
    // ---- Receive thread entry point ----------------------------------------
    void receive_loop();

    // ---- recv_exact: loop until exactly n bytes received -------------------
    // Returns false on disconnect, error, or stop_flag_.
    bool recv_exact(int fd, void* buf, std::size_t n);

    // ---- Members -----------------------------------------------------------

    KdbEventSourceConfig cfg_;   // stored config

    // ~128 KB; largest member.  KdbEventSource must be heap-allocated (new /
    // unique_ptr) — do not stack-allocate.
    SpscQueue<ReplayEvent, KDB_QUEUE_CAPACITY> queue_;

    int listen_fd_;                  // bound, listening socket; main thread only
    std::atomic<int>      accept_fd_;    // accepted fd; written by recv thread, read by stop()
    std::atomic<bool>     stop_flag_;    // receive thread exit signal
    std::thread           recv_thread_;  // default-constructed until start()
    std::atomic<uint64_t> events_dropped_;  // drop-on-full diagnostic counter
};

} // namespace viz::model
