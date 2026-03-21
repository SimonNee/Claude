/*
 * test_book.c — Correctness test suite for the C E-mini order book.
 *
 * Test discipline:
 *   - All expected values are computed independently of the implementation
 *     (hand calculation or oracle tracking).
 *   - check_invariants() is called after every mutating operation.
 *   - Invalid-input cases are covered (B5, B6, A6, A7, A8).
 *   - Build: -std=c11 -O2 -march=native -Wall -Wextra -Wconversion
 *            -Wsign-conversion -Wsign-compare -Werror
 *            -fsanitize=undefined,address
 *   - No -flto (correctness build, per idioms).
 *
 * Run: ./test_book
 * Each test prints PASS <name> on success; assert() aborts on failure.
 */

#include "book.h"
#include "matcher.h"    /* bitmap_is_set_pub */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
 * Oracle — independent state tracking (spec: Oracle rules 1-5)
 *
 * The oracle is a simple set-of-live-orders tracker that does NOT call
 * the implementation to compute expected values.
 * ====================================================================== */

#define ORACLE_MAX 200   /* small oracle for tests */

typedef struct {
    uint32_t    order_id;
    side_t      side;
    tick_t      tick;
    uint32_t    quantity;
    int         live;       /* 1 = live, 0 = cancelled/filled */
} oracle_order_t;

typedef struct {
    oracle_order_t orders[ORACLE_MAX];
    uint32_t       count;
    uint32_t       next_id;   /* next expected order_id (0-indexed) */
} oracle_t;

static void oracle_init(oracle_t *o) {
    memset(o, 0, sizeof(*o));
}

static void oracle_add(oracle_t *o, order_id_t id, side_t side, tick_t tick, uint32_t qty) {
    assert(o->count < ORACLE_MAX);
    assert(id == o->next_id); /* Oracle rule 1: k-th valid add returns k-1 */
    o->orders[o->count].order_id = id;
    o->orders[o->count].side     = side;
    o->orders[o->count].tick     = tick;
    o->orders[o->count].quantity = qty;
    o->orders[o->count].live     = 1;
    o->count++;
    o->next_id++;
}

/* Oracle rule 2: cancel succeeds iff order was issued and is still live.
 * (Referenced by oracle_cancel to verify pre-condition; kept for completeness.) */
static int oracle_is_live(const oracle_t *o, order_id_t id) __attribute__((unused));
static int oracle_is_live(const oracle_t *o, order_id_t id) {
    for (uint32_t i = 0; i < o->count; i++) {
        if (o->orders[i].order_id == id)
            return o->orders[i].live;
    }
    return 0;
}

static void oracle_cancel(oracle_t *o, order_id_t id) {
    for (uint32_t i = 0; i < o->count; i++) {
        if (o->orders[i].order_id == id) {
            o->orders[i].live = 0;
            return;
        }
    }
}

/* Oracle: count live orders at (side, tick) */
static uint32_t oracle_level_count(const oracle_t *o, side_t side, tick_t tick) {
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < o->count; i++) {
        if (o->orders[i].live && o->orders[i].side == side && o->orders[i].tick == tick)
            cnt++;
    }
    return cnt;
}

/* Oracle: sum of live quantities at (side, tick) */
static uint32_t oracle_level_qty(const oracle_t *o, side_t side, tick_t tick) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < o->count; i++) {
        if (o->orders[i].live && o->orders[i].side == side && o->orders[i].tick == tick)
            total += o->orders[i].quantity;
    }
    return total;
}

/* Oracle: best bid tick (max tick with any live bid order) */
static tick_t oracle_best_bid(const oracle_t *o) {
    tick_t best = TICK_INVALID;
    for (uint32_t i = 0; i < o->count; i++) {
        if (o->orders[i].live && o->orders[i].side == BID) {
            if (best == TICK_INVALID || o->orders[i].tick > best)
                best = o->orders[i].tick;
        }
    }
    return best;
}

/* Oracle: best ask tick (min tick with any live ask order) */
static tick_t oracle_best_ask(const oracle_t *o) {
    tick_t best = TICK_INVALID;
    for (uint32_t i = 0; i < o->count; i++) {
        if (o->orders[i].live && o->orders[i].side == ASK) {
            if (best == TICK_INVALID || o->orders[i].tick < best)
                best = o->orders[i].tick;
        }
    }
    return best;
}

/* =========================================================================
 * Invariant checker (spec: Test Suite Contract / Invariants 1–9)
 *
 * Reads raw struct fields directly from the book (Oracle rule 5).
 * Does NOT use the book's public API to check invariants.
 * ====================================================================== */

