/* book_snapshot.hpp — crossing structs: LevelEntry, FillEntry, BookSnapshot
 *
 * These structs cross the model/view boundary.  They are the ONLY structs that
 * the view layer is permitted to read from the model layer.
 *
 * Includes: <cstdint> only.  No ImGui, no book.hpp, no other model headers.
 * Namespace: viz::model (structs live here; VIZ_LADDER_DEPTH and VIZ_TAPE_DEPTH
 *            are in namespace viz::model as constexpr values).
 */

#pragma once

#include <cstdint>

namespace viz::model {

// ---------------------------------------------------------------------------
// Compile-time depth constants.  Defined here — the single source of truth.
// All code referencing these values must include this header.
// ---------------------------------------------------------------------------

static constexpr uint32_t VIZ_LADDER_DEPTH = 50U;   // price levels per side
static constexpr uint32_t VIZ_TAPE_DEPTH   = 32U;   // most-recent fills shown

// ---------------------------------------------------------------------------
// LevelEntry  (spec: Data Model / LevelEntry)
//
// One price level as published to the render thread.
// price_f is not stored here — it is computed at render time from tick.
//
// Layout:
//   tick   offset  0 — absolute price tick
//   _pad   offset  4 — explicit pad
//   qty    offset  8 — scaled qty (10^8); 0 = empty slot
//          total: 16 bytes
// ---------------------------------------------------------------------------

struct LevelEntry {
    uint32_t  tick;   // offset 0 — absolute price tick
    uint32_t  _pad;   // offset 4 — explicit pad
    uint64_t  qty;    // offset 8 — scaled qty (10^8); 0 = empty slot
};                    // total: 16 bytes

static_assert(sizeof(LevelEntry)  == 16U, "LevelEntry layout changed");
static_assert(alignof(LevelEntry) ==  8U, "LevelEntry alignment changed");

// ---------------------------------------------------------------------------
// FillEntry  (spec: Data Model / FillEntry)
//
// One recent fill as published to the render thread.
// For Phase 1 (ETH L2 MBP with no real matcher), fills are synthesised from
// crossed-spread events.
//
// Layout:
//   timestamp_ns  offset  0 — virtual clock at fill time
//   price_tick    offset  8 — fill price tick
//   _pad          offset 12 — explicit pad
//   qty           offset 16 — filled quantity (scaled 10^8)
//                 total: 24 bytes
// ---------------------------------------------------------------------------

struct FillEntry {
    uint64_t  timestamp_ns;   // offset  0 — virtual clock at fill time
    uint32_t  price_tick;     // offset  8 — fill price tick
    uint32_t  _pad;           // offset 12 — explicit pad
    uint64_t  qty;            // offset 16 — filled quantity (scaled)
};                            // total: 24 bytes

static_assert(sizeof(FillEntry)  == 24U, "FillEntry layout changed");
static_assert(alignof(FillEntry) ==  8U, "FillEntry alignment changed");

// ---------------------------------------------------------------------------
// BookSnapshot  (spec: Data Model / BookSnapshot)
//
// Complete state the render thread reads each frame.
// Two instances live in SnapshotBuffer — total working set ~4,800 bytes,
// comfortably within a 32 KB L1 cache.
//
// Layout:
//   bids[]           offset    0 — 50 × 16 =  800 bytes
//   asks[]           offset  800 — 50 × 16 =  800 bytes
//   fills[]          offset 1600 — 32 × 24 =  768 bytes
//   fill_count       offset 2368 — 4 bytes
//   best_bid_tick    offset 2372 — 4 bytes
//   best_ask_tick    offset 2376 — 4 bytes
//   _snap_pad        offset 2380 — 4 bytes
//   virtual_clock_ns offset 2384 — 8 bytes
//   window_base_tick offset 2392 — 8 bytes
//                    total: 2400 bytes
// ---------------------------------------------------------------------------

struct BookSnapshot {
    LevelEntry  bids[VIZ_LADDER_DEPTH];    // offset    0 — best bid first
    LevelEntry  asks[VIZ_LADDER_DEPTH];    // offset  800 — best ask first
    FillEntry   fills[VIZ_TAPE_DEPTH];     // offset 1600 — most recent first
    uint32_t    fill_count;                // offset 2368 — valid entries in fills[]
    uint32_t    best_bid_tick;             // offset 2372 — TICK_INVALID if no bids
    uint32_t    best_ask_tick;             // offset 2376 — TICK_INVALID if no asks
    uint32_t    _snap_pad;                 // offset 2380 — explicit pad
    uint64_t    virtual_clock_ns;          // offset 2384 — virtual time at publication
    uint64_t    window_base_tick;          // offset 2392 — eth::book window_base
};                                         // total: 2400 bytes

static_assert(sizeof(BookSnapshot)  == 2400U, "BookSnapshot layout changed");
static_assert(alignof(BookSnapshot) ==    8U, "BookSnapshot alignment changed");

} // namespace viz::model
