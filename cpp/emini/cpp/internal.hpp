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
 */

#pragma once

#include "book.hpp"

namespace es::book {
namespace internal {

// ---------------------------------------------------------------------------
// Module 3 — Bitmap
// ---------------------------------------------------------------------------

inline void bitmap_set(uint64_t* bitmap, tick_t tick) noexcept {
    bitmap[tick / 64U] |= (UINT64_C(1) << (tick % 64U));
}

inline void bitmap_clear(uint64_t* bitmap, tick_t tick) noexcept {
    bitmap[tick / 64U] &= ~(UINT64_C(1) << (tick % 64U));
}

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

// Dequeue the head node. Marks it DEAD. Clears bitmap bit if level empties.
// Returns pointer to the dequeued node (remains in arena, marked DEAD).
// Returns nullptr if level is empty.
inline order_node_t* queue_dequeue_head(price_level_t& level,
                                        arena_t&       arena,
                                        uint64_t*      bitmap,
                                        tick_t         tick) noexcept {
    if (level.head_idx == NULL_IDX) {
        return nullptr;
    }
    slot_idx_t    slot = level.head_idx;
    order_node_t& node = arena.nodes[slot];

    level.head_idx   = node.next_idx;
    if (level.head_idx == NULL_IDX) {
        level.tail_idx = NULL_IDX;
    }
    level.count    -= 1U;
    level.total_qty -= node.quantity;
    node.flags = static_cast<uint8_t>(node.flags | DEAD_FLAG);
    node.next_idx   = NULL_IDX;

    // Synchronous bitmap update (spec mandates synchronous-only updates)
    if (level.count == 0U) {
        bitmap_clear(bitmap, tick);
    }
    return &node;
}

// Partially fill the head node (does not dequeue). Updates level total_qty.
// Precondition: fill_qty < node.quantity (partial fill only).
inline void queue_partial_fill_head(price_level_t& level,
                                    arena_t&       arena,
                                    qty_t          fill_qty) noexcept {
    order_node_t& node  = arena.nodes[level.head_idx];
    node.quantity      -= fill_qty;
    level.total_qty    -= fill_qty;
}

// Physically evict a single dead head node from the queue.
// Precondition: level.head_idx != NULL_IDX
//               arena.nodes[level.head_idx].flags & DEAD_FLAG
// count/total_qty/bitmap are already correct (settled at cancel time).
// This is pure linked-list maintenance — no accounting update needed.
inline void queue_evict_dead_head(price_level_t& level,
                                  arena_t&       arena) noexcept {
    slot_idx_t    dead_slot = level.head_idx;
    order_node_t& dead      = arena.nodes[dead_slot];
    level.head_idx          = dead.next_idx;
    if (level.head_idx == NULL_IDX) {
        level.tail_idx = NULL_IDX;
    }
    dead.next_idx = NULL_IDX;
}

// Remove an arbitrary node from the FIFO by order_id.
// O(q) predecessor scan for mid-queue cancel (spec: book_cancel implementation note).
// Head-cancel special case is O(1).
// Returns true on success; false if the node is not found in this queue.
inline bool queue_remove(price_level_t& level,
                         arena_t&       arena,
                         uint64_t*      bitmap,
                         tick_t         tick,
                         order_id_t     order_id) noexcept {
    if (level.head_idx == NULL_IDX) {
        return false;
    }

    // O(1) head-cancel special case (spec: book_cancel special case)
    if (level.head_idx == order_id) {
        order_node_t& node = arena.nodes[order_id];
        level.head_idx     = node.next_idx;
        if (level.head_idx == NULL_IDX) {
            level.tail_idx = NULL_IDX;
        }
        level.count    -= 1U;
        level.total_qty -= node.quantity;
        node.flags = static_cast<uint8_t>(node.flags | DEAD_FLAG);
        node.next_idx   = NULL_IDX;
        if (level.count == 0U) {
            bitmap_clear(bitmap, tick);
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
            level.count    -= 1U;
            level.total_qty -= node.quantity;
            node.flags = static_cast<uint8_t>(node.flags | DEAD_FLAG);
            node.next_idx   = NULL_IDX;
            if (level.count == 0U) {
                bitmap_clear(bitmap, tick);
            }
            return true;
        }
        prev_slot = prev.next_idx;
    }
}

} // namespace internal
} // namespace es::book