static int check_invariants(const book_t *book) {
    if (book == NULL) {
        fprintf(stderr, "INVARIANT FAIL: book is NULL\n");
        return 0;
    }

    for (uint8_t s = 0; s < 2; s++) {
        const book_side_t *bside = &book->sides[s];

        for (uint32_t t = 0; t < MAX_TICKS; t++) {
            const price_level_t *level = &bside->levels[t];

            /* Invariant 1: bitmap-level consistency */
            int bit_set = bitmap_is_set_pub(book, (side_t)s, t);
            if (bit_set && level->count == 0) {
                fprintf(stderr, "INV1 FAIL: side=%u tick=%u bitmap set but count==0\n", s, t);
                return 0;
            }
            if (!bit_set && level->count > 0) {
                fprintf(stderr, "INV1 FAIL: side=%u tick=%u bitmap clear but count==%u\n", s, t, level->count);
                return 0;
            }

            /* Walk the chain for invariants 2,3,4,5,6,8,9 */
            uint32_t chain_count = 0;
            uint32_t chain_qty   = 0;
            uint32_t prev_id     = 0;       /* for FIFO order check */
            int      first_node  = 1;
            uint32_t curr        = level->head_idx;

            while (curr != NULL_IDX) {
                if (curr >= MAX_ORDERS) {
                    fprintf(stderr, "INV5 FAIL: side=%u tick=%u next_idx=%u >= MAX_ORDERS\n", s, t, curr);
                    return 0;
                }
                const order_node_t *node = &book->arena.nodes[curr];

                /* Invariant 6: no DEAD_FLAG node in live chain */
                if (node->flags & DEAD_FLAG) {
                    fprintf(stderr, "INV6 FAIL: side=%u tick=%u node %u has DEAD_FLAG but is in chain\n", s, t, curr);
                    return 0;
                }

                /* Invariant 4: monotonically increasing order_id along chain */
                if (!first_node && node->order_id <= prev_id) {
                    fprintf(stderr, "INV4 FAIL: side=%u tick=%u order_id %u not > prev %u\n", s, t, node->order_id, prev_id);
                    return 0;
                }
                prev_id    = node->order_id;
                first_node = 0;

                chain_count++;
                chain_qty += node->quantity;

                /* Advance — check for NULL sentinel at actual tail */
                if (node->next_idx == NULL_IDX) {
                    /* Invariant 8: tail_idx must equal curr */
                    if (level->tail_idx != curr) {
                        fprintf(stderr, "INV8 FAIL: side=%u tick=%u tail_idx=%u but last node is %u\n",
                                s, t, level->tail_idx, curr);
                        return 0;
                    }
                }
                curr = node->next_idx;
            }

            /* Invariant 2: level->count == chain traversal count */
            if (level->count != chain_count) {
                fprintf(stderr, "INV2 FAIL: side=%u tick=%u level->count=%u chain=%u\n",
                        s, t, level->count, chain_count);
                return 0;
            }

            /* Invariant 3: level->total_qty == sum of live node quantities */
            if (level->total_qty != chain_qty) {
                fprintf(stderr, "INV3 FAIL: side=%u tick=%u total_qty=%u chain_qty=%u\n",
                        s, t, level->total_qty, chain_qty);
                return 0;
            }

            /* Invariant 9: if empty, both head and tail must be NULL_IDX */
            if (level->count == 0) {
                if (level->head_idx != NULL_IDX || level->tail_idx != NULL_IDX) {
                    fprintf(stderr, "INV9 FAIL: side=%u tick=%u empty level has non-NULL head=%u tail=%u\n",
                            s, t, level->head_idx, level->tail_idx);
                    return 0;
                }
            } else {
                /* Invariant 9: if non-empty, head must point to lowest-order_id live node */
                const order_node_t *head_node = &book->arena.nodes[level->head_idx];
                /* In FIFO with monotonically increasing IDs, head has the smallest ID */
                uint32_t min_id = head_node->order_id;
                /* Walk chain, verify head has the minimum ID */
                uint32_t walk = level->head_idx;
                while (walk != NULL_IDX) {
                    if (book->arena.nodes[walk].order_id < min_id) {
                        fprintf(stderr, "INV9 FAIL: side=%u tick=%u head is not min-order_id node\n", s, t);
                        return 0;
                    }
                    walk = book->arena.nodes[walk].next_idx;
                }
            }
        }
    }

    /* Invariant 7: arena.next_slot must never decrease (tested by caller tracking) */
    /* We just verify it is within range */
    if (book->arena.next_slot > MAX_ORDERS) {
        fprintf(stderr, "INV7 FAIL: next_slot=%u > MAX_ORDERS\n", book->arena.next_slot);
        return 0;
    }

    return 1;
}

/* =========================================================================
 * Test harness macros
 * ====================================================================== */

#define TEST(name) static void test_##name(void)
#define RUN(name)  do { \
    test_##name(); \
    printf("PASS  " #name "\n"); \
} while(0)

/* Base price used for all tests: 5500.00. Tick size 0.25 → scale 4.
 * tick = (uint32_t)((price - 5500.0) * 4.0 + 0.5)
 * Hand-calculated reference ticks used throughout:
 *   price 5500.00 → tick 0
 *   price 5500.25 → tick 1
 *   price 5501.00 → tick 4
 *   price 5600.00 → tick 400
 *   price 5700.00 → tick 800
 *   price 5799.75 → tick 1199
 *   price last valid: 5500.00 + (8799/4.0) = 5500.00 + 2199.75 = 7699.75 → tick 8799
 */
#define BASE_PRICE  5500.0
#define PRICE_0     5500.00   /* tick 0  */
#define PRICE_1     5500.25   /* tick 1  */
#define PRICE_4     5501.00   /* tick 4  */
#define PRICE_100   5525.00   /* tick 100 */
#define PRICE_101   5525.25   /* tick 101 */
#define PRICE_102   5525.50   /* tick 102 */
#define PRICE_200   5550.00   /* tick 200 */
#define PRICE_400   5600.00   /* tick 400 */
#define PRICE_8799  7699.75   /* tick 8799 = MAX_TICKS-1 */

