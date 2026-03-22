/* test_book.cpp — Correctness test suite for the C++ E-mini order book.
 *
 * Test discipline:
 *   - First test prints sizeof(Book::Impl) (spec requirement).
 *   - All expected values are computed independently of the implementation.
 *   - check_invariants() is called after every mutating operation.
 *   - Invalid-input cases covered: zero qty, below-base price, out-of-range tick,
 *     double cancel, never-issued cancel.
 *   - Build: -std=c++17 -O2 -march=native -Wall -Wextra -Wconversion
 *            -Wsign-conversion -Werror -fno-exceptions
 *            -fsanitize=undefined,address  (no -flto for correctness)
 *
 * Run: ./test_book_cpp
 */

#include "book.hpp"
#include "internal.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>

using namespace es::book;
using namespace es::book::internal;

// ---------------------------------------------------------------------------
// Oracle — independent state tracking (spec: Oracle rules 1-5)
// Does NOT call the implementation to derive expected values.
// ---------------------------------------------------------------------------

struct OracleOrder {
    order_id_t order_id;
    side_t     side;
    tick_t     tick;
    qty_t      quantity;
    bool       live;
};

struct Oracle {
    static constexpr uint32_t MAX_OO = 300U;
    OracleOrder orders[MAX_OO];
    uint32_t    count    = 0U;
    uint32_t    next_id  = 0U;    /* next expected order_id */

    void init() {
        count   = 0U;
        next_id = 0U;
    }

    void add(order_id_t id, side_t side, tick_t tick, qty_t qty) {
        assert(count < MAX_OO);
        assert(id == next_id);   /* Oracle rule 1 */
        orders[count] = { id, side, tick, qty, true };
        count++;
        next_id++;
    }

    bool is_live(order_id_t id) const {
        for (uint32_t i = 0; i < count; ++i)
            if (orders[i].order_id == id)
                return orders[i].live;
        return false;
    }

    void cancel(order_id_t id) {
        for (uint32_t i = 0; i < count; ++i)
            if (orders[i].order_id == id) { orders[i].live = false; return; }
    }

    uint32_t level_count(side_t side, tick_t tick) const {
        uint32_t n = 0;
        for (uint32_t i = 0; i < count; ++i)
            if (orders[i].live && orders[i].side == side && orders[i].tick == tick)
                ++n;
        return n;
    }

    qty_t level_qty(side_t side, tick_t tick) const {
        qty_t total = 0;
        for (uint32_t i = 0; i < count; ++i)
            if (orders[i].live && orders[i].side == side && orders[i].tick == tick)
                total += orders[i].quantity;
        return total;
    }

    tick_t best_bid() const {
        int best = -1;
        for (uint32_t i = 0; i < count; ++i)
            if (orders[i].live && orders[i].side == side_t::BID)
                if (best < 0 || static_cast<int>(orders[i].tick) > best)
                    best = static_cast<int>(orders[i].tick);
        return (best >= 0) ? static_cast<tick_t>(best) : NULL_IDX;
    }

    tick_t best_ask() const {
        int best = -1;
        for (uint32_t i = 0; i < count; ++i)
            if (orders[i].live && orders[i].side == side_t::ASK)
                if (best < 0 || static_cast<int>(orders[i].tick) < best)
                    best = static_cast<int>(orders[i].tick);
        return (best >= 0) ? static_cast<tick_t>(best) : NULL_IDX;
    }
};

// ---------------------------------------------------------------------------
// Invariant checker — reads raw struct fields, NOT the public API.
// Verifies spec invariants 1-9 for both sides, all ticks.
// ---------------------------------------------------------------------------

