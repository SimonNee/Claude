/* internal.hpp — package-internal helpers shared by book.cpp and matcher.cpp
 *
 * Not part of the public API. Do not include from outside this package.
 *
 * Contains: bitmap operations (Module 3), arena accessors (Module 1),
 *           level queue operations (Module 2).
 *
 * All functions are defined inline in this header so that each including TU
 * gets its own copy (internal linkage via anonymous namespace). This avoids
 * ODR issues and guarantees inlining regardless of LTO heuristics (Idiom 6,
 * Pitfall 8).
 *
 * Combined best-bid/ask strategy:
 *
 *   level_set / level_clear maintain BOTH the flat bitmap AND summary[].
 *   queue_remove / queue_dequeue_head accept the full book_side_t& so they can
 *   update best_tick on drain using the hierarchical fallback (bitmap_highest_h /
 *   bitmap_lowest_h) — 2 TZCNT/LZCNT, not the 138-word flat scan.
 *
 *   IsBid template parameter selects the correct hierarchical query:
 *     IsBid = true  → bitmap_highest_h (BID side: highest tick is best)
 *     IsBid = false → bitmap_lowest_h  (ASK side: lowest tick is best)
 */

#pragma once

#include "book.hpp"

namespace es::book {
namespace internal {

// ---------------------------------------------------------------------------
// Module 3 — Bitmap + Summary (combined level_set / level_clear)
//
// level_set and level_clear replace the old bitmap_set / bitmap_clear.
// They maintain both the flat bitmap[] and the two-level summary[] atomically.
//
// Tick→bitmap word mapping:
//   bmap_word = tick / 64          (which uint64_t in bitmap[])
//   bmap_bit  = tick % 64          (which bit within that word)
//
// Bitmap word→summary mapping:
//   sum_word  = bmap_word / 64     (which uint64_t in summary[])
//   sum_bit   = bmap_word % 64     (which bit within that summary word)
// ---------------------------------------------------------------------------

// Set the bit for 'tick' in both bitmap[] and summary[].
// Called on every add when a level transitions from empty to non-empty.
inline void level_set(book_side_t& side, tick_t tick) noexcept {
    uint32_t bmap_word = tick / 64U;
    uint32_t bmap_bit  = tick % 64U;
    side.bitmap[bmap_word] |= (UINT64_C(1) << bmap_bit);

    // Mirror into summary: one summary bit per flat-bitmap word.
    uint32_t sum_word = bmap_word / 64U;
    uint32_t sum_bit  = bmap_word % 64U;
    side.summary[sum_word] |= (UINT64_C(1) << sum_bit);
}

// Clear the bit for 'tick' in bitmap[].
// Clear the corresponding summary bit ONLY if the entire flat-bitmap word is now zero.
// Called when a level transitions from non-empty to empty.
inline void level_clear(book_side_t& side, tick_t tick) noexcept {
    uint32_t bmap_word = tick / 64U;
    uint32_t bmap_bit  = tick % 64U;
    side.bitmap[bmap_word] &= ~(UINT64_C(1) << bmap_bit);

    // Clear the summary bit only when the whole flat-bitmap word reaches zero.
    // If other ticks in the same 64-word group are still active, the summary
    // bit must remain set.
    if (side.bitmap[bmap_word] == 0U) {
        uint32_t sum_word = bmap_word / 64U;
        uint32_t sum_bit  = bmap_word % 64U;
        side.summary[sum_word] &= ~(UINT64_C(1) << sum_bit);
    }
}

// ---------------------------------------------------------------------------
// Retained for the invariant checker in test_book.cpp, which calls
// bitmap_is_set() directly on the flat bitmap.  Not on the hot path.
// ---------------------------------------------------------------------------

inline bool bitmap_is_set(const uint64_t* bitmap, tick_t tick) noexcept {
    return (bitmap[tick / 64U] & (UINT64_C(1) << (tick % 64U))) != 0U;
}

// ---------------------------------------------------------------------------
// Module 1 — Arena
// ---------------------------------------------------------------------------

// Allocate a new node for an order. Returns NULL_IDX if arena is full.
inline slot_idx_t arena_alloc(arena_t& arena, qty_t quantity) noexcept {
    if (arena.next_slot >= MAX_ORDERS) {
        return NULL_IDX;
    }
    slot_idx_t    slot = arena.next_slot++;
    order_node_t& node = arena.nodes[slot];
    node.order_id      = slot;
    node.quantity      = quantity;
    node.next_idx      = NULL_IDX;
    node.flags         = 0U;
    node._pad[0]       = 0U;
    node._pad[1]       = 0U;
    node._pad[2]       = 0U;
    return slot;
}

// ---------------------------------------------------------------------------
// Module 2 — Level Queue
// ---------------------------------------------------------------------------

// Enqueue a newly allocated node at the FIFO tail.
// Precondition: slot was just returned by arena_alloc; node fields are set.
// Does NOT update best_tick — the caller (book_add_impl) does that after
// calling queue_enqueue, because best_tick depends on the tick value and
// the side direction, which the queue layer does not know.
inline void queue_enqueue(price_level_t& level,
                          arena_t&       arena,
                          slot_idx_t     slot) noexcept {
    order_node_t& node = arena.nodes[slot];
    if (level.head_idx == NULL_IDX) {
        level.head_idx = slot;
        level.tail_idx = slot;
    } else {
        arena.nodes[level.tail_idx].next_idx = slot;
        level.tail_idx = slot;
    }
    level.count     += 1U;
    level.total_qty += node.quantity;
}

// Partially fill the head node (does not dequeue). Updates level total_qty.
// Precondition: fill_qty < node.quantity (partial fill only).
// Does NOT affect best_tick — a partial fill leaves the level occupied.
inline void queue_partial_fill_head(price_level_t& level,
                                    arena_t&       arena,
                                    qty_t          fill_qty) noexcept {
    order_node_t& node  = arena.nodes[level.head_idx];
    node.quantity      -= fill_qty;
    level.total_qty    -= fill_qty;
}

// ---------------------------------------------------------------------------
// queue_dequeue_head — templated on IsBid
//
// Dequeues the head node, marks it DEAD, and maintains:
//   - flat bitmap[] via level_clear (when level empties)
//   - summary[]   via level_clear (when the flat-bitmap word reaches zero)
//   - best_tick   via hierarchical fallback (when best level drains)
//
// Template parameter IsBid selects the correct best-price direction:
//   IsBid = true  → BID side: best is highest tick → bitmap_highest_h
//   IsBid = false → ASK side: best is lowest  tick → bitmap_lowest_h
//
// Fast path (level still has orders after dequeue): best_tick unchanged.
// Fallback path (level empties AND tick == best_tick): hierarchical scan.
//
// Returns pointer to the dequeued node (remains in arena, marked DEAD).
// Returns nullptr if level is empty (should not occur on the hot path).
// ---------------------------------------------------------------------------

template<bool IsBid>
inline order_node_t* queue_dequeue_head(book_side_t&   bside,
                                        price_level_t& level,
                                        arena_t&       arena,
                                        tick_t         tick) noexcept {
    if (level.head_idx == NULL_IDX) {
        return nullptr;
    }
    slot_idx_t    slot = level.head_idx;
    order_node_t& node = arena.nodes[slot];

    level.head_idx = node.next_idx;
    if (level.head_idx == NULL_IDX) {
        level.tail_idx = NULL_IDX;
    }
    level.count     -= 1U;
    level.total_qty -= node.quantity;
    node.flags = static_cast<uint8_t>(node.flags | DEAD_FLAG);
    node.next_idx   = NULL_IDX;

    if (level.count == 0U) {
        // The level has just drained: clear both flat bitmap and summary.
        level_clear(bside, tick);

        // FALLBACK PATH: if this was the best level, find the new best
        // using the hierarchical bitmap (2 TZCNT/LZCNT) instead of a
        // 138-word flat scan.
        if (tick == bside.best_tick) {
            int new_best;
            if (IsBid) {
                // BID: highest remaining tick is the new best bid.
                new_best = bitmap_highest_h(bside);
            } else {
                // ASK: lowest remaining tick is the new best ask.
                new_best = bitmap_lowest_h(bside);
            }
            // new_best == -1 means the side is now empty.
            bside.best_tick = (new_best >= 0)
                              ? static_cast<tick_t>(new_best)
                              : TICK_INVALID;
        }
        // If the drained level was NOT the best level (it was behind the
        // best), best_tick is unaffected — no scan needed at all.
    }
    // If the level still has orders (count > 0), best_tick is unchanged.

    return &node;
}

// ---------------------------------------------------------------------------
// queue_remove — templated on IsBid
//
// Remove an arbitrary node from the FIFO by order_id (cancel path).
// O(q) predecessor scan for mid-queue cancel (spec: book_cancel implementation
// note).  Head-cancel special case is O(1).
//
// Maintains flat bitmap[], summary[], and best_tick with the same drain logic
// as queue_dequeue_head<IsBid>.
//
// Returns true on success; false if the node is not found in this queue.
// ---------------------------------------------------------------------------

template<bool IsBid>
inline bool queue_remove(book_side_t&   bside,
                         price_level_t& level,
                         arena_t&       arena,
                         tick_t         tick,
                         order_id_t     order_id) noexcept {
    if (level.head_idx == NULL_IDX) {
        return false;
    }

    // Helper lambda to apply drain logic after the node is unlinked.
    // Called only when level.count reaches zero after the removal.
    // Defined as a lambda here to avoid duplicating the drain logic in both
    // the head-cancel and general-case paths.
    auto on_drain = [&]() noexcept {
        // Synchronous bitmap + summary clear.
        level_clear(bside, tick);

        // FALLBACK PATH: hierarchical rescan for new best_tick.
        if (tick == bside.best_tick) {
            int new_best;
            if (IsBid) {
                new_best = bitmap_highest_h(bside);
            } else {
                new_best = bitmap_lowest_h(bside);
            }
            bside.best_tick = (new_best >= 0)
                              ? static_cast<tick_t>(new_best)
                              : TICK_INVALID;
        }
    };

    // O(1) head-cancel special case (spec: book_cancel special case)
    if (level.head_idx == order_id) {
        order_node_t& node = arena.nodes[order_id];
        level.head_idx     = node.next_idx;
        if (level.head_idx == NULL_IDX) {
            level.tail_idx = NULL_IDX;
        }
        level.count     -= 1U;
        level.total_qty -= node.quantity;
        node.flags = static_cast<uint8_t>(node.flags | DEAD_FLAG);
        node.next_idx   = NULL_IDX;
        if (level.count == 0U) {
            on_drain();
        }
        return true;
    }

    // General case: O(q) predecessor scan
    slot_idx_t prev_slot = level.head_idx;
    while (true) {
        order_node_t& prev = arena.nodes[prev_slot];
        if (prev.next_idx == NULL_IDX) {
            return false;  // order_id not in this queue
        }
        if (prev.next_idx == order_id) {
            order_node_t& node = arena.nodes[order_id];
            prev.next_idx      = node.next_idx;
            if (node.next_idx == NULL_IDX) {
                level.tail_idx = prev_slot;
            }
            level.count     -= 1U;
            level.total_qty -= node.quantity;
            node.flags = static_cast<uint8_t>(node.flags | DEAD_FLAG);
            node.next_idx   = NULL_IDX;
            if (level.count == 0U) {
                on_drain();
            }
            return true;
        }
        prev_slot = prev.next_idx;
    }
}

} // namespace internal
} // namespace es::book