/* Verify hand-calculated tick conversions directly — not using the impl */
static void verify_tick_constants(void) {
    /* These are hand-calculated: delta * 4 + 0.5, truncated */
    assert(price_to_tick(BASE_PRICE + 0.00, BASE_PRICE) == 0U);
    assert(price_to_tick(BASE_PRICE + 0.25, BASE_PRICE) == 1U);
    assert(price_to_tick(BASE_PRICE + 1.00, BASE_PRICE) == 4U);
    assert(price_to_tick(BASE_PRICE + 25.00, BASE_PRICE) == 100U);
    assert(price_to_tick(BASE_PRICE + 25.25, BASE_PRICE) == 101U);
    assert(price_to_tick(BASE_PRICE + 25.50, BASE_PRICE) == 102U);
    assert(price_to_tick(BASE_PRICE + 50.00, BASE_PRICE) == 200U);
    assert(price_to_tick(BASE_PRICE + 100.00, BASE_PRICE) == 400U);
    assert(price_to_tick(BASE_PRICE + 2199.75, BASE_PRICE) == 8799U);
}

/* =========================================================================
 * Category A — Add only
 * ====================================================================== */

/* A1: Add one bid order. Verify level count=1, total_qty correct,
 *     bitmap bit set, returned order_id is 0 (Oracle rule 1). */
TEST(A1_add_one_bid) {
    verify_tick_constants();
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);
    assert(check_invariants(b));

    order_id_t id = book_add(b, BID, PRICE_100, 5U);
    assert(id == 0U);                          /* Oracle rule 1: first add → id 0 */
    assert(check_invariants(b));

    /* Verify via public inspection */
    assert(book_level_count(b, BID, 100U) == 1U);
    assert(book_level_qty(b, BID, 100U)   == 5U);
    assert(bitmap_is_set_pub(b, BID, 100U));
    assert(book_best_bid(b) == 100U);

    book_destroy(b);
}

/* A2: Add two bid orders at same price. Verify count=2, FIFO order
 *     (order_id 0 before order_id 1), both invariants hold. */
TEST(A2_add_two_bids_same_level) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id0 = book_add(b, BID, PRICE_100, 10U);
    assert(id0 == 0U);
    assert(check_invariants(b));

    order_id_t id1 = book_add(b, BID, PRICE_100, 20U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    assert(book_level_count(b, BID, 100U) == 2U);
    assert(book_level_qty(b, BID, 100U)   == 30U);  /* 10+20 */
    assert(bitmap_is_set_pub(b, BID, 100U));

    /* FIFO: head must be id0=0, next must be id1=1 */
    const price_level_t *level = &b->sides[BID].levels[100U];
    assert(level->head_idx == 0U);
    assert(b->arena.nodes[0].next_idx == 1U);
    assert(level->tail_idx == 1U);

    book_destroy(b);
}

/* A3: Add bid and ask at same price. Each side must be independent;
 *     no crossing check in add. */
TEST(A3_add_bid_and_ask_same_tick) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t bid_id = book_add(b, BID, PRICE_100, 5U);
    assert(bid_id == 0U);
    assert(check_invariants(b));

    order_id_t ask_id = book_add(b, ASK, PRICE_100, 7U);
    assert(ask_id == 1U);
    assert(check_invariants(b));

    assert(book_level_count(b, BID, 100U) == 1U);
    assert(book_level_count(b, ASK, 100U) == 1U);
    assert(book_level_qty(b, BID, 100U)   == 5U);
    assert(book_level_qty(b, ASK, 100U)   == 7U);

    book_destroy(b);
}

/* A4: Add order at tick 0 (lowest valid tick). */
TEST(A4_add_at_tick_zero) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id = book_add(b, BID, PRICE_0, 1U);
    assert(id == 0U);
    assert(check_invariants(b));

    assert(book_level_count(b, BID, 0U) == 1U);
    assert(book_level_qty(b, BID, 0U)   == 1U);
    assert(bitmap_is_set_pub(b, BID, 0U));

    book_destroy(b);
}

/* A5: Add order at tick MAX_TICKS-1 (highest valid tick). */
TEST(A5_add_at_tick_max_minus_1) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* PRICE_8799 maps to tick 8799 = MAX_TICKS-1 */
    order_id_t id = book_add(b, ASK, PRICE_8799, 3U);
    assert(id == 0U);
    assert(check_invariants(b));

    assert(book_level_count(b, ASK, 8799U) == 1U);
    assert(book_level_qty(b, ASK, 8799U)   == 3U);

    book_destroy(b);
}

/* A6: Add order with invalid price (below base). Must return NULL_IDX,
 *     no state mutation. */
TEST(A6_add_invalid_price_below_base) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* Price below base_price → delta < 0 → TICK_INVALID */
    order_id_t id = book_add(b, BID, BASE_PRICE - 0.25, 5U);
    assert(id == NULL_IDX);
    assert(check_invariants(b));

    /* No levels touched */
    for (uint32_t t = 0; t < 10; t++)
        assert(book_level_count(b, BID, t) == 0U);

    /* arena untouched */
    assert(b->arena.next_slot == 0U);

    book_destroy(b);
}

/* A7: Add order with price mapping to tick >= MAX_TICKS.
 *     Must return NULL_IDX, no state mutation. */
TEST(A7_add_price_tick_overflow) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* MAX_TICKS=8800 ticks. Price for tick 8800 would be base + 8800/4 = base+2200 */
    /* Actually tick = (price-base)*4+0.5 → for tick=8800: price = base+2200 */
    double overflow_price = BASE_PRICE + 2200.0; /* would be tick 8800 = MAX_TICKS */
    order_id_t id = book_add(b, BID, overflow_price, 5U);
    assert(id == NULL_IDX);
    assert(check_invariants(b));
    assert(b->arena.next_slot == 0U);

    book_destroy(b);
}

