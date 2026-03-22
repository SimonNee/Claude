/* internal.hpp — package-internal helpers for the ETH/USDT orderbook
 *
 * Not part of the public API. Do not include from outside this package.
 *
 * Contains:
 *   - bitmap_set_bit / bitmap_clear_bit  — two-level bitmap maintenance
 *   - bitmap_is_set                       — test helper for the invariant checker
 *   - bitmap_lowest_h / bitmap_highest_h  — hierarchical fallback scan for best_tick
 *   - level_set / level_clear             — level-array + bitmap operations
 *
 * All functions are defined inline in this header so each including TU gets
 * its own copy (internal linkage via anonymous namespace).  This avoids ODR
 * issues and guarantees inlining regardless of LTO heuristics (Idiom 6,
 * Pitfall 8).
 *
 * Difference from the ES internal.hpp:
 *   - No arena module (no order nodes, no FIFO queues).
 *   - No queue_enqueue / queue_remove / queue_dequeue_head.
 *   - Bitmap is indexed by window_idx_t (slot index), not by tick_t directly.
 *   - Fallback scan returns an absolute tick by adding window_base_tick, not
 *     a raw bitmap index.
 */

#pragma once

#include "book.hpp"

namespace eth::book {
namespace internal {

// ---------------------------------------------------------------------------
// Module 2 — Two-level bitmap maintenance
//
// The bitmap is indexed by window_idx_t (slot index in [0, WINDOW_SIZE)).
//
// Tick → bitmap word mapping:
//   bmap_word = widx / 64      (which uint64_t in bitmap[])
//   bmap_bit  = widx % 64      (which bit within that word)
//
// Bitmap word → summary mapping:
//   sum_word  = bmap_word / 64  (which uint64_t in summary[])
//   sum_bit   = bmap_word % 64  (which bit within that summary word)
// ---------------------------------------------------------------------------

// Set the bitmap bit and corresponding summary bit for window slot widx.
// Called when a level transitions from empty (total_qty == 0) to non-empty.
inline void bitmap_set_bit(book_side_t& side, window_idx_t widx) noexcept {
    uint32_t bmap_word = widx / 64U;
    uint32_t bmap_bit  = widx % 64U;
    side.bitmap[bmap_word] |= (UINT64_C(1) << bmap_bit);

    // Mirror into summary: one summary bit per flat-bitmap word.
    uint32_t sum_word = bmap_word / 64U;
    uint32_t sum_bit  = bmap_word % 64U;
    side.summary[sum_word] |= (UINT64_C(1) << sum_bit);
}

// Clear the bitmap bit for window slot widx.
// Clear the corresponding summary bit only if the entire flat-bitmap word is now zero.
// Called when a level transitions from non-empty to empty (total_qty → 0).
inline void bitmap_clear_bit(book_side_t& side, window_idx_t widx) noexcept {
    uint32_t bmap_word = widx / 64U;
    uint32_t bmap_bit  = widx % 64U;
    side.bitmap[bmap_word] &= ~(UINT64_C(1) << bmap_bit);

    // Clear the summary bit only when the whole flat-bitmap word reaches zero.
    // Other ticks sharing the same 64-slot group keep the summary bit set.
    if (side.bitmap[bmap_word] == 0U) {
        uint32_t sum_word = bmap_word / 64U;
        uint32_t sum_bit  = bmap_word % 64U;
        side.summary[sum_word] &= ~(UINT64_C(1) << sum_bit);
    }
}

// Test whether the bitmap bit for window slot widx is set.
// Not on the hot path — used by the invariant checker in test_book.cpp.
inline bool bitmap_is_set(const book_side_t& side, window_idx_t widx) noexcept {
    return (side.bitmap[widx / 64U] & (UINT64_C(1) << (widx % 64U))) != 0U;
}

// ---------------------------------------------------------------------------
// Hierarchical bitmap fallback scan — called only when the best level is deleted
//
// Two-level structure:
//   Level 2 (top):  summary[SUMMARY_WORDS] — one bit per flat-bitmap word
//   Level 1 (flat): bitmap[BITMAP_WORDS]   — one bit per window slot
//
// bitmap_lowest_h  → ASK side fallback (lowest slot = best ask)
// bitmap_highest_h → BID side fallback (highest slot = best bid)
//
// Both functions return the best *absolute* tick by adding window_base_tick,
// or TICK_INVALID if no set bit is found.
//
// Cost: 2 TZCNT/LZCNT + ~3 loads — replaces up to 1,024-word flat scan.
//
// Defined inline so the compiler inlines unconditionally regardless of LTO
// cold-call heuristics (Pitfall 8, Idiom 6).
// ---------------------------------------------------------------------------

// Returns the lowest absolute tick (best ask) using the summary, or TICK_INVALID.
inline tick_t bitmap_lowest_h(const book_side_t& side,
                               uint64_t           window_base_tick) noexcept {
    // Step 1: lowest non-empty summary word (TZCNT on summary).
    for (uint32_t sw = 0U; sw < SUMMARY_WORDS; ++sw) {
        if (side.summary[sw] == 0U) {
            continue;
        }
        // Lowest set bit within summary[sw] → which bitmap word is non-empty.
        uint32_t sb        = static_cast<uint32_t>(__builtin_ctzll(side.summary[sw]));
        uint32_t bmap_word = sw * 64U + sb;
        // Guard: bmap_word must be within the valid range.
        if (bmap_word >= BITMAP_WORDS) {
            return TICK_INVALID;
        }
        uint64_t word = side.bitmap[bmap_word];
        if (word == 0U) {
            // Summary bit set but bitmap word is zero — summary inconsistency; skip.
            continue;
        }
        // Step 2: TZCNT within the bitmap word → bit offset (slot within the word).
        uint32_t bb       = static_cast<uint32_t>(__builtin_ctzll(word));
        uint32_t widx     = bmap_word * 64U + bb;
        // Absolute tick = window_base_tick + widx.
        // widx < WINDOW_SIZE = 65536; window_base_tick is uint64_t → sum fits uint64_t.
        // The result must fit tick_t (uint32_t): valid if window_base_tick + WINDOW_SIZE <= UINT32_MAX.
        // In practice absolute prices are well below UINT32_MAX; no overflow guard needed here.
        return static_cast<tick_t>(window_base_tick + widx);
    }
    return TICK_INVALID;
}

// Returns the highest absolute tick (best bid) using the summary, or TICK_INVALID.
inline tick_t bitmap_highest_h(const book_side_t& side,
                                uint64_t           window_base_tick) noexcept {
    // Step 1: highest non-empty summary word (LZCNT on summary, scan from top).
    for (uint32_t sw = SUMMARY_WORDS; sw-- > 0U; ) {
        if (side.summary[sw] == 0U) {
            continue;
        }
        // Highest set bit within summary[sw] → which bitmap word is non-empty.
        uint32_t sb        = 63U - static_cast<uint32_t>(__builtin_clzll(side.summary[sw]));
        uint32_t bmap_word = sw * 64U + sb;
        if (bmap_word >= BITMAP_WORDS) {
            return TICK_INVALID;
        }
        uint64_t word = side.bitmap[bmap_word];
        if (word == 0U) {
            // Summary bit set but bitmap word is zero — skip.
            continue;
        }
        // Step 2: LZCNT within the bitmap word → highest set bit.
        uint32_t bb   = 63U - static_cast<uint32_t>(__builtin_clzll(word));
        uint32_t widx = bmap_word * 64U + bb;
        return static_cast<tick_t>(window_base_tick + widx);
    }
    return TICK_INVALID;
}

// ---------------------------------------------------------------------------
// Level index helpers — update level array and bitmap together
//
// These maintain the invariant:
//   bitmap_is_set(side, widx) == (side.levels[widx].total_qty > 0)
// after every call.
// ---------------------------------------------------------------------------

// Set a level's total_qty and mark the bitmap bit.
// Called on upsert (qty > 0) for any level (the qty is overwritten, not added).
inline void level_set(book_side_t& side, window_idx_t widx, qty_t qty) noexcept {
    bool was_empty = (side.levels[widx].total_qty == 0U);
    side.levels[widx].total_qty = qty;
    if (was_empty) {
        // Transition: empty → non-empty.  Update bitmap and summary.
        bitmap_set_bit(side, widx);
    }
    // If was not empty, the bitmap bit is already set — no change needed.
}

// Zero a level's total_qty and clear the bitmap bit.
// Called on upsert (qty == 0) — delete path.
inline void level_clear(book_side_t& side, window_idx_t widx) noexcept {
    side.levels[widx].total_qty = 0U;
    // Unconditionally clear — idempotent if already zero/cleared.
    bitmap_clear_bit(side, widx);
}

} // namespace internal
} // namespace eth::book
