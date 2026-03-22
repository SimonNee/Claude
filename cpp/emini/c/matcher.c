/*
 * matcher.c — Module 4: Cross-side matching algorithm.
 *
 * The matcher is the only code with simultaneous read/write access to
 * both sides of the book. All other modules operate on a single side.
 *
 * Matching logic:
 *   BID aggressor → iterates the ASK side from best_ask upward,
 *                   filling while best_ask_tick <= aggressor_tick.
 *   ASK aggressor → iterates the BID side from best_bid downward,
 *                   filling while best_bid_tick >= aggressor_tick.
 *
 * Per resting order at each level (FIFO discipline):
 *   - If maker quantity <= remaining: full fill; dequeue head; mark DEAD.
 *   - If maker quantity >  remaining: partial fill; decrement maker qty;
 *                                     node stays at head (not dequeued).
 *
 * Matching stops when:
 *   - remaining_qty == 0, or
 *   - no more crossing price levels exist, or
 *   - fill_count reaches MAX_FILLS (64).
 *
 * One (uint8_t) cast for DEAD_FLAG assignment — a structural necessity
 * under -Wconversion when assigning uint8_t | uint32_t back to uint8_t.
 * All other arithmetic is uint32_t. All types are explicit.
 */

#include "matcher.h"
#include "bitmap.h"

/* -------------------------------------------------------------------------
 * Internal helpers — static to this translation unit.
 * ---------------------------------------------------------------------- */

/*
 * next_best_ask — find the lowest active tick on the ask side.
 *
 * Thin wrapper so the matcher loop reads cleanly.
 */
static inline tick_t next_best_ask(const book_t *book)
{
    return bitmap_best_ask(book->sides[ASK].bitmap);
}

/*
 * next_best_bid — find the highest active tick on the bid side.
 */
static inline tick_t next_best_bid(const book_t *book)
{
    return bitmap_best_bid(book->sides[BID].bitmap);
}

/*
 * level_dequeue_head_fill — consume one full fill from the head of a level.
 *
 * Removes the head node from the FIFO, marks it DEAD_FLAG, and returns
 * its slot index. Decrements level count and total_qty by fill_qty.
 * Clears the bitmap bit if the level becomes empty.
 *
 * Preconditions (guaranteed by the matcher loop):
 *   - level->head_idx != NULL_IDX
 *   - fill_qty == nodes[head].quantity (full fill)
 */
static inline uint32_t level_dequeue_head_full(price_level_t *level,
                                               uint64_t *bitmap,
                                               tick_t tick,
                                               order_node_t *nodes,
                                               qty_t fill_qty)
{
    uint32_t head      = level->head_idx;
    order_node_t *node = &nodes[head];

    level->head_idx    = node->next_idx;
    if (level->head_idx == NULL_IDX)
        level->tail_idx = NULL_IDX;

    level->count       = level->count - 1U;
    level->total_qty   = level->total_qty - fill_qty;

    node->flags        = (uint8_t)(node->flags | DEAD_FLAG);
    node->next_idx     = NULL_IDX;

    /* Synchronous bitmap clear if the level emptied */
    if (level->head_idx == NULL_IDX) {
        bitmap[tick >> 6U] &= ~(UINT64_C(1) << (tick & 63U));
    }

    return head;
}

/*
 * level_partial_fill_head — apply a partial fill to the head node.
 *
 * The head node remains in the queue. Its quantity is decremented.
 * Level total_qty is decremented. Count is unchanged. Bitmap unchanged.
 *
 * Preconditions:
 *   - level->head_idx != NULL_IDX
 *   - fill_qty < nodes[head].quantity
 */
static inline void level_partial_fill_head(price_level_t *level,
                                           order_node_t *nodes,
                                           qty_t fill_qty)
{
    order_node_t *node  = &nodes[level->head_idx];
    node->quantity      = node->quantity - fill_qty;
    level->total_qty    = level->total_qty - fill_qty;
    /* count and head_idx are unchanged; the node stays at the head */
}