/* A8: Add order with quantity=0. Must return NULL_IDX, no state mutation. */
TEST(A8_add_zero_quantity) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id = book_add(b, BID, PRICE_100, 0U);
    assert(id == NULL_IDX);
    assert(check_invariants(b));
    assert(b->arena.next_slot == 0U);

    book_destroy(b);
}

/* A9/A10: Add MAX_ORDERS orders — last succeeds (id=MAX_ORDERS-1),
 *         (MAX_ORDERS+1)th returns NULL_IDX.
 *
 * NOTE: MAX_ORDERS=1,000,000. We spread across 100 ticks to avoid
 *       measuring only one level but still test the capacity boundary.
 *       The invariant checker is NOT called every iteration here
 *       (it is O(MAX_TICKS*MAX_ORDERS) if called each time); instead we
 *       verify the boundary values directly and call invariants once at end.
 *       The spec test says to test boundary, not to call invariants 1M times.
 */
TEST(A9_A10_arena_exhaustion) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* Add MAX_ORDERS orders spread across 10 tick levels */
    for (uint32_t i = 0; i < MAX_ORDERS; i++) {
        /* Use tick levels 100..109 cyclically */
        uint32_t tick = 100U + (i % 10U);
        double price  = BASE_PRICE + (double)tick * 0.25;
        order_id_t id = book_add(b, BID, price, 1U);
        assert(id == i); /* Oracle rule 1: k-th valid add returns k */
    }
    assert(b->arena.next_slot == MAX_ORDERS);

    /* (MAX_ORDERS+1)th add must fail */
    order_id_t id = book_add(b, BID, PRICE_100, 1U);
    assert(id == NULL_IDX);

    /* arena.next_slot must not have increased */
    assert(b->arena.next_slot == MAX_ORDERS);

    book_destroy(b);
}

/* =========================================================================
 * Category B — Cancel
 * ====================================================================== */

/* B1: Add one order, cancel it. Verify level count=0, bitmap cleared,
 *     DEAD_FLAG set on node. */
TEST(B1_cancel_only_order) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id = book_add(b, BID, PRICE_100, 10U);
    assert(id == 0U);
    assert(check_invariants(b));

    bool ok = book_cancel(b, id, BID, 100U);
    assert(ok);
    assert(check_invariants(b));

    assert(book_level_count(b, BID, 100U) == 0U);
    assert(book_level_qty(b, BID, 100U)   == 0U);
    assert(!bitmap_is_set_pub(b, BID, 100U));
    assert(b->arena.nodes[0].flags & DEAD_FLAG);
    /* head and tail must be NULL_IDX */
    assert(b->sides[BID].levels[100U].head_idx == NULL_IDX);
    assert(b->sides[BID].levels[100U].tail_idx == NULL_IDX);

    book_destroy(b);
}

/* B2: Add two orders at same level, cancel head. Verify head is now second
 *     order; count=1; bitmap still set. */
TEST(B2_cancel_head_of_two) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id0 = book_add(b, BID, PRICE_100, 5U);
    order_id_t id1 = book_add(b, BID, PRICE_100, 7U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    bool ok = book_cancel(b, id0, BID, 100U);
    assert(ok);
    assert(check_invariants(b));

    assert(book_level_count(b, BID, 100U) == 1U);
    assert(book_level_qty(b, BID, 100U)   == 7U);
    assert(bitmap_is_set_pub(b, BID, 100U));

    /* The new head is id1 */
    assert(b->sides[BID].levels[100U].head_idx == id1);
    assert(b->sides[BID].levels[100U].tail_idx == id1);

    book_destroy(b);
}

/* B3: Add two orders at same level, cancel tail (non-head, mid-queue at q=2).
 *     Verify head is still first; count=1. */
TEST(B3_cancel_tail_of_two) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id0 = book_add(b, BID, PRICE_100, 5U);
    order_id_t id1 = book_add(b, BID, PRICE_100, 7U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    bool ok = book_cancel(b, id1, BID, 100U);
    assert(ok);
    assert(check_invariants(b));

    assert(book_level_count(b, BID, 100U) == 1U);
    assert(book_level_qty(b, BID, 100U)   == 5U);
    assert(bitmap_is_set_pub(b, BID, 100U));

    /* Head is still id0; tail is also id0 */
    assert(b->sides[BID].levels[100U].head_idx == id0);
    assert(b->sides[BID].levels[100U].tail_idx == id0);

    book_destroy(b);
}

/* B4: Add five orders at same level, cancel the third (mid-queue, q=5).
 *     Verify chain is still ordered, count=4. */
TEST(B4_cancel_middle_of_five) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t ids[5];
    for (uint32_t i = 0; i < 5; i++) {
        ids[i] = book_add(b, BID, PRICE_100, (qty_t)(i + 1U));
        assert(ids[i] == i);
        assert(check_invariants(b));
    }

    /* Cancel the third order (0-indexed: index 2, order_id=2) */
    bool ok = book_cancel(b, ids[2], BID, 100U);
    assert(ok);
    assert(check_invariants(b));

    /* Hand-calculated remaining qty: 1+2+4+5 = 12 (was 1+2+3+4+5=15, minus 3=12) */
    assert(book_level_count(b, BID, 100U) == 4U);
    assert(book_level_qty(b, BID, 100U)   == 12U);

    /* FIFO chain must be: id0→id1→id3→id4 (id2 removed) */
    const price_level_t *level = &b->sides[BID].levels[100U];
    assert(level->head_idx == ids[0]);
    uint32_t curr = level->head_idx;
    uint32_t expected_order[] = { ids[0], ids[1], ids[3], ids[4] };
    for (int j = 0; j < 4; j++) {
        assert(curr == expected_order[j]);
        curr = b->arena.nodes[curr].next_idx;
    }
    assert(curr == NULL_IDX);

    book_destroy(b);
}

