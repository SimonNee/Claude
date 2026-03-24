/* sim_engine.cpp — SimEngine implementation
 *
 * Sim thread: consume events, dispatch into book, publish snapshots.
 * Virtual-clock pacing: sleep between events proportional to timestamp delta
 * divided by speed_mult.
 *
 * No exceptions. -fno-exceptions compliant.
 * Pitfall 4: no virtual on hot-path structs.
 * Pitfall 7: PlaybackCmd read under mutex by value — no dangling reference.
 */

#include "sim_engine.hpp"
#include "ou_event_source.hpp"

#include <chrono>
#include <cstring>
#include <thread>

namespace viz::model {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SimEngine::SimEngine(IEventSource*   source,
                     SnapshotBuffer* sb,
                     std::mutex*     cmd_mutex,
                     PlaybackCmd*    cmd,
                     std::mutex*     save_mutex,
                     SaveCmd*        save_cmd)
    : source_(source)
    , sb_(sb)
    , cmd_mutex_(cmd_mutex)
    , cmd_(cmd)
    , save_mutex_(save_mutex)
    , save_cmd_(save_cmd)
    , book_(eth::book::NULL_BASE_TICK)
    , virtual_clock_ns_(0U)
    , prev_event_ns_(0U)
    , fill_head_(0U)
    , fill_count_(0U)
    , prev_best_bid_(eth::book::TICK_INVALID)
    , prev_best_ask_(eth::book::TICK_INVALID)
    , live_source_(source->is_live())
    , stop_flag_(false)
{
    // Zero-initialise fill ring buffer.
    std::memset(fill_ring_, 0, sizeof(fill_ring_));
}

// ---------------------------------------------------------------------------
// stop — signal the sim thread to exit
// ---------------------------------------------------------------------------

void SimEngine::stop() {
    stop_flag_.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// run — sim thread entry point
// ---------------------------------------------------------------------------

void SimEngine::run() {
    while (!stop_flag_.load(std::memory_order_acquire)) {

        // ---- Read PlaybackCmd under mutex ----
        PlaybackState state    = PlaybackState::PAUSED;
        float         speed    = 1.0f;
        {
            std::lock_guard<std::mutex> lk(*cmd_mutex_);
            state = cmd_->state;
            speed = cmd_->speed_mult;
        }

        if (state == PlaybackState::PAUSED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // ---- Poll SaveCmd under mutex ----
        {
            std::lock_guard<std::mutex> lk(*save_mutex_);
            if (save_cmd_->pending) {
                // Attempt dynamic_cast: only succeeds if source is OUEventSource.
                OUEventSource* ou = dynamic_cast<OUEventSource*>(source_);
                if (ou != nullptr) {
                    bool ok = ou->save_csv(save_cmd_->path);
                    if (!ok) {
                        std::fprintf(stderr, "SimEngine: save_csv failed\n");
                    }
                }
                save_cmd_->pending = false;
            }
        }

        // ---- Fetch next event ----
        ReplayEvent ev{};
        if (!source_->next_event(ev)) {
            if (live_source_) {
                // Live feed: empty queue means no event yet — not exhausted.
                // Sleep briefly and retry; do NOT set PAUSED.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                // Finite source exhausted: pause and wait for user action.
                std::lock_guard<std::mutex> lk(*cmd_mutex_);
                cmd_->state = PlaybackState::PAUSED;
            }
            continue;
        }

        // ---- Virtual-clock pacing (skipped for live sources) ----
        // KDB+ live feed provides its own pacing via .z.ts timer.
        // Applying sleep_for on top would double-pace.
        if (!live_source_ && ev.timestamp_ns > prev_event_ns_ && prev_event_ns_ != 0U) {
            uint64_t delta_ns = ev.timestamp_ns - prev_event_ns_;
            // speed > 0 guard: speed_mult range [0.0625, 16.0] per spec.
            if (speed > 0.0f) {
                // Divide delta by speed multiplier: faster replay = shorter sleep.
                double sleep_d = static_cast<double>(delta_ns) / static_cast<double>(speed);
                auto sleep_ns = static_cast<uint64_t>(sleep_d);
                if (sleep_ns > 0U) {
                    std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
                }
            }
        }
        prev_event_ns_ = ev.timestamp_ns;

        // ---- Advance virtual clock ----
        virtual_clock_ns_ = ev.timestamp_ns;

        // ---- Dispatch event into book ----
        dispatch_event(ev);

        // ---- Publish snapshot ----
        publish_snapshot();
    }
}

// ---------------------------------------------------------------------------
// dispatch_event — convert ReplayEvent into a book upsert/delete
// ---------------------------------------------------------------------------

bool SimEngine::dispatch_event(const ReplayEvent& e) {
    // Convert uint8_t side to eth::book::side_t — single conversion site.
    eth::book::side_t book_side = (e.side == 0U)
                                  ? eth::book::side_t::BID
                                  : eth::book::side_t::ASK;

    // Initial rebase: book is constructed with NULL_BASE_TICK; upsert_by_tick
    // returns false until the window is positioned.  On the first valid event,
    // centre the window on the event's tick.
    if (book_.window_base() == eth::book::NULL_BASE_TICK) {
        uint64_t base = (static_cast<uint64_t>(e.tick) >= eth::book::WINDOW_SIZE / 2U)
                        ? static_cast<uint64_t>(e.tick) - eth::book::WINDOW_SIZE / 2U
                        : 0U;
        book_.rebase(base);
    }

    bool ok = book_.upsert_by_tick(book_side, e.tick, e.qty);

    // Ongoing rebase: keep the window centred as price moves.
    if (ok && book_.needs_rebase()) {
        eth::book::tick_t bb = book_.best_bid();
        eth::book::tick_t ba = book_.best_ask();
        uint64_t mid = (bb != eth::book::TICK_INVALID && ba != eth::book::TICK_INVALID)
                       ? (static_cast<uint64_t>(bb) + static_cast<uint64_t>(ba)) / 2U
                       : (bb != eth::book::TICK_INVALID
                           ? static_cast<uint64_t>(bb)
                           : static_cast<uint64_t>(ba));
        uint64_t new_base = (mid >= eth::book::WINDOW_SIZE / 2U)
                            ? mid - eth::book::WINDOW_SIZE / 2U
                            : 0U;
        book_.rebase(new_base);
    }

    return ok;
}

// ---------------------------------------------------------------------------
// publish_snapshot — copy book state into inactive SnapshotBuffer slot
// ---------------------------------------------------------------------------

void SimEngine::publish_snapshot() {
    // Triple-buffer scheme: write to scratch_idx (private to sim thread,
    // not visible to reader until published_idx is atomically swapped).
    // Acquire the per-slot mutex before writing so TSan can track the edge.
    uint32_t wi = sb_->scratch_idx;

    std::lock_guard<std::mutex> slot_lk(sb_->slot_mutex[wi]);
    BookSnapshot& snap = sb_->buffers[wi];

    // Zero out the snapshot fields we populate (level counts may vary).
    // Using explicit zero rather than memset to avoid potential over-zeroing
    // of the entire 2400-byte struct each frame.
    snap.fill_count       = 0U;
    snap.best_bid_tick    = eth::book::TICK_INVALID;
    snap.best_ask_tick    = eth::book::TICK_INVALID;
    snap._snap_pad        = 0U;
    snap.virtual_clock_ns = virtual_clock_ns_;
    snap.window_base_tick = book_.window_base();

    const eth::book::tick_t bb = book_.best_bid();
    const eth::book::tick_t ba = book_.best_ask();

    snap.best_bid_tick = bb;
    snap.best_ask_tick = ba;

    // ---- Bids: walk down from best_bid, filling up to VIZ_LADDER_DEPTH ----
    uint32_t bid_count = 0U;
    if (bb != eth::book::TICK_INVALID) {
        uint64_t window_base = book_.window_base();
        // window_base is NULL_BASE_TICK if uninitialised; guard against that.
        if (window_base != eth::book::NULL_BASE_TICK) {
            // Walk down from best_bid filling ladder.
            // Guard: tick must not underflow below window_base.
            uint32_t tick = bb;
            while (bid_count < VIZ_LADDER_DEPTH) {
                eth::book::qty_t qty =
                    book_.level_qty(eth::book::side_t::BID, tick);
                if (qty > 0U) {
                    snap.bids[bid_count].tick = tick;
                    snap.bids[bid_count]._pad = 0U;
                    snap.bids[bid_count].qty  = qty;
                    ++bid_count;
                }
                // Stop if we'd underflow below window base.
                if (tick == 0U || static_cast<uint64_t>(tick) <= window_base) {
                    break;
                }
                --tick;
            }
        }
    }
    // Zero out unused bid slots.
    for (uint32_t i = bid_count; i < VIZ_LADDER_DEPTH; ++i) {
        snap.bids[i].tick = 0U;
        snap.bids[i]._pad = 0U;
        snap.bids[i].qty  = 0U;
    }

    // ---- Asks: walk up from best_ask, filling up to VIZ_LADDER_DEPTH ----
    uint32_t ask_count = 0U;
    if (ba != eth::book::TICK_INVALID) {
        uint64_t window_base = book_.window_base();
        if (window_base != eth::book::NULL_BASE_TICK) {
            uint32_t tick    = ba;
            uint32_t max_tick = static_cast<uint32_t>(
                window_base + static_cast<uint64_t>(eth::book::WINDOW_MASK));
            while (ask_count < VIZ_LADDER_DEPTH && tick <= max_tick) {
                eth::book::qty_t qty =
                    book_.level_qty(eth::book::side_t::ASK, tick);
                if (qty > 0U) {
                    snap.asks[ask_count].tick = tick;
                    snap.asks[ask_count]._pad = 0U;
                    snap.asks[ask_count].qty  = qty;
                    ++ask_count;
                }
                if (tick == 0xFFFFFFFFU) { break; }   // prevent uint32_t overflow
                ++tick;
            }
        }
    }
    // Zero out unused ask slots.
    for (uint32_t i = ask_count; i < VIZ_LADDER_DEPTH; ++i) {
        snap.asks[i].tick = 0U;
        snap.asks[i]._pad = 0U;
        snap.asks[i].qty  = 0U;
    }

    // ---- Synthesised fills ----
    // Emit a tape entry when:
    //   (a) spread crossed (bb >= ba) — clear trade signal, or
    //   (b) best bid changes — top level moved, likely a trade on the bid side, or
    //   (c) best ask changes — top level moved, likely a trade on the ask side.
    // ETH L2 MBP has no real fills; this is an approximation labelled "estimated".
    bool bid_moved = (bb != eth::book::TICK_INVALID) && (bb != prev_best_bid_);
    bool ask_moved = (ba != eth::book::TICK_INVALID) && (ba != prev_best_ask_);
    bool crossed   = (bb != eth::book::TICK_INVALID) &&
                     (ba != eth::book::TICK_INVALID) &&
                     (bb >= ba);

    if (crossed || bid_moved || ask_moved) {
        // Use the best bid as fill price when bid moved, best ask when ask moved,
        // mid when crossed.
        uint32_t fill_tick;
        if (crossed) {
            fill_tick = bb / 2U + ba / 2U + ((bb & 1U) & (ba & 1U));
        } else if (bid_moved && bb != eth::book::TICK_INVALID) {
            fill_tick = bb;
        } else {
            fill_tick = ba;
        }

        // Determine fill qty from the book level that triggered the fill.
        eth::book::qty_t fill_qty;
        if (crossed) {
            eth::book::qty_t bq = book_.level_qty(eth::book::side_t::BID, bb);
            eth::book::qty_t aq = book_.level_qty(eth::book::side_t::ASK, ba);
            fill_qty = (bq < aq) ? bq : aq;
        } else if (bid_moved) {
            fill_qty = book_.level_qty(eth::book::side_t::BID, bb);
        } else {
            fill_qty = book_.level_qty(eth::book::side_t::ASK, ba);
        }
        if (fill_qty == 0U) { fill_qty = 100000000ULL; }  // fallback: 1 unit

        FillEntry fe{};
        fe.timestamp_ns = virtual_clock_ns_;
        fe.price_tick   = fill_tick;
        fe._pad         = 0U;
        fe.qty          = fill_qty;

        fill_ring_[fill_head_] = fe;
        fill_head_ = (fill_head_ + 1U) % VIZ_TAPE_DEPTH;
        if (fill_count_ < VIZ_TAPE_DEPTH) {
            ++fill_count_;
        }
    }

    prev_best_bid_ = bb;
    prev_best_ask_ = ba;

    // ---- Copy fill ring into snapshot (most-recent first) ----
    uint32_t copy_count = fill_count_;
    if (copy_count > VIZ_TAPE_DEPTH) {
        copy_count = VIZ_TAPE_DEPTH;
    }
    snap.fill_count = copy_count;
    for (uint32_t i = 0U; i < copy_count; ++i) {
        // fill_head_ points at the next write slot; walk backwards for most-recent-first.
        uint32_t ring_idx = (fill_head_ + VIZ_TAPE_DEPTH - 1U - i) % VIZ_TAPE_DEPTH;
        snap.fills[i] = fill_ring_[ring_idx];
    }
    // Zero out unused fill slots.
    for (uint32_t i = copy_count; i < VIZ_TAPE_DEPTH; ++i) {
        snap.fills[i] = FillEntry{};
    }

    // ---- Publish: atomically swap scratch and published slots ----
    // slot_lk is released here (end of scope via RAII) BEFORE publishing,
    // ensuring the write is fully visible before the reader can acquire the mutex.
    // Actually we need to release after publishing — keep slot_lk alive until
    // after the store so the reader's mutex acquire happens-after the write.
    // The slot_lk destructor runs at end of this function scope.

    // Store the new published index with release ordering.
    uint32_t old_pub = sb_->published_idx.exchange(wi, std::memory_order_release);
    // old_pub is now the new scratch; it is safe to use next time because
    // the reader cannot acquire it without first loading published_idx, which
    // now points to wi (the just-written buffer).
    sb_->scratch_idx = old_pub;   // private to sim thread

    // Increment generation counter so the render thread can detect new data.
    sb_->generation.fetch_add(1U, std::memory_order_release);
}

} // namespace viz::model