static bool check_invariants(const Book& book) {
    const Book::Impl& impl = book.impl();

    for (uint8_t s = 0; s < 2; ++s) {
        const book_side_t& bside = impl.sides[s];
        side_t sd = static_cast<side_t>(s);

        for (uint32_t t = 0; t < MAX_TICKS; ++t) {
            const price_level_t& level = bside.levels[t];

            // Invariant 1: bitmap-level consistency
            bool bit = bitmap_is_set(bside.bitmap, t);
            if (bit && level.count == 0U) {
                std::fprintf(stderr, "INV1 FAIL: side=%u tick=%u bit set but count=0\n", s, t);
                return false;
            }
            if (!bit && level.count > 0U) {
                std::fprintf(stderr, "INV1 FAIL: side=%u tick=%u bit clear but count=%u\n", s, t, level.count);
                return false;
            }

            // Walk chain for invariants 2,3,4,5,6,8,9
            uint32_t chain_count = 0U;
            qty_t    chain_qty   = 0U;
            int      first       = 1;
            uint32_t prev_id     = 0U;
            uint32_t curr        = level.head_idx;

            while (curr != NULL_IDX) {
                if (curr >= MAX_ORDERS) {
                    std::fprintf(stderr, "INV5 FAIL: side=%u tick=%u invalid idx %u\n", s, t, curr);
                    return false;
                }
                const order_node_t& node = impl.arena.nodes[curr];

                // Invariant 6: no DEAD_FLAG in live chain
                if (node.flags & DEAD_FLAG) {
                    std::fprintf(stderr, "INV6 FAIL: side=%u tick=%u node %u is DEAD in chain\n", s, t, curr);
                    return false;
                }

                // Invariant 4: monotonically increasing slot index (FIFO order).
                // Slots are allocated monotonically; enqueue always appends at tail,
                // so slot indices must be strictly ascending along the chain.
                // Uses curr (slot index) directly — order_id field no longer exists;
                // the slot index IS the order_id by construction (TRIZ Trimming).
                if (!first && curr <= prev_id) {
                    std::fprintf(stderr, "INV4 FAIL: side=%u tick=%u slot %u not > prev %u\n", s, t, curr, prev_id);
                    return false;
                }
                prev_id = curr;
                first   = 0;

                // Doubly-linked invariant DL1: head node must have no predecessor
                if (curr == level.head_idx && node.prev_idx != NULL_IDX) {
                    std::fprintf(stderr, "INV_DL1 FAIL: side=%u tick=%u head %u has prev=%u\n",
                                 s, t, curr, node.prev_idx);
                    return false;
                }

                // Doubly-linked invariant DL2: next.prev must point back to curr
                if (node.next_idx != NULL_IDX &&
                    impl.arena.nodes[node.next_idx].prev_idx != curr) {
                    std::fprintf(stderr, "INV_DL2 FAIL: side=%u tick=%u node %u: next=%u next.prev=%u\n",
                                 s, t, curr, node.next_idx, impl.arena.nodes[node.next_idx].prev_idx);
                    return false;
                }

                ++chain_count;
                chain_qty += node.quantity;

                // Invariant 8: tail_idx must equal last node
                if (node.next_idx == NULL_IDX) {
                    if (level.tail_idx != curr) {
                        std::fprintf(stderr, "INV8 FAIL: side=%u tick=%u tail=%u but last=%u\n", s, t, level.tail_idx, curr);
                        return false;
                    }
                }
                curr = node.next_idx;
            }

            // Invariant 2
            if (level.count != chain_count) {
                std::fprintf(stderr, "INV2 FAIL: side=%u tick=%u count=%u chain=%u\n", s, t, level.count, chain_count);
                return false;
            }

            // Invariant 3
            if (level.total_qty != chain_qty) {
                std::fprintf(stderr, "INV3 FAIL: side=%u tick=%u total_qty=%u chain=%u\n", s, t, level.total_qty, chain_qty);
                return false;
            }

            // Invariant 9: empty level → both head and tail NULL_IDX
            if (level.count == 0U) {
                if (level.head_idx != NULL_IDX || level.tail_idx != NULL_IDX) {
                    std::fprintf(stderr, "INV9 FAIL: side=%u tick=%u empty but head=%u tail=%u\n", s, t, level.head_idx, level.tail_idx);
                    return false;
                }
            }
        }

        // side_t is properly scoped — just suppress unused warning
        (void)sd;
    }

    // Invariant 7: next_slot in range
    if (impl.arena.next_slot > MAX_ORDERS) {
        std::fprintf(stderr, "INV7 FAIL: next_slot=%u > MAX_ORDERS\n", impl.arena.next_slot);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------

#define TEST(name) static void test_##name()
#define RUN(name)  do { \
    test_##name(); \
    std::printf("PASS  " #name "\n"); \
} while(0)

// Hand-calculated tick constants (spec: price_to_tick formula)
// tick = (uint32_t)((price - base) * 4.0 + 0.5)
// base = 5500.00
// delta=25.00 → tick 100; delta=25.25 → tick 101; delta=25.50 → tick 102
// delta=50.00 → tick 200; delta=100.00 → tick 400; delta=2199.75 → tick 8799

static constexpr double BASE  = 5500.0;
static constexpr double P0    = 5500.00;   // tick 0
static constexpr double P1    = 5500.25;   // tick 1
static constexpr double P100  = 5525.00;   // tick 100
static constexpr double P101  = 5525.25;   // tick 101
static constexpr double P102  = 5525.50;   // tick 102
static constexpr double P200  = 5550.00;   // tick 200
static constexpr double P400  = 5600.00;   // tick 400
static constexpr double P8799 = 7699.75;   // tick 8799 = MAX_TICKS-1

// ---------------------------------------------------------------------------
// First test: print sizeof(Book::Impl) (spec requirement)
// ---------------------------------------------------------------------------

TEST(T0_sizeof_BookImpl) {
    // Spec says agentTest must print this value — it is not asserted here
    // because the spec explicitly says the value is platform-sensitive and
    // not pre-asserted in the spec itself.
    std::printf("sizeof(Book::Impl) = %zu bytes\n", sizeof(Book::Impl));

    // Sanity: sub-components are verified by static_assert in book.hpp.
    // We do check that the struct is at least the sum of its known fields
    // (no unexpected shrinkage):
    //   sides[2]: 2 * 141904 = 283808
    //   arena: 16000004
    //   base_price: 8
    //   padding: >= 0
    constexpr std::size_t min_impl = 2U * 141904U + 16000004U + 8U;
    assert(sizeof(Book::Impl) >= min_impl);
}

// ---------------------------------------------------------------------------
// Price conversion (hand-calculated, not circular)
// ---------------------------------------------------------------------------

TEST(T1_price_to_tick_known_values) {
    // All expected values are hand-calculated from the formula
    assert(price_to_tick(P0,    BASE) == 0U);
    assert(price_to_tick(P1,    BASE) == 1U);
    assert(price_to_tick(P100,  BASE) == 100U);
    assert(price_to_tick(P101,  BASE) == 101U);
    assert(price_to_tick(P102,  BASE) == 102U);
    assert(price_to_tick(P200,  BASE) == 200U);
    assert(price_to_tick(P400,  BASE) == 400U);
    assert(price_to_tick(P8799, BASE) == 8799U);

    // Invalid inputs
    assert(price_to_tick(BASE - 0.25, BASE) == TICK_INVALID); // below base
    assert(price_to_tick(BASE + 2200.0, BASE) == TICK_INVALID); // tick=8800 = MAX_TICKS
    // NaN
    assert(price_to_tick(std::numeric_limits<double>::quiet_NaN(), BASE) == TICK_INVALID);
    // infinity
    assert(price_to_tick(std::numeric_limits<double>::infinity(), BASE) == TICK_INVALID);
}

// ---------------------------------------------------------------------------
// A1: Add one bid order. Verify count=1, qty, bitmap, id=0.
// ---------------------------------------------------------------------------

TEST(A1_add_one_bid) {
    Book b(BASE);
    assert(check_invariants(b));

    auto id = b.add(side_t::BID, P100, 5U);
    assert(id == 0U);                 // Oracle rule 1: first add → id 0
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 1U);
    assert(b.level_qty(side_t::BID, 100U)   == 5U);
    assert(b.best_bid() == 100U);

    // Raw field access: bitmap set, head_idx=0
    const Book::Impl& impl = b.impl();
    assert(bitmap_is_set(impl.sides[0].bitmap, 100U));
    assert(impl.sides[0].levels[100U].head_idx == 0U);
    assert(impl.sides[0].levels[100U].tail_idx == 0U);
}

// ---------------------------------------------------------------------------
// A2: Two bids at same level, FIFO order
// ---------------------------------------------------------------------------

TEST(A2_add_two_bids_same_level) {
    Book b(BASE);

    auto id0 = b.add(side_t::BID, P100, 10U);
    auto id1 = b.add(side_t::BID, P100, 20U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 2U);
    assert(b.level_qty(side_t::BID, 100U)   == 30U);  // 10+20

    const Book::Impl& impl = b.impl();
    // FIFO: head=id0, next=id1, tail=id1
    assert(impl.sides[0].levels[100U].head_idx == id0);
    assert(impl.arena.nodes[id0].next_idx      == id1);
    assert(impl.sides[0].levels[100U].tail_idx == id1);
}

// ---------------------------------------------------------------------------
// A3: Bid and ask at same tick — independent sides
// ---------------------------------------------------------------------------

TEST(A3_bid_and_ask_same_tick) {
    Book b(BASE);

    auto bid = b.add(side_t::BID, P100, 5U);
    auto ask = b.add(side_t::ASK, P100, 7U);
    assert(bid == 0U);
    assert(ask == 1U);
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 1U);
    assert(b.level_count(side_t::ASK, 100U) == 1U);
    assert(b.level_qty(side_t::BID, 100U)   == 5U);
    assert(b.level_qty(side_t::ASK, 100U)   == 7U);
}

