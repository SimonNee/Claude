/*
 * book.h — E-mini S&P 500 limit order book, C implementation.
 *
 * Public API for the order book. All internal state is encapsulated behind
 * book_t*. Callers hold order_id_t values only — no interior pointers are
 * ever exposed.
 *
 * One sanctioned float-to-integer cast exists in price_to_tick(). No other
 * cast or promotion appears inside any hot-path function.
 *
 * Build: -std=c11 -O2 -march=native -Wall -Wextra -Wconversion
 *        -Wsign-conversion -Wsign-compare -Werror
 */

#ifndef BOOK_H
#define BOOK_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>       /* isfinite */

/* -------------------------------------------------------------------------
 * Compile-time constants
 * ---------------------------------------------------------------------- */

#define MAX_TICKS    8800U          /* full ±20% CME hard limit, bidirectional */
#define BITMAP_WORDS 138U           /* ceil(8800 / 64) */
#define MAX_ORDERS   1000000U       /* session upper bound */
#define NULL_IDX     0xFFFFFFFFU    /* sentinel: end-of-list, empty level, error */
#define DEAD_FLAG    0x01U          /* order_node_t.flags: cancelled or fully filled */
#define TICK_INVALID 0xFFFFFFFFU    /* returned by price_to_tick on validation failure */
#define MAX_FILLS    64U            /* maximum fill records per book_match call */

/* -------------------------------------------------------------------------
 * Type aliases
 * ---------------------------------------------------------------------- */

typedef uint32_t tick_t;
typedef uint32_t qty_t;
typedef uint32_t order_id_t;
typedef uint32_t slot_idx_t;

/*
 * side_t: BID = 0, ASK = 1.
 * Used as a direct array index into book_t.sides[2].
 * Only two valid values; anything else is rejected at the API boundary.
 */
typedef uint8_t side_t;
#define BID ((side_t)0U)
#define ASK ((side_t)1U)

/* -------------------------------------------------------------------------
 * Struct definitions
 * ---------------------------------------------------------------------- */

/*
 * order_node_t — one entry in the arena.
 *
 * Exactly 16 bytes. AoS layout. order_id == slot index by construction.
 * flags bit 0 == DEAD_FLAG when the order is cancelled or fully filled.
 * _pad[3] is explicit padding to reach 16 bytes; must be zero on alloc.
 */
typedef struct {
    uint32_t order_id;   /* offset  0 — equals slot index by construction */
    uint32_t quantity;   /* offset  4 — remaining quantity */
    uint32_t next_idx;   /* offset  8 — next in FIFO; NULL_IDX if tail */
    uint8_t  flags;      /* offset 12 — DEAD_FLAG when dead */
    uint8_t  _pad[3];    /* offset 13 — explicit pad to 16 bytes */
} order_node_t;

/*
 * price_level_t — the FIFO queue at a single tick on one side.
 *
 * Exactly 16 bytes. Empty invariant: head_idx == NULL_IDX,
 * tail_idx == NULL_IDX, count == 0, total_qty == 0.
 */
typedef struct {
    uint32_t head_idx;   /* offset  0 — head of FIFO; NULL_IDX if empty */
    uint32_t tail_idx;   /* offset  4 — tail of FIFO; NULL_IDX if empty */
    uint32_t count;      /* offset  8 — live order count at this level */
    uint32_t total_qty;  /* offset 12 — aggregate quantity at this level */
} price_level_t;

/*
 * book_side_t — one complete side (bid or ask).
 *
 * levels[] before bitmap[] so the larger, more frequently accessed
 * structure occupies the lower address range.
 */
typedef struct {
    price_level_t levels[MAX_TICKS];    /* 8800 × 16 = 140,800 bytes */
    uint64_t      bitmap[BITMAP_WORDS]; /*  138 ×  8 =   1,104 bytes */
} book_side_t;

/*
 * arena_t — session-wide, monotonic-allocation order node pool.
 *
 * No free list. alloc = next_slot++. Cancel = DEAD_FLAG write only.
 * order_id == slot_index by construction.
 */
typedef struct {
    order_node_t nodes[MAX_ORDERS]; /* 1,000,000 × 16 = 16,000,000 bytes */
    uint32_t     next_slot;         /* allocation high-water mark */
} arena_t;

/*
 * book_t — the complete order book.
 *
 * Always heap-allocated via book_create(); never placed on the stack.
 * Total size ~15.5 MB.
 */
typedef struct {
    book_side_t sides[2];   /* sides[BID] and sides[ASK] */
    arena_t     arena;
    double      base_price; /* reference price for tick conversion */
} book_t;

/*
 * fill_t — one matched trade record.
 */
typedef struct {
    order_id_t maker_order_id;  /* offset  0 — resting order that was matched */
    order_id_t taker_order_id;  /* offset  4 — aggressive order that caused the match */
    tick_t     price_tick;      /* offset  8 — price tick at which fill occurred */
    qty_t      filled_qty;      /* offset 12 — quantity exchanged */
} fill_t;

/*
 * fill_result_t — result of one book_match call.
 *
 * fill_count is capped at MAX_FILLS (64). If the incoming order would
 * generate more than 64 fills, matching stops at 64 and remaining_qty
 * reflects the unmatched portion. The caller must decide whether to
 * resubmit or discard the remainder.
 */
typedef struct {
    fill_t   fills[MAX_FILLS]; /* valid entries: fills[0 .. fill_count-1] */
    uint32_t fill_count;       /* number of valid fill records */
    qty_t    remaining_qty;    /* quantity not matched; 0 if fully filled */
} fill_result_t;

/* -------------------------------------------------------------------------
 * Compile-time layout verification
 * ---------------------------------------------------------------------- */

