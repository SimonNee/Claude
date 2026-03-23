/* sim_engine.hpp — SimEngine: sim thread entry point
 *
 * Consumes events from IEventSource*, dispatches into eth::book::Book,
 * and publishes BookSnapshot to SnapshotBuffer after each event.
 *
 * All pointers are non-owning.  Objects are owned and constructed by main.cpp.
 *
 * Includes: model headers only.  No ImGui.
 * Namespace: viz::model
 */

#pragma once

#include <atomic>
#include <mutex>

#include "event_source.hpp"
#include "snapshot_buffer.hpp"
#include "playback_cmd.hpp"
#include "save_cmd.hpp"

// ETH book — on include path as cpp/viz/../eth/cpp
#include "book.hpp"

namespace viz::model {

// ---------------------------------------------------------------------------
// SimEngine
// ---------------------------------------------------------------------------

class SimEngine {
public:
    // All pointers are non-owning; the caller (main.cpp) owns all objects.
    SimEngine(IEventSource*   source,
              SnapshotBuffer* sb,
              std::mutex*     cmd_mutex,
              PlaybackCmd*    cmd,
              std::mutex*     save_mutex,
              SaveCmd*        save_cmd);

    // Sim thread entry point.  Loops until stop() is called.
    void run();

    // Signal the sim thread to exit.  Called from the main thread.
    void stop();

private:
    IEventSource*   source_;
    SnapshotBuffer* sb_;
    std::mutex*     cmd_mutex_;
    PlaybackCmd*    cmd_;
    std::mutex*     save_mutex_;
    SaveCmd*        save_cmd_;

    eth::book::Book book_;          // ETH/USDT orderbook
    uint64_t        virtual_clock_ns_;
    uint64_t        prev_event_ns_;  // timestamp of previous event for pacing

    // Fill ring buffer — maintained across publish_snapshot() calls.
    FillEntry  fill_ring_[VIZ_TAPE_DEPTH];
    uint32_t   fill_head_;    // index of next write slot (ring buffer head)
    uint32_t   fill_count_;   // total valid entries (capped at VIZ_TAPE_DEPTH)

    // Previous best bid/ask — used to detect top-of-book movement for fill synthesis.
    eth::book::tick_t prev_best_bid_;
    eth::book::tick_t prev_best_ask_;

    bool              live_source_;  // true if source is a live feed; skip sleep_for pacing
    std::atomic<bool> stop_flag_;

    // Private helpers
    bool dispatch_event(const ReplayEvent& e);
    void publish_snapshot();
};

} // namespace viz::model