// ---------------------------------------------------------------------------
// A4: Tick 0 boundary
// ---------------------------------------------------------------------------

TEST(A4_add_at_tick_zero) {
    Book b(BASE);
    auto id = b.add(side_t::BID, P0, 1U);
    assert(id == 0U);
    assert(check_invariants(b));
    assert(b.level_count(side_t::BID, 0U) == 1U);
    const Book::Impl& impl = b.impl();
    assert(bitmap_is_set(impl.sides[0].bitmap, 0U));
}

// ---------------------------------------------------------------------------
// A5: Tick MAX_TICKS-1 boundary
// ---------------------------------------------------------------------------

TEST(A5_add_at_tick_max_minus_1) {
    Book b(BASE);
    auto id = b.add(side_t::ASK, P8799, 3U);
    assert(id == 0U);
    assert(check_invariants(b));
    assert(b.level_count(side_t::ASK, MAX_TICKS - 1U) == 1U);
}

// ---------------------------------------------------------------------------
// A6: Price below base → NULL_IDX, no mutation
// ---------------------------------------------------------------------------

TEST(A6_add_price_below_base) {
    Book b(BASE);
    auto id = b.add(side_t::BID, BASE - 0.25, 5U);
    assert(id == NULL_IDX);
    assert(check_invariants(b));
    assert(b.impl().arena.next_slot == 0U);
}

