/*
 * book.c — E-mini S&P 500 limit order book implementation.
 *
 * Module decomposition (all internal — book.c is the assembly point):
 *   Module 1: Arena   — monotonic allocation, no free list
 *   Module 2: Queue   — per-level doubly-linked FIFO (intrusive, index-based)
 *   Module 3: Bitmap  — uint64_t word array; TZCNT/LZCNT for best-level scan
 *   Module 5: Book    — public API, routing to the above modules
 *
 * Module 4 (Matcher) is in matcher.c / matcher.h.
 *
 * No-cast rule: the only float-to-integer cast is in price_to_tick() in
 * book.h. This file contains one (uint8_t) cast for DEAD_FLAG assignment
 * — a structural necessity under -Wconversion when assigning the result of
 * uint8_t | uint32_t back to uint8_t. This is not a type model error.
 *
 * Order node slot index: since order_id == slot index by construction,
 * and order_id is no longer stored in the node (TRIZ Trimming), callers
 * recover the slot as (arena->next_slot - 1U) immediately after arena_alloc.
 */

#include "book.h"
#include "bitmap.h"
#include "matcher.h"

#include <stdlib.h>   /* malloc, free */
#include <string.h>   /* memset */

/* =========================================================================
 * Module 1: Arena
 * ====================================================================== */

/*
 * arena_alloc — claim the next slot and initialise the node.
 *
 * Returns a pointer to the freshly initialised node, or NULL if the arena
 * is exhausted (next_slot >= MAX_ORDERS).
 *
 * The slot index equals (arena->next_slot - 1U) after this call returns.
 * order_id is no longer stored in the node — the caller recovers the slot
 * from arena->next_slot - 1U immediately after a successful alloc.
 *
 * prev_idx is initialised to NULL_IDX (node is initially isolated).
 * queue_enqueue will set the correct prev_idx before linking.
 */
static inline order_node_t *arena_alloc(arena_t *arena, qty_t quantity)
{
    if (arena->next_slot >= MAX_ORDERS)
        return NULL;

    uint32_t slot        = arena->next_slot;
    arena->next_slot     = slot + 1U;

    order_node_t *node   = &arena->nodes[slot];
    node->prev_idx  = NULL_IDX;
    node->quantity  = quantity;
    node->next_idx  = NULL_IDX;
    node->flags     = 0U;
    node->_pad[0]   = 0U;
    node->_pad[1]   = 0U;
    node->_pad[2]   = 0U;

    return node;
}

/*
 * arena_get — direct dereference by order_id (== slot index).
 * Precondition: order_id < arena->next_slot (verified by caller).
 */
static inline order_node_t *arena_get(arena_t *arena, order_id_t order_id)
{
    return &arena->nodes[order_id];
}

/* =========================================================================
 * Module 2: Level Queue (intrusive doubly-linked FIFO, index-based)
 *
 * The arena node array is passed explicitly so that queue operations can
 * walk and modify prev_idx/next_idx fields without holding a global pointer.
 *
 * Doubly-linked invariants maintained by queue_enqueue and queue_splice_out:
 *   - head node has prev_idx == NULL_IDX
 *   - tail node has next_idx == NULL_IDX
 *   - for any non-head node n: nodes[nodes[n].prev_idx].next_idx == n
 *   - for any non-tail node n: nodes[nodes[n].next_idx].prev_idx == n
 * ====================================================================== */

/*
 * queue_is_empty — true when no live orders exist at this level.
 */
static inline bool queue_is_empty(const price_level_t *level)
{
    return level->head_idx == NULL_IDX;
}

/*
 * queue_enqueue — append a node (identified by slot index) to the FIFO tail.
 *
 * Sets prev_idx on the new node to the old tail (or NULL_IDX if level was
 * empty). Updates the old tail's next_idx to point to the new node.
 * If the level was previously empty, sets the bitmap bit synchronously.
 *
 * Caller: book_add. Preconditions (all guaranteed by book_add):
 *   - nodes[slot].next_idx == NULL_IDX  (set by arena_alloc)
 *   - slot < MAX_ORDERS
 *   - tick < MAX_TICKS
 */
static inline void queue_enqueue(price_level_t *level, uint64_t *bitmap,
                                 tick_t tick, order_node_t *nodes,
                                 uint32_t slot, qty_t quantity)
{
    bool was_empty = queue_is_empty(level);

    /* Set prev_idx before updating tail_idx: old tail is still in tail_idx */
    nodes[slot].prev_idx = was_empty ? NULL_IDX : level->tail_idx;

    if (was_empty) {
        /* First order at this level */
        level->head_idx = slot;
    } else {
        /* Chain new node onto the current tail */
        nodes[level->tail_idx].next_idx = slot;
    }
    level->tail_idx   = slot;
    level->count      = level->count + 1U;
    level->total_qty  = level->total_qty + quantity;

    /* Synchronous bitmap update */
    if (was_empty) {
        bitmap[tick >> 6U] |= (UINT64_C(1) << (tick & 63U));
    }
}

