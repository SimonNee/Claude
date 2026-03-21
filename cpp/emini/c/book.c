/*
 * book.c — E-mini S&P 500 limit order book implementation.
 *
 * Module decomposition (all internal — book.c is the assembly point):
 *   Module 1: Arena   — monotonic allocation, no free list
 *   Module 2: Queue   — per-level singly-linked FIFO (intrusive, index-based)
 *   Module 3: Bitmap  — uint64_t word array; TZCNT/LZCNT for best-level scan
 *   Module 5: Book    — public API, routing to the above modules
 *
 * Module 4 (Matcher) is in matcher.c / matcher.h.
 *
 * No-cast rule: the only float-to-integer cast is in price_to_tick() in
 * book.h. This file contains two (uint8_t) casts for DEAD_FLAG assignment
 * — a structural necessity under -Wconversion when assigning the result of
 * uint8_t | uint32_t back to uint8_t. These are not type model errors.
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
 * The node's order_id is set to the slot index. Invariant:
 *   arena.nodes[i].order_id == i   for every allocated node.
 */
static inline order_node_t *arena_alloc(arena_t *arena, qty_t quantity)
{
    if (arena->next_slot >= MAX_ORDERS)
        return NULL;

    uint32_t slot        = arena->next_slot;
    arena->next_slot     = slot + 1U;

    order_node_t *node   = &arena->nodes[slot];
    node->order_id  = slot;
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
 * Module 2: Level Queue (intrusive singly-linked FIFO, index-based)
 *
 * The arena node array is passed explicitly so that queue operations can
 * walk and modify next_idx fields without holding a global pointer.
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
 * The node's next_idx must already be NULL_IDX (set by arena_alloc).
 * If the level was previously empty, sets the bitmap bit synchronously.
 *
 * Caller: book_add. Preconditions (all guaranteed by book_add):
 *   - nodes[slot].next_idx == NULL_IDX
 *   - slot < MAX_ORDERS
 *   - tick < MAX_TICKS
 */
static inline void queue_enqueue(price_level_t *level, uint64_t *bitmap,
                                 tick_t tick, order_node_t *nodes,
                                 uint32_t slot, qty_t quantity)
{
    bool was_empty = queue_is_empty(level);

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
 * queue_remove — remove an arbitrary node from the FIFO by order_id.
 *
 * Performs a predecessor scan from head (O(q)). Head removal is O(1).
 * Sets DEAD_FLAG on the removed node. Decrements count and total_qty.
 * If the level empties, clears the bitmap bit synchronously.
 *
 * Returns true if the node was found and removed.
 * Returns false if order_id is not found in this queue.
 */
static bool queue_remove(price_level_t *level, uint64_t *bitmap,
                         tick_t tick, order_node_t *nodes,
                         order_id_t order_id)
{
    if (queue_is_empty(level))
        return false;

    /* Fast path: head cancel O(1) */
    if (level->head_idx == order_id) {
        order_node_t *node  = &nodes[order_id];
        level->head_idx     = node->next_idx;
        if (level->head_idx == NULL_IDX)
            level->tail_idx = NULL_IDX;

        level->count        = level->count - 1U;
        level->total_qty    = level->total_qty - node->quantity;
        node->flags         = (uint8_t)(node->flags | DEAD_FLAG);
        node->next_idx      = NULL_IDX;

        if (level->head_idx == NULL_IDX) {
            bitmap[tick >> 6U] &= ~(UINT64_C(1) << (tick & 63U));
        }
        return true;
    }

    /* Mid-queue scan: walk from head, find predecessor of order_id */
    uint32_t prev = level->head_idx;
    uint32_t curr = nodes[prev].next_idx;

    while (curr != NULL_IDX) {
        if (curr == order_id) {
            order_node_t *node      = &nodes[curr];
            nodes[prev].next_idx    = node->next_idx;

            /* Update tail if we removed the tail node */
            if (curr == level->tail_idx) {
                level->tail_idx = prev;
            }

            level->count        = level->count - 1U;
            level->total_qty    = level->total_qty - node->quantity;
            node->flags         = (uint8_t)(node->flags | DEAD_FLAG);
            node->next_idx      = NULL_IDX;

            /* Level cannot become empty from a mid-queue remove
             * (there is at least the head node remaining), so no
             * bitmap update is needed here. The level is empty only
             * when count reaches 0, which happens only via head removal
             * once prev == head_idx and next is NULL_IDX. Confirmed:
             * mid-queue means prev >= head, curr is not head, so at
             * minimum head is still live. No bitmap clear needed. */
            return true;
        }
        prev = curr;
        curr = nodes[curr].next_idx;
    }

    return false; /* order_id not found in this queue */
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

    uint32_t slot = node->order_id;

    book_side_t   *bside = &book->sides[side];
    price_level_t *level = &bside->levels[tick];

    /*
     * queue_enqueue handles:
     *   - linking node->next_idx into the current tail's next field
     *     (when level is non-empty)
     *   - setting head_idx when the level was empty
     *   - updating tail_idx, count, total_qty
     *   - setting the bitmap bit if the level just became live
     *
     * The arena pointer is passed so queue_enqueue can write
     * nodes[tail_idx].next_idx = slot when the level is non-empty.
     */
    queue_enqueue(level, bside->bitmap, tick, book->arena.nodes, slot, quantity);

    return slot;
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

    /* Order already dead — no double-cancel */
    if (node->flags & DEAD_FLAG)
        return false;

    /* Corruption guard: slot self-reference must hold */
    if (node->order_id != order_id)
        return false;

    book_side_t   *bside = &book->sides[side];
    price_level_t *level = &bside->levels[tick];

    return queue_remove(level, bside->bitmap, tick,
                        book->arena.nodes, order_id);
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