// ---------------------------------------------------------------------------
// A7: Price → tick >= MAX_TICKS → NULL_IDX
// ---------------------------------------------------------------------------

TEST(A7_add_price_tick_overflow) {
    Book b(BASE);
    // BASE + 2200.0 → tick=8800 = MAX_TICKS → overflow
    auto id = b.add(side_t::BID, BASE + 2200.0, 5U);
    assert(id == NULL_IDX);
    assert(check_invariants(b));
    assert(b.impl().arena.next_slot == 0U);
}

// ---------------------------------------------------------------------------
// A8: Quantity=0 → NULL_IDX
// ---------------------------------------------------------------------------

TEST(A8_add_zero_quantity) {
    Book b(BASE);
    auto id = b.add(side_t::BID, P100, 0U);
    assert(id == NULL_IDX);
    assert(check_invariants(b));
    assert(b.impl().arena.next_slot == 0U);
}

// ---------------------------------------------------------------------------
// B1: Add one, cancel it. Level empty, bitmap cleared, DEAD_FLAG set.
// ---------------------------------------------------------------------------

TEST(B1_cancel_only_order) {
    Book b(BASE);

    auto id = b.add(side_t::BID, P100, 10U);
    assert(id == 0U);
    assert(check_invariants(b));

    bool ok = b.cancel(id, side_t::BID, 100U);
    assert(ok);
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 0U);
    assert(b.level_qty(side_t::BID, 100U)   == 0U);
    assert(b.best_bid() == NULL_IDX);

    const Book::Impl& impl = b.impl();
    assert(!bitmap_is_set(impl.sides[0].bitmap, 100U));
    assert(impl.arena.nodes[id].flags & DEAD_FLAG);
    assert(impl.sides[0].levels[100U].head_idx == NULL_IDX);
    assert(impl.sides[0].levels[100U].tail_idx == NULL_IDX);
}

// ---------------------------------------------------------------------------
// B2: Two at same level, cancel head → second becomes head
// ---------------------------------------------------------------------------

TEST(B2_cancel_head_of_two) {
    Book b(BASE);

    auto id0 = b.add(side_t::BID, P100, 5U);
    auto id1 = b.add(side_t::BID, P100, 7U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    assert(b.cancel(id0, side_t::BID, 100U));
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 1U);
    assert(b.level_qty(side_t::BID, 100U)   == 7U);

    const Book::Impl& impl = b.impl();
    assert(impl.sides[0].levels[100U].head_idx == id1);
    assert(impl.sides[0].levels[100U].tail_idx == id1);
    assert(bitmap_is_set(impl.sides[0].bitmap, 100U));
    // Doubly-linked: new head has no predecessor
    assert(impl.arena.nodes[id1].prev_idx == NULL_IDX);
}

// ---------------------------------------------------------------------------
// B3: Two at same level, cancel tail
// ---------------------------------------------------------------------------

TEST(B3_cancel_tail_of_two) {
    Book b(BASE);

    auto id0 = b.add(side_t::BID, P100, 5U);
    auto id1 = b.add(side_t::BID, P100, 7U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    assert(b.cancel(id1, side_t::BID, 100U));
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 1U);
    assert(b.level_qty(side_t::BID, 100U)   == 5U);

    const Book::Impl& impl = b.impl();
    assert(impl.sides[0].levels[100U].head_idx == id0);
    assert(impl.sides[0].levels[100U].tail_idx == id0);
}

// ---------------------------------------------------------------------------
// B4: Five at same level, cancel third (mid-queue)
// ---------------------------------------------------------------------------

TEST(B4_cancel_middle_of_five) {
    Book b(BASE);

    order_id_t ids[5];
    for (uint32_t i = 0; i < 5U; ++i) {
        ids[i] = b.add(side_t::BID, P100, static_cast<qty_t>(i + 1U));
        assert(ids[i] == i);
        assert(check_invariants(b));
    }

    assert(b.cancel(ids[2], side_t::BID, 100U));
    assert(check_invariants(b));

    // Hand-calculated remaining qty: 1+2+4+5 = 12
    assert(b.level_count(side_t::BID, 100U) == 4U);
    assert(b.level_qty(side_t::BID, 100U)   == 12U);

    // Verify FIFO chain: id0→id1→id3→id4
    const Book::Impl& impl = b.impl();
    uint32_t curr = impl.sides[0].levels[100U].head_idx;
    order_id_t expected[] = { ids[0], ids[1], ids[3], ids[4] };
    for (int j = 0; j < 4; ++j) {
        assert(curr == expected[j]);
        curr = impl.arena.nodes[curr].next_idx;
    }
    assert(curr == NULL_IDX);
    // Doubly-linked: after splicing out ids[2], ids[3].prev must be ids[1]
    assert(impl.arena.nodes[ids[3]].prev_idx == ids[1]);
}