/*
 * queue_splice_out — O(1) removal of an arbitrary node from the doubly-linked FIFO.
 *
 * Uses prev_idx/next_idx to splice the node out without scanning.
 * Sets DEAD_FLAG on the removed node. Updates count, total_qty, and bitmap.
 * Updates head_idx and/or tail_idx if the node was the head or tail.
 * Zeroes prev_idx and next_idx on the removed node (node is now isolated).
 *
 * Preconditions (guaranteed by book_cancel before calling):
 *   - order_id < arena->next_slot
 *   - the node does NOT have DEAD_FLAG set
 */
static inline void queue_splice_out(price_level_t *level, uint64_t *bitmap,
                                    tick_t tick, order_node_t *nodes,
                                    order_id_t order_id)
{
    order_node_t *node = &nodes[order_id];
    uint32_t prev      = node->prev_idx;
    uint32_t next      = node->next_idx;

    /* Splice: update predecessor's next */
    if (prev != NULL_IDX) {
        nodes[prev].next_idx = next;
    } else {
        /* Node was the head */
        level->head_idx = next;
    }

    /* Splice: update successor's prev */
    if (next != NULL_IDX) {
        nodes[next].prev_idx = prev;
    } else {
        /* Node was the tail */
        level->tail_idx = prev;
    }

    level->count     = level->count - 1U;
    level->total_qty = level->total_qty - node->quantity;
    node->flags      = (uint8_t)(node->flags | DEAD_FLAG);
    node->next_idx   = NULL_IDX;
    node->prev_idx   = NULL_IDX;

    if (level->count == 0U) {
        bitmap[tick >> 6U] &= ~(UINT64_C(1) << (tick & 63U));
    }
}

/* =========================================================================
 * Module 3: Bitmap
 *
 * bitmap_best_ask and bitmap_best_bid are now static inline in bitmap.h,
 * included above. Both book.c and matcher.c include bitmap.h, giving the
 * compiler full visibility for inlining in both TUs.
 *
 * bitmap_is_set_pub is not on the hot path (test harness only) and retains
 * external linkage here.
 * ====================================================================== */

/*
 * bitmap_is_set_pub — query whether tick has any live orders.
 *
 * Used by the test harness for invariant checking (not on the hot path).
 */
bool bitmap_is_set_pub(const book_t *book, side_t side, tick_t tick)
{
    if (side != BID && side != ASK)
        return false;
    if (tick >= MAX_TICKS)
        return false;
    uint64_t word = book->sides[side].bitmap[tick >> 6U];
    return (word & (UINT64_C(1) << (tick & 63U))) != 0U;
}

/* =========================================================================
 * Module 5: Book — public API
 * ====================================================================== */

book_t *book_create(double base_price)
{
    if (!isfinite(base_price) || base_price <= 0.0)
        return NULL;

    book_t *book = (book_t *)malloc(sizeof(book_t));
    if (book == NULL)
        return NULL;

    /* Zero the entire structure first (sets count, total_qty, bitmap to 0).
     * Then walk each level to set head_idx and tail_idx to NULL_IDX,
     * since 0 is a valid slot index and must not be used as a sentinel. */
    memset(book, 0, sizeof(book_t));

    for (uint32_t s = 0U; s < 2U; ++s) {
        book_side_t *bside = &book->sides[s];
        for (uint32_t t = 0U; t < MAX_TICKS; ++t) {
            bside->levels[t].head_idx = NULL_IDX;
            bside->levels[t].tail_idx = NULL_IDX;
        }
    }

    book->arena.next_slot = 0U;
    book->base_price      = base_price;

    return book;
}

void book_destroy(book_t *book)
{
    free(book); /* free(NULL) is defined as a no-op in C11 §7.22.3.3 */
}

order_id_t book_add(book_t *book, side_t side, double price, qty_t quantity)
{
    if (book == NULL)
        return NULL_IDX;
    if (side != BID && side != ASK)
        return NULL_IDX;
    if (quantity == 0U)
        return NULL_IDX;

    tick_t tick = price_to_tick(price, book->base_price);
    if (tick == TICK_INVALID)
        return NULL_IDX;

    order_node_t *node = arena_alloc(&book->arena, quantity);
    if (node == NULL)
        return NULL_IDX;

    /* Slot index: arena_alloc incremented next_slot, so allocated slot is one below */
    uint32_t slot = book->arena.next_slot - 1U;

    book_side_t   *bside = &book->sides[side];
    price_level_t *level = &bside->levels[tick];

    /*
     * queue_enqueue handles:
     *   - setting prev_idx on the new node (old tail or NULL_IDX)
     *   - linking old tail's next_idx to slot (when level is non-empty)
     *   - setting head_idx when the level was empty
     *   - updating tail_idx, count, total_qty
     *   - setting the bitmap bit if the level just became live
     */
    queue_enqueue(level, bside->bitmap, tick, book->arena.nodes, slot, quantity);

    return slot;
}

