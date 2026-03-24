/* test_matching.cpp — Unit tests for SimEngine price-time priority matching
 *
 * Case 1: BID walks multiple ask levels (user's example)
 *   ASK 200003 qty=3, ASK 200004 qty=6, BID 200005 qty=7
 *   → fills at ask prices: (200003,3) then (200004,4); ask 200004 rests qty=2
 *
 * Case 2: Passive BID rests (no ask in book)
 *   BID 200000 qty=5 → no fills; bid rests at 200000
 *
 * Case 3: Partial fill — bid qty < ask qty, ask remainder rests
 *   ASK 200003 qty=6, BID 200005 qty=4
 *   → fill (200003,4); ask 200003 rests at qty=2
 *
 * Includes model .cpp files directly for standalone linking (no viz library).
 */

#include <cassert>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "snapshot_buffer.hpp"
#include "playback_cmd.hpp"
#include "save_cmd.hpp"
#include "event_source.hpp"
#include "replay_event.hpp"
#include "sim_engine.hpp"

// Standalone linking — include implementations directly.
#include "ou_event_source.cpp"
#include "csv_event_source.cpp"
#include "sim_engine.cpp"
#include "book.cpp"

// ---------------------------------------------------------------------------
// FixedEventSource — feeds a predetermined sequence then signals exhaustion
// ---------------------------------------------------------------------------

struct FixedEventSource : viz::model::IEventSource {
    std::vector<viz::model::ReplayEvent> events_;
    std::size_t pos_ = 0U;

    explicit FixedEventSource(std::initializer_list<viz::model::ReplayEvent> evs)
        : events_(evs) {}

    bool next_event(viz::model::ReplayEvent& out) override {
        if (pos_ >= events_.size()) { return false; }
        out = events_[pos_++];
        return true;
    }
    void reset() override { pos_ = 0U; }
    std::size_t event_count() const override { return events_.size(); }
    bool is_live() const noexcept override { return false; }
};

static viz::model::ReplayEvent make_event(uint8_t side, uint32_t tick, uint64_t qty) {
    viz::model::ReplayEvent e{};
    e.timestamp_ns = 0U;
    e.tick         = tick;
    e._pad         = 0U;
    e.qty          = qty;
    e.side         = side;
    return e;
}

// ---------------------------------------------------------------------------
// Run FixedEventSource through SimEngine; return snapshot after exhaustion.
// SimEngine sets cmd.state=PAUSED when source returns false.
// ---------------------------------------------------------------------------