// ---------------------------------------------------------------------------
// B5: Cancel never-issued id → false, no mutation
// ---------------------------------------------------------------------------

TEST(B5_cancel_never_issued_id) {
    Book b(BASE);

    auto id = b.add(side_t::BID, P100, 5U);
    assert(id == 0U);
    assert(check_invariants(b));

    // order_id=1 never issued
    assert(!b.cancel(1U, side_t::BID, 100U));
    assert(check_invariants(b));

    // order_id=MAX_ORDERS entirely out of range
    assert(!b.cancel(MAX_ORDERS, side_t::BID, 100U));
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 1U);
}

// ---------------------------------------------------------------------------
// B6: Double cancel → second returns false
// ---------------------------------------------------------------------------

TEST(B6_double_cancel) {
    Book b(BASE);

    auto id = b.add(side_t::BID, P100, 5U);
    assert(id == 0U);

    assert(b.cancel(id, side_t::BID, 100U));
    assert(check_invariants(b));

    assert(!b.cancel(id, side_t::BID, 100U));
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 0U);
}

// ---------------------------------------------------------------------------
// B7: Cancel one of two levels; other unchanged
// ---------------------------------------------------------------------------

TEST(B7_cancel_one_of_two_levels) {
    Book b(BASE);

    auto id0 = b.add(side_t::BID, P100, 5U);
    auto id1 = b.add(side_t::BID, P200, 8U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    assert(b.cancel(id0, side_t::BID, 100U));
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 0U);
    assert(b.level_count(side_t::BID, 200U) == 1U);
    assert(b.level_qty(side_t::BID, 200U)   == 8U);

    const Book::Impl& impl = b.impl();
    assert(!bitmap_is_set(impl.sides[0].bitmap, 100U));
    assert( bitmap_is_set(impl.sides[0].bitmap, 200U));
}

// ---------------------------------------------------------------------------
// B8: Cancel all N at one level → fully empty
// ---------------------------------------------------------------------------

TEST(B8_cancel_all_at_level) {
    Book b(BASE);

    constexpr uint32_t N = 20U;
    order_id_t ids[N];
    for (uint32_t i = 0; i < N; ++i) {
        ids[i] = b.add(side_t::ASK, P100, static_cast<qty_t>(i + 1U));
        assert(ids[i] == i);
        assert(check_invariants(b));
    }

    for (uint32_t i = 0; i < N; ++i) {
        assert(b.cancel(ids[i], side_t::ASK, 100U));
        assert(check_invariants(b));
    }

    assert(b.level_count(side_t::ASK, 100U) == 0U);
    assert(b.level_qty(side_t::ASK, 100U)   == 0U);
    assert(b.best_ask() == NULL_IDX);

    const Book::Impl& impl = b.impl();
    assert(!bitmap_is_set(impl.sides[1].bitmap, 100U));
    assert(impl.sides[1].levels[100U].head_idx == NULL_IDX);
    assert(impl.sides[1].levels[100U].tail_idx == NULL_IDX);
}

// ---------------------------------------------------------------------------
// C1: Exact match one ask
// ---------------------------------------------------------------------------

TEST(C1_match_exact_one_ask) {
    Book b(BASE);

    auto ask_id = b.add(side_t::ASK, P100, 10U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    fill_result_t r = b.match(side_t::BID, P100, 10U, 999U);
    assert(check_invariants(b));

    assert(r.fill_count    == 1U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].maker_order_id == ask_id);
    assert(r.fills[0].taker_order_id == 999U);
    assert(r.fills[0].price_tick     == 100U);
    assert(r.fills[0].filled_qty     == 10U);

    assert(b.level_count(side_t::ASK, 100U) == 0U);
    const Book::Impl& impl = b.impl();
    assert(!bitmap_is_set(impl.sides[1].bitmap, 100U));
    assert(impl.arena.nodes[ask_id].flags & DEAD_FLAG);
}

// ---------------------------------------------------------------------------
// C2: Partial match one ask
// ---------------------------------------------------------------------------

TEST(C2_match_partial_one_ask) {
    Book b(BASE);

    auto ask_id = b.add(side_t::ASK, P100, 20U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    fill_result_t r = b.match(side_t::BID, P100, 10U, 42U);
    assert(check_invariants(b));

    assert(r.fill_count    == 1U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].filled_qty == 10U);

    // Ask node alive, qty decremented to 10
    assert(b.level_count(side_t::ASK, 100U) == 1U);
    assert(b.level_qty(side_t::ASK, 100U)   == 10U);

    const Book::Impl& impl = b.impl();
    assert(!(impl.arena.nodes[ask_id].flags & DEAD_FLAG));
    assert(impl.arena.nodes[ask_id].quantity == 10U);
}