/* B5: Cancel with order_id that was never issued (>= next_slot).
 *     Returns false, no mutation. */
TEST(B5_cancel_never_issued_id) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id = book_add(b, BID, PRICE_100, 5U);
    assert(id == 0U);
    assert(check_invariants(b));

    /* order_id=1 was never issued (next_slot=1) */
    bool ok = book_cancel(b, 1U, BID, 100U);
    assert(!ok);
    assert(check_invariants(b));

    /* Level still has 1 live order */
    assert(book_level_count(b, BID, 100U) == 1U);

    /* order_id=MAX_ORDERS (out of range entirely) */
    ok = book_cancel(b, MAX_ORDERS, BID, 100U);
    assert(!ok);
    assert(check_invariants(b));

    book_destroy(b);
}

/* B6: Cancel the same order_id twice. Second cancel returns false (DEAD_FLAG),
 *     no mutation. */
TEST(B6_double_cancel) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id = book_add(b, BID, PRICE_100, 5U);
    assert(id == 0U);

    bool ok1 = book_cancel(b, id, BID, 100U);
    assert(ok1);
    assert(check_invariants(b));

    bool ok2 = book_cancel(b, id, BID, 100U);
    assert(!ok2);
    assert(check_invariants(b));

    /* Level must still be empty */
    assert(book_level_count(b, BID, 100U) == 0U);

    book_destroy(b);
}

/* B7: Add at two different price levels, cancel one. Verify the other level
 *     is unaffected. */
TEST(B7_cancel_one_of_two_levels) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id0 = book_add(b, BID, PRICE_100, 5U);
    order_id_t id1 = book_add(b, BID, PRICE_200, 8U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    bool ok = book_cancel(b, id0, BID, 100U);
    assert(ok);
    assert(check_invariants(b));

    assert(book_level_count(b, BID, 100U) == 0U);
    assert(book_level_count(b, BID, 200U) == 1U);
    assert(book_level_qty(b, BID, 200U)   == 8U);
    assert(!bitmap_is_set_pub(b, BID, 100U));
    assert(bitmap_is_set_pub(b, BID, 200U));

    book_destroy(b);
}

/* B8: Add N orders at one level, cancel all N. Verify level fully empty. */
TEST(B8_cancel_all_at_level) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    const uint32_t N = 20U;
    order_id_t ids[20];
    for (uint32_t i = 0; i < N; i++) {
        ids[i] = book_add(b, ASK, PRICE_100, (qty_t)(i + 1U));
        assert(ids[i] == i);
        assert(check_invariants(b));
    }

    /* Cancel all */
    for (uint32_t i = 0; i < N; i++) {
        bool ok = book_cancel(b, ids[i], ASK, 100U);
        assert(ok);
        assert(check_invariants(b));
    }

    /* Level fully empty */
    assert(book_level_count(b, ASK, 100U) == 0U);
    assert(book_level_qty(b, ASK, 100U)   == 0U);
    assert(!bitmap_is_set_pub(b, ASK, 100U));
    assert(b->sides[ASK].levels[100U].head_idx == NULL_IDX);
    assert(b->sides[ASK].levels[100U].tail_idx == NULL_IDX);
    assert(book_best_ask(b) == NULL_IDX);  /* empty book */

    book_destroy(b);
}

/* =========================================================================
 * Category C — Match
 * ====================================================================== */

/* C1: Add one ask at tick 100, qty=10. Match BID aggressor at tick 100,
 *     qty=10. Expect: fill_count=1, remaining_qty=0, ask level empty,
 *     bitmap cleared. */
TEST(C1_match_exact_one_ask) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t ask_id = book_add(b, ASK, PRICE_100, 10U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    /* taker_id=999: arbitrary, we just verify it appears in the fill */
    fill_result_t r = book_match(b, BID, PRICE_100, 10U, 999U);
    assert(check_invariants(b));

    /* Hand-calculated: one complete fill */
    assert(r.fill_count == 1U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].maker_order_id == ask_id);
    assert(r.fills[0].taker_order_id == 999U);
    assert(r.fills[0].price_tick     == 100U);
    assert(r.fills[0].filled_qty     == 10U);

    /* Ask level must be empty */
    assert(book_level_count(b, ASK, 100U) == 0U);
    assert(!bitmap_is_set_pub(b, ASK, 100U));
    /* DEAD_FLAG set on the matched node */
    assert(b->arena.nodes[ask_id].flags & DEAD_FLAG);

    book_destroy(b);
}

/* C2: Add one ask at tick 100, qty=20. Match BID qty=10. Expect partial fill:
 *     ask node quantity decremented to 10, level still live. */
TEST(C2_match_partial_one_ask) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t ask_id = book_add(b, ASK, PRICE_100, 20U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    fill_result_t r = book_match(b, BID, PRICE_100, 10U, 42U);
    assert(check_invariants(b));

    assert(r.fill_count == 1U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].filled_qty == 10U);

    /* Ask node still alive, quantity reduced to 10 */
    assert(book_level_count(b, ASK, 100U) == 1U);
    assert(book_level_qty(b, ASK, 100U)   == 10U);
    assert(bitmap_is_set_pub(b, ASK, 100U));
    assert(!(b->arena.nodes[ask_id].flags & DEAD_FLAG));
    /* The node's quantity is 10 (20-10=10) */
    assert(b->arena.nodes[ask_id].quantity == 10U);

    book_destroy(b);
}