static viz::model::BookSnapshot run_to_completion(FixedEventSource& source) {
    viz::model::SnapshotBuffer sb;

    viz::model::PlaybackCmd cmd{};
    cmd.state      = viz::model::PlaybackState::RUNNING;
    cmd.speed_mult = 1.0f;
    std::mutex cmd_mutex;

    viz::model::SaveCmd save_cmd{};
    save_cmd.pending = false;
    std::mutex save_mutex;

    viz::model::SimEngine engine(&source, &sb, &cmd_mutex, &cmd, &save_mutex, &save_cmd);

    std::thread t(&viz::model::SimEngine::run, &engine);

    // Poll until SimEngine pauses (source exhausted) — up to 1 second.
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::lock_guard<std::mutex> lk(cmd_mutex);
        if (cmd.state == viz::model::PlaybackState::PAUSED) { break; }
    }

    engine.stop();
    t.join();

    uint32_t idx = sb.published_idx.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lk(sb.slot_mutex[idx]);
    return sb.buffers[idx];
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    static constexpr uint64_t U = 100000000ULL;   // 1 lot = 10^8
    int failures = 0;

    // -----------------------------------------------------------------------
    // Case 1: BID walks multiple ask levels
    //   ASK 200003 qty=3, ASK 200004 qty=6, BID 200005 qty=7
    //   BID matches: fill (200003,3) then (200004,4); remainder=0
    //   Book after: best_ask=200004 qty=2, best_bid=TICK_INVALID
    //   Snapshot fills[] is most-recent-first: fills[0]=(200004,4) fills[1]=(200003,3)
    // -----------------------------------------------------------------------
    {
        FixedEventSource src {
            make_event(1, 200003U, 3*U),
            make_event(1, 200004U, 6*U),
            make_event(0, 200005U, 7*U),
        };
        auto snap = run_to_completion(src);
        bool ok = true;

        if (snap.fill_count != 2U) {
            std::fprintf(stderr, "FAIL case1: fill_count=%u want 2\n", snap.fill_count);
            ok = false;
        } else {
            // fills[0] = most recent = 200004,4
            if (snap.fills[0].price_tick != 200004U || snap.fills[0].qty != 4*U) {
                std::fprintf(stderr, "FAIL case1: fills[0] tick=%u qty=%llu want 200004,4\n",
                             snap.fills[0].price_tick, (unsigned long long)snap.fills[0].qty);
                ok = false;
            }
            // fills[1] = first fill = 200003,3
            if (snap.fills[1].price_tick != 200003U || snap.fills[1].qty != 3*U) {
                std::fprintf(stderr, "FAIL case1: fills[1] tick=%u qty=%llu want 200003,3\n",
                             snap.fills[1].price_tick, (unsigned long long)snap.fills[1].qty);
                ok = false;
            }
        }
        if (snap.best_ask_tick != 200004U) {
            std::fprintf(stderr, "FAIL case1: best_ask=%u want 200004\n", snap.best_ask_tick);
            ok = false;
        }
        if (snap.best_bid_tick != eth::book::TICK_INVALID) {
            std::fprintf(stderr, "FAIL case1: best_bid=%u want TICK_INVALID\n", snap.best_bid_tick);
            ok = false;
        }
        if (ok) { std::printf("PASS case1: bid walks multiple ask levels\n"); }
        else    { ++failures; }
    }

    // -----------------------------------------------------------------------
    // Case 2: Passive BID rests — no ask in book, no fills
    //   BID 200000 qty=5 → rests; book: best_bid=200000, best_ask=TICK_INVALID
    // -----------------------------------------------------------------------
    {
        FixedEventSource src {
            make_event(0, 200000U, 5*U),
        };
        auto snap = run_to_completion(src);
        bool ok = true;

        if (snap.fill_count != 0U) {
            std::fprintf(stderr, "FAIL case2: fill_count=%u want 0\n", snap.fill_count);
            ok = false;
        }
        if (snap.best_bid_tick != 200000U) {
            std::fprintf(stderr, "FAIL case2: best_bid=%u want 200000\n", snap.best_bid_tick);
            ok = false;
        }
        if (snap.best_ask_tick != eth::book::TICK_INVALID) {
            std::fprintf(stderr, "FAIL case2: best_ask=%u want TICK_INVALID\n", snap.best_ask_tick);
            ok = false;
        }
        if (ok) { std::printf("PASS case2: passive bid rests with no fills\n"); }
        else    { ++failures; }
    }

    // -----------------------------------------------------------------------
    // Case 3: Partial fill — bid qty < ask qty; ask remainder rests
    //   ASK 200003 qty=6, BID 200005 qty=4
    //   Fill: (200003,4); ask 200003 rests at qty=2
    // -----------------------------------------------------------------------
    {
        FixedEventSource src {
            make_event(1, 200003U, 6*U),
            make_event(0, 200005U, 4*U),
        };
        auto snap = run_to_completion(src);
        bool ok = true;

        if (snap.fill_count != 1U) {
            std::fprintf(stderr, "FAIL case3: fill_count=%u want 1\n", snap.fill_count);
            ok = false;
        } else {
            if (snap.fills[0].price_tick != 200003U || snap.fills[0].qty != 4*U) {
                std::fprintf(stderr, "FAIL case3: fills[0] tick=%u qty=%llu want 200003,4\n",
                             snap.fills[0].price_tick, (unsigned long long)snap.fills[0].qty);
                ok = false;
            }
        }
        if (snap.best_ask_tick != 200003U) {
            std::fprintf(stderr, "FAIL case3: best_ask=%u want 200003\n", snap.best_ask_tick);
            ok = false;
        }
        if (snap.best_bid_tick != eth::book::TICK_INVALID) {
            std::fprintf(stderr, "FAIL case3: best_bid=%u want TICK_INVALID\n", snap.best_bid_tick);
            ok = false;
        }
        if (ok) { std::printf("PASS case3: partial fill, ask remainder rests\n"); }
        else    { ++failures; }
    }

    // -----------------------------------------------------------------------
    // Case 4: ASK walks multiple bid levels
    //   BID 200005 qty=3, BID 200004 qty=6, ASK 200003 qty=7
    //   ASK matches: fill (200005,3) then (200004,4); remainder=0
    //   Book after: best_bid=200004 qty=2, best_ask=TICK_INVALID
    //   Snapshot fills[] most-recent-first: fills[0]=(200004,4) fills[1]=(200005,3)
    // -----------------------------------------------------------------------
    {
        FixedEventSource src {
            make_event(0, 200005U, 3*U),
            make_event(0, 200004U, 6*U),
            make_event(1, 200003U, 7*U),
        };
        auto snap = run_to_completion(src);
        bool ok = true;

        if (snap.fill_count != 2U) {
            std::fprintf(stderr, "FAIL case4: fill_count=%u want 2\n", snap.fill_count);
            ok = false;
        } else {
            // fills[0] = most recent = 200004,4
            if (snap.fills[0].price_tick != 200004U || snap.fills[0].qty != 4*U) {
                std::fprintf(stderr, "FAIL case4: fills[0] tick=%u qty=%llu want 200004,4\n",
                             snap.fills[0].price_tick, (unsigned long long)snap.fills[0].qty);
                ok = false;
            }
            // fills[1] = first fill = 200005,3
            if (snap.fills[1].price_tick != 200005U || snap.fills[1].qty != 3*U) {
                std::fprintf(stderr, "FAIL case4: fills[1] tick=%u qty=%llu want 200005,3\n",
                             snap.fills[1].price_tick, (unsigned long long)snap.fills[1].qty);
                ok = false;
            }
        }
        if (snap.best_bid_tick != 200004U) {
            std::fprintf(stderr, "FAIL case4: best_bid=%u want 200004\n", snap.best_bid_tick);
            ok = false;
        }
        if (snap.best_ask_tick != eth::book::TICK_INVALID) {
            std::fprintf(stderr, "FAIL case4: best_ask=%u want TICK_INVALID\n", snap.best_ask_tick);
            ok = false;
        }
        if (ok) { std::printf("PASS case4: ask walks multiple bid levels\n"); }
        else    { ++failures; }
    }

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    if (failures == 0) {
        std::printf("PASS  all 4 matching tests\n");
        return 0;
    }
    std::fprintf(stderr, "FAIL  %d test(s) failed\n", failures);
    return 1;
}