order_id_t book_add_tick(book_t *book, side_t side, tick_t tick, qty_t quantity)
{
    if (book == NULL)
        return NULL_IDX;
    if (side != BID && side != ASK)
        return NULL_IDX;
    if (quantity == 0U)
        return NULL_IDX;
    if (tick >= MAX_TICKS)
        return NULL_IDX;

    order_node_t *node = arena_alloc(&book->arena, quantity);
    if (node == NULL)
        return NULL_IDX;

    /* Slot index: arena_alloc incremented next_slot, so allocated slot is one below */
    uint32_t slot = book->arena.next_slot - 1U;

    book_side_t   *bside = &book->sides[side];
    price_level_t *level = &bside->levels[tick];

    queue_enqueue(level, bside->bitmap, tick, book->arena.nodes, slot, quantity);

    return slot;
}

fill_result_t book_match_tick(book_t *book, side_t aggressor_side,
                              tick_t aggressor_tick, qty_t quantity,
                              order_id_t taker_id)
{
    fill_result_t result;
    result.fill_count    = 0U;
    result.remaining_qty = quantity;

    if (book == NULL || quantity == 0U)
        return result;
    if (aggressor_side != BID && aggressor_side != ASK)
        return result;
    if (aggressor_tick >= MAX_TICKS)
        return result;

    matcher_execute(book, aggressor_side, aggressor_tick, quantity, taker_id,
                    &result);
    return result;
}

bool book_cancel(book_t *book, order_id_t order_id, side_t side, tick_t tick)
{
    if (book == NULL)
        return false;
    if (side != BID && side != ASK)
        return false;
    if (tick >= MAX_TICKS)
        return false;
    if (order_id >= MAX_ORDERS || order_id >= book->arena.next_slot)
        return false;

    order_node_t *node = arena_get(&book->arena, order_id);

    /* Order already dead — double-cancel guard */
    if (node->flags & DEAD_FLAG)
        return false;

    book_side_t   *bside = &book->sides[side];
    price_level_t *level = &bside->levels[tick];

    queue_splice_out(level, bside->bitmap, tick, book->arena.nodes, order_id);
    return true;
}

fill_result_t book_match(book_t *book, side_t aggressor_side, double price,
                         qty_t quantity, order_id_t taker_id)
{
    fill_result_t result;
    result.fill_count    = 0U;
    result.remaining_qty = quantity;

    if (book == NULL || quantity == 0U)
        return result;
    if (aggressor_side != BID && aggressor_side != ASK)
        return result;

    tick_t aggressor_tick = price_to_tick(price, book->base_price);
    if (aggressor_tick == TICK_INVALID)
        return result;

    matcher_execute(book, aggressor_side, aggressor_tick, quantity, taker_id,
                    &result);
    return result;
}

tick_t book_best_bid(const book_t *book)
{
    if (book == NULL)
        return TICK_INVALID;
    return bitmap_best_bid(book->sides[BID].bitmap);
}

tick_t book_best_ask(const book_t *book)
{
    if (book == NULL)
        return TICK_INVALID;
    return bitmap_best_ask(book->sides[ASK].bitmap);
}

uint32_t book_level_count(const book_t *book, side_t side, tick_t tick)
{
    if (book == NULL || (side != BID && side != ASK) || tick >= MAX_TICKS)
        return 0U;
    return book->sides[side].levels[tick].count;
}

qty_t book_level_qty(const book_t *book, side_t side, tick_t tick)
{
    if (book == NULL || (side != BID && side != ASK) || tick >= MAX_TICKS)
        return 0U;
    return book->sides[side].levels[tick].total_qty;
}

void book_reset(book_t *book)
{
    if (book == NULL)
        return;

    double saved = book->base_price;

    memset(book, 0, sizeof(book_t));

    /* Restore NULL_IDX sentinels for head_idx and tail_idx */
    for (uint32_t s = 0U; s < 2U; ++s) {
        book_side_t *bside = &book->sides[s];
        for (uint32_t t = 0U; t < MAX_TICKS; ++t) {
            bside->levels[t].head_idx = NULL_IDX;
            bside->levels[t].tail_idx = NULL_IDX;
        }
    }

    book->base_price = saved;
}