_Static_assert(sizeof(order_node_t)  == 16,       "order_node_t layout changed");
_Static_assert(sizeof(price_level_t) == 16,       "price_level_t layout changed");
_Static_assert(sizeof(book_side_t)   == 141904U,  "book_side_t layout changed");
_Static_assert(sizeof(arena_t)       == 16000004U, "arena_t layout changed");
_Static_assert(sizeof(fill_t)        == 16,       "fill_t layout changed");

/* -------------------------------------------------------------------------
 * Price conversion — the one sanctioned cast in the entire system
 * ---------------------------------------------------------------------- */

/*
 * price_to_tick — convert an external double price to an integer tick index.
 *
 * The cast (uint32_t)(…) is the only float-to-integer cast anywhere in the
 * implementation. It is isolated here so that -Wconversion violations
 * anywhere else in the source are unambiguously type model errors.
 *
 * Returns TICK_INVALID if any precondition fails.
 */
static inline tick_t price_to_tick(double price, double base_price)
{
    if (!isfinite(price) || !isfinite(base_price))
        return TICK_INVALID;
    double delta = price - base_price;
    if (delta < 0.0)
        return TICK_INVALID;
    double raw = delta * 4.0 + 0.5;
    if (raw >= (double)MAX_TICKS)
        return TICK_INVALID;
    /* Sanctioned cast: validated double in [0, MAX_TICKS) → uint32_t */
    return (tick_t)raw;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * book_create — allocate and initialise a book on the heap.
 *
 * Precondition: isfinite(base_price) && base_price > 0.0
 * Returns NULL if heap allocation fails. Caller must check.
 */
book_t *book_create(double base_price);

/*
 * book_destroy — release the heap memory for a book.
 *
 * No-op if book == NULL (matches free(NULL) semantics).
 */
void book_destroy(book_t *book);

/*
 * book_add — place a resting order in the book.
 *
 * Does NOT check for crossing prices. Call book_match first for
 * aggressive orders.
 *
 * Returns the order_id_t for the newly placed order.
 * Returns NULL_IDX on any error (invalid price, zero qty, invalid side,
 * arena exhausted). Caller must check.
 */
order_id_t book_add(book_t *book, side_t side, double price, qty_t quantity);

/*
 * book_cancel — remove a resting order from the book.
 *
 * Caller must supply side and tick — the same values known at add time.
 * This avoids any secondary lookup: the cancel path is exactly one arena
 * node dereference plus FIFO manipulation.
 *
 * For a singly-linked queue, mid-queue cancel is O(q) — a scan from head
 * to find the predecessor. Head cancel is O(1).
 *
 * Returns true if the order was live and has been cancelled.
 * Returns false if the order was already dead or the parameters are invalid.
 */
bool book_cancel(book_t *book, order_id_t order_id, side_t side, tick_t tick);

/*
 * book_match — attempt to match an incoming aggressive order.
 *
 * aggressor_side is the side of the incoming order.
 *   BID aggressor → matches against the ASK side (best ask upward).
 *   ASK aggressor → matches against the BID side (best bid downward).
 *
 * price and quantity describe the aggressor. taker_id is recorded in
 * fill records; it is not verified inside the book.
 *
 * Matching stops when remaining_qty == 0, no crossing price remains,
 * or fill_count reaches MAX_FILLS (64).
 *
 * Returns fill_result_t by value. On error: fill_count = 0,
 * remaining_qty = quantity, no state mutation.
 */
fill_result_t book_match(book_t *book, side_t aggressor_side, double price,
                         qty_t quantity, order_id_t taker_id);

/*
 * book_add_tick — place a resting order using a pre-validated integer tick.
 *
 * Identical to book_add but bypasses price_to_tick(). Intended for callers
 * that already hold a validated tick_t (e.g. benchmarks replaying CSV data).
 * The public book_add signature is unchanged.
 *
 * Preconditions: tick < MAX_TICKS, quantity > 0, side == BID || side == ASK.
 * Returns NULL_IDX on any error (invalid arguments, arena exhausted).
 */
order_id_t book_add_tick(book_t *book, side_t side, tick_t tick, qty_t quantity);

/*
 * book_match_tick — attempt to match an aggressive order using a pre-validated
 * integer tick. Identical to book_match but bypasses price_to_tick().
 *
 * Intended for callers that already hold a validated tick_t. The public
 * book_match signature is unchanged.
 *
 * Preconditions: aggressor_tick < MAX_TICKS, quantity > 0,
 *                aggressor_side == BID || aggressor_side == ASK.
 */
fill_result_t book_match_tick(book_t *book, side_t aggressor_side,
                              tick_t aggressor_tick, qty_t quantity,
                              order_id_t taker_id);

/*
 * book_best_bid — tick index of the highest active bid level.
 * Returns NULL_IDX if the bid side is empty.
 */
tick_t book_best_bid(const book_t *book);

/*
 * book_best_ask — tick index of the lowest active ask level.
 * Returns NULL_IDX if the ask side is empty.
 */
tick_t book_best_ask(const book_t *book);

/*
 * book_level_count — live order count at a given tick on a given side.
 * Returns 0 on invalid side or tick >= MAX_TICKS.
 */
uint32_t book_level_count(const book_t *book, side_t side, tick_t tick);

/*
 * book_level_qty — aggregate quantity at a given tick on a given side.
 * Returns 0 on invalid side or tick >= MAX_TICKS.
 */
qty_t book_level_qty(const book_t *book, side_t side, tick_t tick);

/*
 * book_reset — clear all book state. base_price is retained.
 *
 * Session boundary operation only; not on the hot path.
 * Uses memset for the bulk clear.
 */
void book_reset(book_t *book);

#endif /* BOOK_H */