// ---------------------------------------------------------------------------
// C3: Two asks qty=5 each, match qty=8 → first full, second partial
// ---------------------------------------------------------------------------

TEST(C3_match_two_asks_partial) {
    Book b(BASE);

    auto id0 = b.add(side_t::ASK, P100, 5U);
    auto id1 = b.add(side_t::ASK, P100, 5U);
    assert(id0 == 0U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    fill_result_t r = b.match(side_t::BID, P100, 8U, 77U);
    assert(check_invariants(b));

    // Hand-calculated:
    // id0 qty=5 <= 8 → full fill, remaining=3
    // id1 qty=5, fill 3, remaining=0 → partial
    assert(r.fill_count    == 2U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].maker_order_id == id0);
    assert(r.fills[0].filled_qty     == 5U);
    assert(r.fills[1].maker_order_id == id1);
    assert(r.fills[1].filled_qty     == 3U);

    assert(b.level_count(side_t::ASK, 100U) == 1U);
    assert(b.level_qty(side_t::ASK, 100U)   == 2U);

    const Book::Impl& impl = b.impl();
    assert(impl.arena.nodes[id1].quantity == 2U);
    assert(!(impl.arena.nodes[id1].flags & DEAD_FLAG));
}

// ---------------------------------------------------------------------------
// C4: No crossing orders → fill_count=0
// ---------------------------------------------------------------------------

TEST(C4_match_no_crossing) {
    Book b(BASE);

    auto ask_id = b.add(side_t::ASK, P200, 10U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    // BID at tick 100 < ask at 200 → no cross
    fill_result_t r = b.match(side_t::BID, P100, 10U, 1U);
    assert(check_invariants(b));

    assert(r.fill_count    == 0U);
    assert(r.remaining_qty == 10U);
    assert(b.level_count(side_t::ASK, 200U) == 1U);
}

// ---------------------------------------------------------------------------
// C5: Multi-level price priority (asks at 100,101,102; BID at 102, qty=30)
// ---------------------------------------------------------------------------

TEST(C5_match_multi_level_price_priority) {
    Book b(BASE);

    auto id100 = b.add(side_t::ASK, P100, 10U);
    auto id101 = b.add(side_t::ASK, P101, 10U);
    auto id102 = b.add(side_t::ASK, P102, 10U);
    assert(id100 == 0U);
    assert(id101 == 1U);
    assert(id102 == 2U);
    assert(check_invariants(b));

    fill_result_t r = b.match(side_t::BID, P102, 30U, 5U);
    assert(check_invariants(b));

    assert(r.fill_count    == 3U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].price_tick == 100U);
    assert(r.fills[1].price_tick == 101U);
    assert(r.fills[2].price_tick == 102U);
    assert(r.fills[0].filled_qty == 10U);
    assert(r.fills[1].filled_qty == 10U);
    assert(r.fills[2].filled_qty == 10U);

    assert(b.level_count(side_t::ASK, 100U) == 0U);
    assert(b.level_count(side_t::ASK, 101U) == 0U);
    assert(b.level_count(side_t::ASK, 102U) == 0U);
    assert(b.best_ask() == NULL_IDX);
}

// ---------------------------------------------------------------------------
// C6: Exactly 64 fills → remaining_qty > 0, fill_count=64
// ---------------------------------------------------------------------------

TEST(C6_match_exactly_64_fills) {
    Book b(BASE);

    for (uint32_t i = 0; i < 64U; ++i) {
        auto id = b.add(side_t::ASK, P100, 1U);
        assert(id == i);
    }
    assert(check_invariants(b));

    fill_result_t r = b.match(side_t::BID, P100, 100U, 0U);
    assert(check_invariants(b));

    assert(r.fill_count    == 64U);
    assert(r.remaining_qty == 36U);  // 100 - 64 = 36
    // Level now empty (all 64 orders of qty=1 consumed)
    assert(b.level_count(side_t::ASK, 100U) == 0U);
}

// ---------------------------------------------------------------------------
// C7: BID one tick below ask → no cross
// ---------------------------------------------------------------------------