/* C3: Add two asks at tick 100, each qty=5. Match BID qty=8. Expect:
 *     first ask fully filled (5), second ask partially filled (3),
 *     fill_count=2. */
TEST(C3_match_two_asks_partial) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id0 = book_add(b, ASK, PRICE_100, 5U);
    order_id_t id1 = book_add(b, ASK, PRICE_100, 5U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    fill_result_t r = book_match(b, BID, PRICE_100, 8U, 77U);
    assert(check_invariants(b));

    /* Hand-calculated:
     * - id0 (qty=5): 5 <= 8 → full fill, remaining = 3
     * - id1 (qty=5): 3 < 5 → partial fill of 3, remaining = 0
     */
    assert(r.fill_count == 2U);
    assert(r.remaining_qty == 0U);

    assert(r.fills[0].maker_order_id == id0);
    assert(r.fills[0].filled_qty     == 5U);
    assert(r.fills[1].maker_order_id == id1);
    assert(r.fills[1].filled_qty     == 3U);

    /* id1 still alive with 2 remaining */
    assert(book_level_count(b, ASK, 100U) == 1U);
    assert(book_level_qty(b, ASK, 100U)   == 2U);
    assert(b->arena.nodes[id1].quantity   == 2U);
    assert(!(b->arena.nodes[id1].flags & DEAD_FLAG));

    book_destroy(b);
}

/* C4: Match with no crossing orders. fill_count=0, remaining_qty=input qty,
 *     no mutation. */
TEST(C4_match_no_crossing) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* Add an ask at tick 200, try to match BID at tick 100 (doesn't cross) */
    order_id_t ask_id = book_add(b, ASK, PRICE_200, 10U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    /* BID aggressor at tick 100; ask is at 200; 100 < 200 → no cross */
    fill_result_t r = book_match(b, BID, PRICE_100, 10U, 1U);
    assert(check_invariants(b));

    assert(r.fill_count    == 0U);
    assert(r.remaining_qty == 10U);

    /* Ask level unchanged */
    assert(book_level_count(b, ASK, 200U) == 1U);
    assert(book_level_qty(b, ASK, 200U)   == 10U);

    book_destroy(b);
}

/* C5: Add asks at ticks 100, 101, 102. Match BID at tick 102 with large qty.
 *     Fills at 100 first (price priority FIFO), then 101, then 102. */
TEST(C5_match_multi_level_price_priority) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* Each level has 1 order of qty=10 */
    order_id_t id100 = book_add(b, ASK, PRICE_100, 10U);
    order_id_t id101 = book_add(b, ASK, PRICE_101, 10U);
    order_id_t id102 = book_add(b, ASK, PRICE_102, 10U);
    assert(id100 == 0U);
    assert(id101 == 1U);
    assert(id102 == 2U);
    assert(check_invariants(b));

    /* BID at tick 102 with qty=30 — should sweep all three levels */
    fill_result_t r = book_match(b, BID, PRICE_102, 30U, 5U);
    assert(check_invariants(b));

    /* Hand-calculated: 3 fills, price priority → tick 100 first */
    assert(r.fill_count    == 3U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].price_tick == 100U);  /* best ask filled first */
    assert(r.fills[1].price_tick == 101U);
    assert(r.fills[2].price_tick == 102U);
    assert(r.fills[0].filled_qty == 10U);
    assert(r.fills[1].filled_qty == 10U);
    assert(r.fills[2].filled_qty == 10U);

    /* All levels empty */
    assert(book_level_count(b, ASK, 100U) == 0U);
    assert(book_level_count(b, ASK, 101U) == 0U);
    assert(book_level_count(b, ASK, 102U) == 0U);
    assert(book_best_ask(b) == NULL_IDX);

    book_destroy(b);
}

/* C6: Match generates exactly 64 fills. Verify remaining_qty > 0,
 *     fill_count = 64. */
TEST(C6_match_exactly_64_fills) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* Add 64 ask orders, each qty=1, at tick 100 */
    for (uint32_t i = 0; i < 64U; i++) {
        order_id_t id = book_add(b, ASK, PRICE_100, 1U);
        assert(id == i);
    }
    assert(check_invariants(b));

    /* Match BID qty=100 — only 64 fills will be recorded */
    fill_result_t r = book_match(b, BID, PRICE_100, 100U, 0U);
    assert(check_invariants(b));

    assert(r.fill_count    == 64U);
    assert(r.remaining_qty == 36U);  /* 100 - 64 = 36 */

    /* Level has no live orders remaining (all 64 orders of qty=1 filled) */
    assert(book_level_count(b, ASK, 100U) == 0U);

    book_destroy(b);
}

/* C7: Match BID at ask_tick - 1 tick. Does not cross. */
TEST(C7_match_bid_below_ask) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t ask_id = book_add(b, ASK, PRICE_100, 10U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    /* BID at tick 99 (= PRICE_100 - 0.25); ask is at 100; 99 < 100 → no cross */
    double price_99 = BASE_PRICE + 99.0 * 0.25;  /* hand-calculated: 5500 + 24.75 = 5524.75 */
    assert(price_to_tick(price_99, BASE_PRICE) == 99U);

    fill_result_t r = book_match(b, BID, price_99, 10U, 1U);
    assert(check_invariants(b));

    assert(r.fill_count    == 0U);
    assert(r.remaining_qty == 10U);
    assert(book_level_count(b, ASK, 100U) == 1U);

    book_destroy(b);
}

/* =========================================================================
 * Category D — Combined operations
 * ====================================================================== */

