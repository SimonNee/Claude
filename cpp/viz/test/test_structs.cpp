/* test_structs.cpp — static_assert size and alignment verification
 *
 * This binary verifies that every crossing struct and model struct has the
 * layout mandated by the architect spec.  All checks are static_asserts so
 * failures are compile-time, not runtime.
 *
 * At runtime, main() prints a PASS line and exits 0.
 *
 * Build flags (no LTO, no sanitizers for correctness tests per Idiom 9):
 *   -std=c++17 -O2 -march=native -Wall -Wextra
 *   -Wconversion -Wsign-conversion -Werror -fno-exceptions
 */

#include <cstdio>

// All crossing structs and model structs
#include "book_snapshot.hpp"
#include "replay_event.hpp"
#include "playback_cmd.hpp"
#include "save_cmd.hpp"
#include "snapshot_buffer.hpp"

// ---------------------------------------------------------------------------
// Compile-time size assertions — duplicated here as an independent check
// against the in-header static_asserts (belt-and-suspenders)
// ---------------------------------------------------------------------------

// LevelEntry
static_assert(sizeof(viz::model::LevelEntry)  == 16U, "LevelEntry size mismatch");
static_assert(alignof(viz::model::LevelEntry) ==  8U, "LevelEntry align mismatch");

// FillEntry
static_assert(sizeof(viz::model::FillEntry)   == 24U, "FillEntry size mismatch");
static_assert(alignof(viz::model::FillEntry)  ==  8U, "FillEntry align mismatch");

// BookSnapshot
static_assert(sizeof(viz::model::BookSnapshot)  == 2400U, "BookSnapshot size mismatch");
static_assert(alignof(viz::model::BookSnapshot) ==    8U, "BookSnapshot align mismatch");

// ReplayEvent
static_assert(sizeof(viz::model::ReplayEvent)  == 32U, "ReplayEvent size mismatch");
static_assert(alignof(viz::model::ReplayEvent) ==  8U, "ReplayEvent align mismatch");

// PlaybackCmd
static_assert(sizeof(viz::model::PlaybackCmd)  == 8U, "PlaybackCmd size mismatch");
static_assert(alignof(viz::model::PlaybackCmd) == 4U, "PlaybackCmd align mismatch");

// SaveCmd
static_assert(sizeof(viz::model::SaveCmd)  == 520U, "SaveCmd size mismatch");
static_assert(alignof(viz::model::SaveCmd) ==   1U, "SaveCmd align mismatch");

// VIZ_LADDER_DEPTH and VIZ_TAPE_DEPTH
static_assert(viz::model::VIZ_LADDER_DEPTH == 50U, "VIZ_LADDER_DEPTH mismatch");
static_assert(viz::model::VIZ_TAPE_DEPTH   == 32U, "VIZ_TAPE_DEPTH mismatch");

// Verify BookSnapshot internal offsets via offsetof
static_assert(offsetof(viz::model::BookSnapshot, bids)             ==    0U);
static_assert(offsetof(viz::model::BookSnapshot, asks)             ==  800U);
static_assert(offsetof(viz::model::BookSnapshot, fills)            == 1600U);
static_assert(offsetof(viz::model::BookSnapshot, fill_count)       == 2368U);
static_assert(offsetof(viz::model::BookSnapshot, best_bid_tick)    == 2372U);
static_assert(offsetof(viz::model::BookSnapshot, best_ask_tick)    == 2376U);
static_assert(offsetof(viz::model::BookSnapshot, _snap_pad)        == 2380U);
static_assert(offsetof(viz::model::BookSnapshot, virtual_clock_ns) == 2384U);
static_assert(offsetof(viz::model::BookSnapshot, window_base_tick) == 2392U);

// Verify LevelEntry offsets
static_assert(offsetof(viz::model::LevelEntry, tick) == 0U);
static_assert(offsetof(viz::model::LevelEntry, _pad) == 4U);
static_assert(offsetof(viz::model::LevelEntry, qty)  == 8U);

// Verify FillEntry offsets
static_assert(offsetof(viz::model::FillEntry, timestamp_ns) ==  0U);
static_assert(offsetof(viz::model::FillEntry, price_tick)   ==  8U);
static_assert(offsetof(viz::model::FillEntry, _pad)         == 12U);
static_assert(offsetof(viz::model::FillEntry, qty)          == 16U);

// Verify ReplayEvent offsets
static_assert(offsetof(viz::model::ReplayEvent, timestamp_ns) ==  0U);
static_assert(offsetof(viz::model::ReplayEvent, tick)         ==  8U);
static_assert(offsetof(viz::model::ReplayEvent, _pad)         == 12U);
static_assert(offsetof(viz::model::ReplayEvent, qty)          == 16U);
static_assert(offsetof(viz::model::ReplayEvent, side)         == 24U);

// Verify PlaybackCmd offsets
static_assert(offsetof(viz::model::PlaybackCmd, state)      == 0U);
static_assert(offsetof(viz::model::PlaybackCmd, speed_mult) == 4U);

// Verify SaveCmd offsets
static_assert(offsetof(viz::model::SaveCmd, pending) ==   0U);
static_assert(offsetof(viz::model::SaveCmd, path)    ==   8U);

// ---------------------------------------------------------------------------
// Runtime entry point
// ---------------------------------------------------------------------------

int main() {
    std::printf("PASS  all static_assert sizes\n");
    return 0;
}