TEST(C7_bid_below_ask_no_cross) {
    Book b(BASE);

    auto ask_id = b.add(side_t::ASK, P100, 10U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    // tick 99 = P100 - 0.25 = 5524.75
    // hand-calculated: (5524.75 - 5500.0) * 4 + 0.5 = 99.5 → truncate → 99
    double p99 = BASE + 99.0 * 0.25;
    assert(price_to_tick(p99, BASE) == 99U);

    fill_result_t r = b.match(side_t::BID, p99, 10U, 1U);
    assert(check_invariants(b));

    assert(r.fill_count    == 0U);
    assert(r.remaining_qty == 10U);
    assert(b.level_count(side_t::ASK, 100U) == 1U);
}

// ---------------------------------------------------------------------------
// D1: Add, cancel, re-add at same level
// ---------------------------------------------------------------------------

TEST(D1_add_cancel_add) {
    Book b(BASE);

    auto id0 = b.add(side_t::BID, P100, 5U);
    assert(id0 == 0U);
    assert(check_invariants(b));

    assert(b.cancel(id0, side_t::BID, 100U));
    assert(check_invariants(b));

    // Slot 0 is dead; second add must get slot 1 (monotonic arena)
    auto id1 = b.add(side_t::BID, P100, 7U);
    assert(id1 == 1U);
    assert(check_invariants(b));

    assert(b.level_count(side_t::BID, 100U) == 1U);
    assert(b.level_qty(side_t::BID, 100U)   == 7U);
    assert(b.impl().sides[0].levels[100U].head_idx == id1);
}

// ---------------------------------------------------------------------------
// D2: Interleaved add and match — remaining in FIFO order
// ---------------------------------------------------------------------------

TEST(D2_interleaved_add_and_match) {
    Book b(BASE);

    constexpr uint32_t N = 10U;
    order_id_t ids[N];
    for (uint32_t i = 0; i < N; ++i) {
        ids[i] = b.add(side_t::ASK, P100, static_cast<qty_t>(i + 1U));
        assert(ids[i] == i);
        assert(check_invariants(b));
    }

    // Match first 5 completely: 1+2+3+4+5 = 15
    fill_result_t r = b.match(side_t::BID, P100, 15U, 42U);
    assert(check_invariants(b));

    assert(r.fill_count    == 5U);
    assert(r.remaining_qty == 0U);
    assert(b.level_count(side_t::ASK, 100U) == 5U);

    // Remaining must be ids[5..9] in FIFO order
    const Book::Impl& impl = b.impl();
    uint32_t curr = impl.sides[1].levels[100U].head_idx;
    for (uint32_t i = 5; i < N; ++i) {
        assert(curr == ids[i]);
        curr = impl.arena.nodes[curr].next_idx;
    }
    assert(curr == NULL_IDX);
}

// ---------------------------------------------------------------------------
// D3: Multi-level add, cancel odd ids, verify oracle
// ---------------------------------------------------------------------------

TEST(D3_add_across_levels_cancel_odd) {
    Book b(BASE);
    Oracle oracle;
    oracle.init();

    tick_t ticks[4]   = { 100U, 101U, 102U, 103U };
    double prices[4]  = { P100, P101, P102, BASE + 103.0 * 0.25 };

    for (uint32_t i = 0; i < 20U; ++i) {
        uint32_t t   = i % 4U;
        auto id = b.add(side_t::BID, prices[t], static_cast<qty_t>(i + 1U));
        assert(id == i);
        oracle.add(id, side_t::BID, ticks[t], i + 1U);
        assert(check_invariants(b));
    }

    for (uint32_t i = 1U; i < 20U; i += 2U) {
        uint32_t t = i % 4U;
        assert(b.cancel(i, side_t::BID, ticks[t]));
        oracle.cancel(i);
        assert(check_invariants(b));
    }

    for (uint32_t t = 0; t < 4U; ++t) {
        tick_t tick  = ticks[t];
        uint32_t cnt = oracle.level_count(side_t::BID, tick);
        qty_t    qty = oracle.level_qty(side_t::BID, tick);
        assert(b.level_count(side_t::BID, tick) == cnt);
        assert(b.level_qty(side_t::BID, tick)   == qty);
        bool exp_bit = (cnt > 0);
        assert(bitmap_is_set(b.impl().sides[0].bitmap, tick) == exp_bit);
    }
}

// ---------------------------------------------------------------------------
// D4: best_bid / best_ask oracle tracking
// ---------------------------------------------------------------------------

TEST(D4_best_bid_ask_oracle) {
    Book b(BASE);
    Oracle oracle;
    oracle.init();

    auto bid100 = b.add(side_t::BID, P100, 1U);
    oracle.add(bid100, side_t::BID, 100U, 1U);
    assert(check_invariants(b));
    assert(b.best_bid() == oracle.best_bid());  // = 100

    auto bid200 = b.add(side_t::BID, P200, 1U);
    oracle.add(bid200, side_t::BID, 200U, 1U);
    assert(check_invariants(b));
    assert(b.best_bid() == oracle.best_bid());  // = 200

    auto bid400 = b.add(side_t::BID, P400, 1U);
    oracle.add(bid400, side_t::BID, 400U, 1U);
    assert(check_invariants(b));
    assert(b.best_bid() == oracle.best_bid());  // = 400

    auto ask200 = b.add(side_t::ASK, P200, 1U);
    oracle.add(ask200, side_t::ASK, 200U, 1U);
    assert(check_invariants(b));
    assert(b.best_ask() == oracle.best_ask());  // = 200

    b.cancel(bid400, side_t::BID, 400U);
    oracle.cancel(bid400);
    assert(check_invariants(b));
    assert(b.best_bid() == oracle.best_bid());  // = 200

    b.cancel(bid200, side_t::BID, 200U);
    oracle.cancel(bid200);
    assert(check_invariants(b));
    assert(b.best_bid() == oracle.best_bid());  // = 100
}

// ---------------------------------------------------------------------------
// D5: Reset clears all state, retains base_price
// ---------------------------------------------------------------------------

TEST(D5_reset) {
    Book b(BASE);

    auto r0 = b.add(side_t::BID, P100, 5U);
    auto r1 = b.add(side_t::ASK, P100, 3U);
    (void)r0; (void)r1;
    assert(check_invariants(b));
    auto r2 = b.match(side_t::BID, P100, 3U, 99U);
    (void)r2;
    assert(check_invariants(b));

    b.reset();
    assert(check_invariants(b));

    assert(b.best_bid() == NULL_IDX);
    assert(b.best_ask() == NULL_IDX);
    assert(b.impl().arena.next_slot == 0U);
    assert(b.impl().base_price == BASE);

    for (uint32_t t = 0; t < MAX_TICKS; ++t) {
        const Book::Impl& impl = b.impl();
        assert(impl.sides[0].levels[t].head_idx == NULL_IDX);
        assert(impl.sides[0].levels[t].tail_idx == NULL_IDX);
        assert(impl.sides[0].levels[t].count    == 0U);
        assert(impl.sides[1].levels[t].head_idx == NULL_IDX);
        assert(impl.sides[1].levels[t].tail_idx == NULL_IDX);
        assert(impl.sides[1].levels[t].count    == 0U);
    }

    for (uint32_t w = 0; w < BITMAP_WORDS; ++w) {
        assert(b.impl().sides[0].bitmap[w] == 0U);
        assert(b.impl().sides[1].bitmap[w] == 0U);
    }
}

// ---------------------------------------------------------------------------
// D6: cancel with tick >= MAX_TICKS → false
// ---------------------------------------------------------------------------

TEST(D6_cancel_invalid_tick) {
    Book b(BASE);
    auto id = b.add(side_t::BID, P100, 5U);
    assert(id == 0U);
    assert(check_invariants(b));

    assert(!b.cancel(id, side_t::BID, MAX_TICKS));
    assert(check_invariants(b));
    assert(b.level_count(side_t::BID, 100U) == 1U);
}

// ---------------------------------------------------------------------------
// D7: ASK aggressor match (symmetric path in matcher)
// ---------------------------------------------------------------------------

TEST(D7_ask_aggressor_match) {
    Book b(BASE);

    auto bid_id = b.add(side_t::BID, P100, 10U);
    assert(bid_id == 0U);
    assert(check_invariants(b));

    fill_result_t r = b.match(side_t::ASK, P100, 10U, 55U);
    assert(check_invariants(b));

    assert(r.fill_count    == 1U);
    assert(r.remaining_qty == 0U);
    assert(r.fills[0].maker_order_id == bid_id);
    assert(r.fills[0].taker_order_id == 55U);
    assert(r.fills[0].price_tick     == 100U);
    assert(r.fills[0].filled_qty     == 10U);

    assert(b.level_count(side_t::BID, 100U) == 0U);
}

// ---------------------------------------------------------------------------
// D8: Match with invalid price → fill_count=0, no mutation
// ---------------------------------------------------------------------------

TEST(D8_match_invalid_price) {
    Book b(BASE);
    auto ask_id = b.add(side_t::ASK, P100, 5U);
    assert(ask_id == 0U);
    assert(check_invariants(b));

    // price below base → TICK_INVALID
    fill_result_t r = b.match(side_t::BID, BASE - 1.0, 5U, 0U);
    assert(check_invariants(b));
    assert(r.fill_count    == 0U);
    assert(r.remaining_qty == 5U);
    assert(b.level_count(side_t::ASK, 100U) == 1U);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    // T0 prints sizeof(Book::Impl) — required by spec
    RUN(T0_sizeof_BookImpl);

    RUN(T1_price_to_tick_known_values);
    RUN(A1_add_one_bid);
    RUN(A2_add_two_bids_same_level);
    RUN(A3_bid_and_ask_same_tick);
    RUN(A4_add_at_tick_zero);
    RUN(A5_add_at_tick_max_minus_1);
    RUN(A6_add_price_below_base);
    RUN(A7_add_price_tick_overflow);
    RUN(A8_add_zero_quantity);
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
    RUN(C7_bid_below_ask_no_cross);
    RUN(D1_add_cancel_add);
    RUN(D2_interleaved_add_and_match);
    RUN(D3_add_across_levels_cancel_odd);
    RUN(D4_best_bid_ask_oracle);
    RUN(D5_reset);
    RUN(D6_cancel_invalid_tick);
    RUN(D7_ask_aggressor_match);
    RUN(D8_match_invalid_price);

    std::printf("\n33 tests passed.\n");
    return 0;
}