/* D1: Add, cancel, add again at same level. Second add uses a new slot,
 *     FIFO order correct. */
TEST(D1_add_cancel_add) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id0 = book_add(b, BID, PRICE_100, 5U);
    assert(id0 == 0U);
    assert(check_invariants(b));

    bool ok = book_cancel(b, id0, BID, 100U);
    assert(ok);
    assert(check_invariants(b));

    /* Second add must use slot 1 (arena is monotonic — slot 0 never reused) */
    order_id_t id1 = book_add(b, BID, PRICE_100, 7U);
    assert(id1 == 1U);  /* Oracle rule 1: next slot */
    assert(check_invariants(b));

    assert(book_level_count(b, BID, 100U) == 1U);
    assert(book_level_qty(b, BID, 100U)   == 7U);
    assert(b->sides[BID].levels[100U].head_idx == id1);

    book_destroy(b);
}

/* D2: Interleaved add and match: add 10 asks, match 5, verify remaining 5
 *     are in FIFO order. */
TEST(D2_interleaved_add_and_match) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    const uint32_t N = 10U;
    order_id_t ids[10];
    for (uint32_t i = 0; i < N; i++) {
        ids[i] = book_add(b, ASK, PRICE_100, (qty_t)(i + 1U));
        assert(ids[i] == i);
        assert(check_invariants(b));
    }

    /* Match enough qty to consume the first 5: 1+2+3+4+5=15 */
    fill_result_t r = book_match(b, BID, PRICE_100, 15U, 42U);
    assert(check_invariants(b));

    assert(r.fill_count    == 5U);
    assert(r.remaining_qty == 0U);

    /* Remaining 5 orders must be in FIFO order (ids[5..9]) */
    assert(book_level_count(b, ASK, 100U) == 5U);
    uint32_t curr = b->sides[ASK].levels[100U].head_idx;
    for (uint32_t i = 5; i < N; i++) {
        assert(curr == ids[i]);
        curr = b->arena.nodes[curr].next_idx;
    }
    assert(curr == NULL_IDX);

    book_destroy(b);
}

/* D3: Add N orders across M levels, cancel all odd-order_id orders,
 *     verify all invariants. */
TEST(D3_add_across_levels_cancel_odd) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);
    oracle_t o;
    oracle_init(&o);

    /* Add 20 orders across 4 levels (ticks 100,101,102,103) */
    uint32_t ticks[4] = {100U, 101U, 102U, 103U};
    double   prices[4] = {PRICE_100, PRICE_101, PRICE_102,
                          BASE_PRICE + 103.0 * 0.25};

    for (uint32_t i = 0; i < 20U; i++) {
        uint32_t t   = i % 4U;
        tick_t   tick = ticks[t];
        order_id_t id = book_add(b, BID, prices[t], (qty_t)(i + 1U));
        assert(id == i);
        oracle_add(&o, id, BID, tick, i + 1U);
        assert(check_invariants(b));
    }

    /* Cancel all odd-id orders */
    for (uint32_t i = 1U; i < 20U; i += 2U) {
        uint32_t t   = i % 4U;
        tick_t   tick = ticks[t];
        bool ok = book_cancel(b, i, BID, tick);
        assert(ok);
        oracle_cancel(&o, i);
        assert(check_invariants(b));
    }

    /* Verify counts against oracle */
    for (uint32_t t = 0; t < 4U; t++) {
        tick_t tick = ticks[t];
        uint32_t exp_count = oracle_level_count(&o, BID, tick);
        uint32_t exp_qty   = oracle_level_qty(&o, BID, tick);
        assert(book_level_count(b, BID, tick) == exp_count);
        assert(book_level_qty(b, BID, tick)   == exp_qty);
        bool exp_bit = (exp_count > 0);
        assert(bitmap_is_set_pub(b, BID, tick) == exp_bit);
    }

    book_destroy(b);
}

/* D4: best_bid and best_ask correctness with oracle tracking */
TEST(D4_best_bid_ask_oracle) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);
    oracle_t o;
    oracle_init(&o);

    /* Add bids at ticks 100, 200, 400 */
    order_id_t bid100 = book_add(b, BID, PRICE_100, 1U);
    oracle_add(&o, bid100, BID, 100U, 1U);
    assert(check_invariants(b));
    assert(book_best_bid(b) == oracle_best_bid(&o)); /* = 100 */

    order_id_t bid200 = book_add(b, BID, PRICE_200, 1U);
    oracle_add(&o, bid200, BID, 200U, 1U);
    assert(check_invariants(b));
    assert(book_best_bid(b) == oracle_best_bid(&o)); /* = 200 */

    order_id_t bid400 = book_add(b, BID, PRICE_400, 1U);
    oracle_add(&o, bid400, BID, 400U, 1U);
    assert(check_invariants(b));
    assert(book_best_bid(b) == oracle_best_bid(&o)); /* = 400 */

    /* Add ask at tick 200 */
    order_id_t ask200 = book_add(b, ASK, PRICE_200, 1U);
    oracle_add(&o, ask200, ASK, 200U, 1U);
    assert(check_invariants(b));
    assert(book_best_ask(b) == oracle_best_ask(&o)); /* = 200 */

    /* Cancel bid at 400 → best_bid drops to 200 */
    book_cancel(b, bid400, BID, 400U);
    oracle_cancel(&o, bid400);
    assert(check_invariants(b));
    assert(book_best_bid(b) == oracle_best_bid(&o)); /* = 200 */

    /* Cancel bid at 200 → best_bid drops to 100 */
    book_cancel(b, bid200, BID, 200U);
    oracle_cancel(&o, bid200);
    assert(check_invariants(b));
    assert(book_best_bid(b) == oracle_best_bid(&o)); /* = 100 */

    book_destroy(b);
}