/* -------------------------------------------------------------------------
 * matcher_execute — the matching loop.
 * ---------------------------------------------------------------------- */

void matcher_execute(book_t *book, side_t aggressor_side,
                     tick_t aggressor_tick, qty_t quantity,
                     order_id_t taker_id, fill_result_t *out)
{
    out->fill_count    = 0U;
    out->remaining_qty = quantity;

    /*
     * maker_side is the opposite of the aggressor.
     * BID aggressor → maker is ASK (side index 1).
     * ASK aggressor → maker is BID (side index 0).
     *
     * Avoid the ternary operator: its result type is the common promoted
     * type of both branches (int for uint8_t operands), which would
     * narrow on assignment to side_t (uint8_t) and fire -Wconversion.
     * Use if/else with direct assignment to side_t to stay clean.
     */
    side_t maker_side;
    if (aggressor_side == BID) {
        maker_side = ASK;
    } else {
        maker_side = BID;
    }

    book_side_t   *mside = &book->sides[maker_side];
    order_node_t  *nodes = book->arena.nodes;

    qty_t remaining = quantity;

    while (remaining > 0U && out->fill_count < MAX_FILLS) {
        /* Find the best price on the maker side */
        tick_t best_tick;
        if (aggressor_side == BID) {
            best_tick = next_best_ask(book);
            /* BID aggressor fills ASK at best_ask_tick <= aggressor_tick */
            if (best_tick == TICK_INVALID || best_tick > aggressor_tick)
                break;
        } else {
            best_tick = next_best_bid(book);
            /* ASK aggressor fills BID at best_bid_tick >= aggressor_tick */
            if (best_tick == TICK_INVALID || best_tick < aggressor_tick)
                break;
        }

        price_level_t *level = &mside->levels[best_tick];

        /*
         * Consume orders from the head of this level in FIFO order.
         * Each head order produces exactly one fill record, whether it is
         * a full or partial fill.
         */
        while (remaining > 0U &&
               out->fill_count < MAX_FILLS &&
               level->head_idx != NULL_IDX) {

            /* Skip dead heads left by O(1) lazy cancels.
             * count/total_qty/bitmap are already correct from cancel time;
             * only the physical linked-list pointers need surgery here. */
            while (level->head_idx != NULL_IDX &&
                   (nodes[level->head_idx].flags & DEAD_FLAG)) {
                uint32_t dead        = level->head_idx;
                level->head_idx      = nodes[dead].next_idx;
                nodes[dead].next_idx = NULL_IDX;
                if (level->head_idx == NULL_IDX)
                    level->tail_idx = NULL_IDX;
            }
            /* If all remaining nodes at this level were dead, move to
             * the next bitmap tick. bitmap is already clear (cleared at
             * cancel time when count hit 0), so the outer bitmap scan
             * will not visit this level again. */
            if (level->head_idx == NULL_IDX)
                break;

            order_node_t *maker = &nodes[level->head_idx];
            qty_t maker_qty     = maker->quantity;
            uint32_t maker_id   = maker->order_id;

            qty_t fill_qty;
            if (maker_qty <= remaining) {
                /* Full fill of the maker order */
                fill_qty  = maker_qty;
                remaining = remaining - fill_qty;
                level_dequeue_head_full(level, mside->bitmap, best_tick,
                                        nodes, fill_qty);
            } else {
                /* Partial fill: maker survives with reduced quantity */
                fill_qty  = remaining;
                remaining = 0U;
                level_partial_fill_head(level, nodes, fill_qty);
            }

            /* Record the fill directly into the caller's buffer */
            fill_t *f         = &out->fills[out->fill_count];
            f->maker_order_id = maker_id;
            f->taker_order_id = taker_id;
            f->price_tick     = best_tick;
            f->filled_qty     = fill_qty;
            out->fill_count   = out->fill_count + 1U;
        }

        /*
         * If the level is now empty the bitmap bit was cleared synchronously
         * inside level_dequeue_head_full. The next iteration's bitmap scan
         * (next_best_ask / next_best_bid) will skip to the next active level.
         */
    }

    out->remaining_qty = remaining;
}