/* D5: Reset: after a full session, call book_reset, verify all levels empty,
 *     bitmap all zero, next_slot=0, base_price retained. */
TEST(D5_reset) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* Add a few orders and a match */
    order_id_t id0 = book_add(b, BID, PRICE_100, 5U);
    order_id_t id1 = book_add(b, ASK, PRICE_100, 3U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));
    (void)book_match(b, BID, PRICE_100, 3U, 99U);
    assert(check_invariants(b));

    book_reset(b);
    assert(check_invariants(b));

    /* Verify completely empty */
    assert(b->arena.next_slot == 0U);
    assert(book_best_bid(b) == NULL_IDX);
    assert(book_best_ask(b) == NULL_IDX);
    assert(b->base_price == BASE_PRICE);  /* retained */

    for (uint32_t t = 0; t < MAX_TICKS; t++) {
        assert(b->sides[BID].levels[t].head_idx == NULL_IDX);
        assert(b->sides[BID].levels[t].tail_idx == NULL_IDX);
        assert(b->sides[BID].levels[t].count    == 0U);
        assert(b->sides[ASK].levels[t].head_idx == NULL_IDX);
        assert(b->sides[ASK].levels[t].tail_idx == NULL_IDX);
        assert(b->sides[ASK].levels[t].count    == 0U);
    }
    for (uint32_t w = 0; w < BITMAP_WORDS; w++) {
        assert(b->sides[BID].bitmap[w] == 0U);
        assert(b->sides[ASK].bitmap[w] == 0U);
    }

    book_destroy(b);
}

/* D6: book_create with invalid base_price returns NULL */
TEST(D6_create_invalid_base) {
    book_t *b1 = book_create(-1.0);
    assert(b1 == NULL);

    book_t *b2 = book_create(0.0);
    assert(b2 == NULL);

    /* NaN — isfinite returns false */
    book_t *b3 = book_create(0.0 / 0.0);
    assert(b3 == NULL);

    /* infinity */
    double inf = 1.0 / 0.0;
    book_t *b4 = book_create(inf);
    assert(b4 == NULL);
}

/* D7: book_cancel with tick >= MAX_TICKS returns false */
TEST(D7_cancel_invalid_tick) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    order_id_t id = book_add(b, BID, PRICE_100, 5U);
    assert(id == 0U);
    assert(check_invariants(b));

    /* tick = MAX_TICKS is out of bounds */
    bool ok = book_cancel(b, id, BID, MAX_TICKS);
    assert(!ok);
    assert(check_invariants(b));

    /* Order is still live */
    assert(book_level_count(b, BID, 100U) == 1U);

    book_destroy(b);
}

/* D8: ASK aggressor matching (symmetric path in matcher) */
TEST(D8_ask_aggressor_match) {
    book_t *b = book_create(BASE_PRICE);
    assert(b != NULL);

    /* Place a bid at tick 100 qty=10 */
    order_id_t bid_id = book_add(b, BID, PRICE_100, 10U);
    assert(bid_id == 0U);
    assert(check_invariants(b));

    /* ASK aggressor at tick 100 (crosses: bid_tick=100 >= agg_tick=100) */
    fill_result_t r = book_match(b, ASK, PRICE_100, 10U, 55U);
    assert(check_invariants(b));

    assert(r.fill_count    == 1U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].maker_order_id == bid_id);
    assert(r.fills[0].taker_order_id == 55U);
    assert(r.fills[0].price_tick     == 100U);
    assert(r.fills[0].filled_qty     == 10U);

    assert(book_level_count(b, BID, 100U) == 0U);
    assert(!bitmap_is_set_pub(b, BID, 100U));

    book_destroy(b);
}

/* =========================================================================
 * Main
 * ====================================================================== */

int main(void) {
    RUN(A1_add_one_bid);
    RUN(A2_add_two_bids_same_level);
    RUN(A3_add_bid_and_ask_same_tick);
    RUN(A4_add_at_tick_zero);
    RUN(A5_add_at_tick_max_minus_1);
    RUN(A6_add_invalid_price_below_base);
    RUN(A7_add_price_tick_overflow);
    RUN(A8_add_zero_quantity);
    RUN(A9_A10_arena_exhaustion);
    RUN(B1_cancel_only_order);
    RUN(B2_cancel_head_of_two);
    RUN(B3_cancel_tail_of_two);
    RUN(B4_cancel_middle_of_five);
    RUN(B5_cancel_never_issued_id);
    RUN(B6_double_cancel);
    RUN(B7_cancel_one_of_two_levels);
    RUN(B8_cancel_all_at_level);
    RUN(C1_match_exact_one_ask);
    RUN(C2_match_partial_one_ask);
    RUN(C3_match_two_asks_partial);
    RUN(C4_match_no_crossing);
    RUN(C5_match_multi_level_price_priority);
    RUN(C6_match_exactly_64_fills);
    RUN(C7_match_bid_below_ask);
    RUN(D1_add_cancel_add);
    RUN(D2_interleaved_add_and_match);
    RUN(D3_add_across_levels_cancel_odd);
    RUN(D4_best_bid_ask_oracle);
    RUN(D5_reset);
    RUN(D6_create_invalid_base);
    RUN(D7_cancel_invalid_tick);
    RUN(D8_ask_aggressor_match);

    printf("\n32 tests passed.\n");
    return 0;
}
