/* test_book.cpp — Correctness test suite for the ETH/USDT L2 orderbook (C++).
 *
 * Test discipline:
 *   - All expected values are derived by hand calculation or mathematical
 *     property — never by running the function under test (no circular tests).
 *   - check_invariants() is called after every mutating operation.
 *   - Oracle tracks independent state for per-level qty and best_bid/best_ask.
 *   - Invalid-input cases covered: TICK_INVALID, ticks outside window.
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

using namespace eth::book;
using namespace eth::book::internal;

// ---------------------------------------------------------------------------
// Oracle — independent state tracking.
// Expected values are computed by the Oracle's own linear scan, NOT by the
// book implementation. The Oracle is a reference for correctness, not a
// mirror of the implementation.
// ---------------------------------------------------------------------------

struct Oracle {
    struct LevelEntry {
        tick_t tick;
        qty_t  qty;
    };

    static constexpr uint32_t MAX_ENTRIES = 10000U;

    LevelEntry bid_levels[MAX_ENTRIES];
    LevelEntry ask_levels[MAX_ENTRIES];
    uint32_t   n_bid = 0U;
    uint32_t   n_ask = 0U;

    // Remove all entries (used on reset).
    void clear() noexcept {
        n_bid = 0U;
        n_ask = 0U;
    }

    // Set or update a level. qty == 0 removes it.
    void upsert(side_t side, tick_t tick, qty_t qty) noexcept {
        LevelEntry* levels = (side == side_t::BID) ? bid_levels : ask_levels;
        uint32_t&   n      = (side == side_t::BID) ? n_bid      : n_ask;

        // Find existing entry.
        for (uint32_t i = 0U; i < n; ++i) {
            if (levels[i].tick == tick) {
                if (qty == 0U) {
                    // Delete: replace with last element.
                    levels[i] = levels[n - 1U];
                    --n;
                } else {
                    levels[i].qty = qty;
                }
                return;
            }
        }

        // Not found: insert if qty > 0.
        if (qty > 0U) {
            assert(n < MAX_ENTRIES);
            levels[n].tick = tick;
            levels[n].qty  = qty;
            ++n;
        }
    }

    // Query qty for a specific level. Returns 0 if absent.
    [[nodiscard]] qty_t level_qty(side_t side, tick_t tick) const noexcept {
        const LevelEntry* levels = (side == side_t::BID) ? bid_levels : ask_levels;
        uint32_t          n      = (side == side_t::BID) ? n_bid      : n_ask;

        for (uint32_t i = 0U; i < n; ++i)
            if (levels[i].tick == tick)
                return levels[i].qty;
        return 0U;
    }

    // Best bid: max tick with qty > 0. Returns TICK_INVALID if empty.
    [[nodiscard]] tick_t best_bid() const noexcept {
        if (n_bid == 0U) return TICK_INVALID;
        tick_t best = 0U;
        bool   found = false;
        for (uint32_t i = 0U; i < n_bid; ++i) {
            if (!found || bid_levels[i].tick > best) {
                best  = bid_levels[i].tick;
                found = true;
            }
        }
        return found ? best : TICK_INVALID;
    }

    // Best ask: min tick with qty > 0. Returns TICK_INVALID if empty.
    [[nodiscard]] tick_t best_ask() const noexcept {
        if (n_ask == 0U) return TICK_INVALID;
        tick_t best  = TICK_INVALID;
        bool   found = false;
        for (uint32_t i = 0U; i < n_ask; ++i) {
            if (!found || ask_levels[i].tick < best) {
                best  = ask_levels[i].tick;
                found = true;
            }
        }
        return found ? best : TICK_INVALID;
    }
};

// ---------------------------------------------------------------------------
// check_invariants — called after every mutating operation.
//
// Verifies:
//   1. For every tick in oracle bid/ask sets: book.level_qty matches oracle.
//   2. book.best_bid() == oracle.best_bid().
//   3. book.best_ask() == oracle.best_ask().
//   4. Bitmap consistency: for every window slot,
//      bitmap_is_set(slot) == (level.total_qty > 0).
// ---------------------------------------------------------------------------

static bool check_invariants(const Book& book, const Oracle& oracle,
                              const char* where) {
    const Book::Impl& impl  = book.impl();

    // Check 1: level qty agreement for all oracle-tracked levels.
    for (uint32_t i = 0U; i < oracle.n_bid; ++i) {
        tick_t  tick     = oracle.bid_levels[i].tick;
        qty_t   expected = oracle.bid_levels[i].qty;
        qty_t   got      = book.level_qty(side_t::BID, tick);
        if (got != expected) {
            fprintf(stderr,
                "INVARIANT FAIL [%s]: BID tick %u: expected qty %llu got %llu\n",
                where,
                (unsigned)tick,
                (unsigned long long)expected,
                (unsigned long long)got);
            return false;
        }
    }
    for (uint32_t i = 0U; i < oracle.n_ask; ++i) {
        tick_t  tick     = oracle.ask_levels[i].tick;
        qty_t   expected = oracle.ask_levels[i].qty;
        qty_t   got      = book.level_qty(side_t::ASK, tick);
        if (got != expected) {
            fprintf(stderr,
                "INVARIANT FAIL [%s]: ASK tick %u: expected qty %llu got %llu\n",
                where,
                (unsigned)tick,
                (unsigned long long)expected,
                (unsigned long long)got);
            return false;
        }
    }

    // Check 2: best_bid agrees with oracle.
    tick_t exp_bid = oracle.best_bid();
    tick_t got_bid = book.best_bid();
    if (got_bid != exp_bid) {
        fprintf(stderr,
            "INVARIANT FAIL [%s]: best_bid: expected %u got %u\n",
            where,
            (unsigned)exp_bid,
            (unsigned)got_bid);
        return false;
    }

    // Check 3: best_ask agrees with oracle.
    tick_t exp_ask = oracle.best_ask();
    tick_t got_ask = book.best_ask();
    if (got_ask != exp_ask) {
        fprintf(stderr,
            "INVARIANT FAIL [%s]: best_ask: expected %u got %u\n",
            where,
            (unsigned)exp_ask,
            (unsigned)got_ask);
        return false;
    }

    // Check 4: bitmap consistency for each side.
    for (uint8_t s = 0U; s < 2U; ++s) {
        const book_side_t& bside = impl.sides[s];
        for (uint32_t widx = 0U; widx < WINDOW_SIZE; ++widx) {
            bool bit_set  = bitmap_is_set(bside, widx);
            bool has_qty  = (bside.levels[widx].total_qty > 0U);
            if (bit_set != has_qty) {
                fprintf(stderr,
                    "INVARIANT FAIL [%s]: side %u slot %u: bit_set=%d has_qty=%d\n",
                    where,
                    (unsigned)s,
                    (unsigned)widx,
                    (int)bit_set,
                    (int)has_qty);
                return false;
            }
        }
    }

    return true;
}

#define CHECK(book, oracle) do { \
    assert(check_invariants((book), (oracle), __func__)); \
} while (0)

// ---------------------------------------------------------------------------
// T1 — Layout sizes
// Expected values: hand-computed per architect-spec.md.
//   price_level_t:  1 × uint64_t = 8 bytes.
//   book_side_t:    65536×8 + 1024×8 + 4 + 4 + 16×8 = 524288 + 8192 + 4 + 4 + 128 = 532616.
//   Book::Impl:     2×532616 + 8 + 8 = 1065232 + 16 = 1065248.
// ---------------------------------------------------------------------------

static void test_t1_layout_sizes() {
    static_assert(sizeof(price_level_t) == 8U,    "price_level_t size");
    static_assert(sizeof(book_side_t)   == 532616U, "book_side_t size");
    static_assert(sizeof(Book::Impl)    == 1065248U, "Book::Impl size");

    assert(sizeof(price_level_t) == 8U);
    assert(sizeof(book_side_t)   == 532616U);
    printf("  sizeof(Book::Impl) = %zu\n", sizeof(Book::Impl));
    assert(sizeof(Book::Impl) == 1065248U);
}

// ---------------------------------------------------------------------------
// T2 — Empty book
// base_tick = 267000 (price $2670.00); window covers 267000..332535.
// Both best_bid/best_ask must be TICK_INVALID; needs_rebase must be false.
// ---------------------------------------------------------------------------

static void test_t2_empty_book() {
    Book    b(267000ULL);
    Oracle  o;

    assert(b.best_bid()    == TICK_INVALID);
    assert(b.best_ask()    == TICK_INVALID);
    assert(b.needs_rebase() == false);
    CHECK(b, o);
}

// ---------------------------------------------------------------------------
// T3 — Single upsert bid
// Insert BID at tick 300000 (price $3000.00), qty 1000000 (0.01 ETH).
// Hand-calc: window distance = 300000 - 267000 = 33000, inside window.
// ---------------------------------------------------------------------------

static void test_t3_single_upsert_bid() {
    Book   b(267000ULL);
    Oracle o;

    bool ok = b.upsert_by_tick(side_t::BID, 300000U, 1000000ULL);
    assert(ok == true);
    o.upsert(side_t::BID, 300000U, 1000000ULL);
    CHECK(b, o);

    assert(b.best_bid() == 300000U);
    assert(b.best_ask() == TICK_INVALID);
    assert(b.level_qty(side_t::BID, 300000U) == 1000000ULL);
}

// ---------------------------------------------------------------------------
// T4 — Single upsert ask
// Insert ASK at tick 300001 (price $3000.01), qty 2000000 (0.02 ETH).
// ---------------------------------------------------------------------------

static void test_t4_single_upsert_ask() {
    Book   b(267000ULL);
    Oracle o;

    bool ok = b.upsert_by_tick(side_t::ASK, 300001U, 2000000ULL);
    assert(ok == true);
    o.upsert(side_t::ASK, 300001U, 2000000ULL);
    CHECK(b, o);

    assert(b.best_ask() == 300001U);
    assert(b.best_bid() == TICK_INVALID);
    assert(b.level_qty(side_t::ASK, 300001U) == 2000000ULL);
}

// ---------------------------------------------------------------------------
// T5 — Multiple bid levels, best_bid tracks highest
// Insert BIDs at 299998, 299999, 300000. Highest is 300000.
// ---------------------------------------------------------------------------

static void test_t5_multiple_bids_best_tracks_highest() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::BID, 299998U, 500000ULL));
    o.upsert(side_t::BID, 299998U, 500000ULL);
    CHECK(b, o);

    assert(b.upsert_by_tick(side_t::BID, 299999U, 600000ULL));
    o.upsert(side_t::BID, 299999U, 600000ULL);
    CHECK(b, o);

    assert(b.upsert_by_tick(side_t::BID, 300000U, 700000ULL));
    o.upsert(side_t::BID, 300000U, 700000ULL);
    CHECK(b, o);

    assert(b.best_bid() == 300000U);
}

// ---------------------------------------------------------------------------
// T6 — Multiple ask levels, best_ask tracks lowest
// Insert ASKs at 300001, 300002, 300003. Lowest is 300001.
// ---------------------------------------------------------------------------

static void test_t6_multiple_asks_best_tracks_lowest() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::ASK, 300003U, 500000ULL));
    o.upsert(side_t::ASK, 300003U, 500000ULL);
    CHECK(b, o);

    assert(b.upsert_by_tick(side_t::ASK, 300002U, 600000ULL));
    o.upsert(side_t::ASK, 300002U, 600000ULL);
    CHECK(b, o);

    assert(b.upsert_by_tick(side_t::ASK, 300001U, 700000ULL));
    o.upsert(side_t::ASK, 300001U, 700000ULL);
    CHECK(b, o);

    assert(b.best_ask() == 300001U);
}

// ---------------------------------------------------------------------------
// T7 — Update existing level qty (absolute set, not accumulate)
// Upsert BID at 300000 with 1000000, then 2000000. Result must be 2000000.
// ---------------------------------------------------------------------------

static void test_t7_update_existing_level() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::BID, 300000U, 1000000ULL));
    o.upsert(side_t::BID, 300000U, 1000000ULL);
    CHECK(b, o);

    assert(b.level_qty(side_t::BID, 300000U) == 1000000ULL);

    assert(b.upsert_by_tick(side_t::BID, 300000U, 2000000ULL));
    o.upsert(side_t::BID, 300000U, 2000000ULL);
    CHECK(b, o);

    assert(b.level_qty(side_t::BID, 300000U) == 2000000ULL);
}

// ---------------------------------------------------------------------------
// T8 — Delete non-best level (best_tick does not change)
// BIDs at 299998, 299999, 300000. Delete 299998. best_bid stays 300000.
// ---------------------------------------------------------------------------

static void test_t8_delete_non_best_level() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::BID, 299998U, 500000ULL));
    o.upsert(side_t::BID, 299998U, 500000ULL);
    assert(b.upsert_by_tick(side_t::BID, 299999U, 600000ULL));
    o.upsert(side_t::BID, 299999U, 600000ULL);
    assert(b.upsert_by_tick(side_t::BID, 300000U, 700000ULL));
    o.upsert(side_t::BID, 300000U, 700000ULL);
    CHECK(b, o);

    // Delete the non-best level.
    bool ok = b.upsert_by_tick(side_t::BID, 299998U, 0ULL);
    assert(ok == true);
    o.upsert(side_t::BID, 299998U, 0ULL);
    CHECK(b, o);

    assert(b.best_bid() == 300000U);
    assert(b.level_qty(side_t::BID, 299998U) == 0ULL);
}

// ---------------------------------------------------------------------------
// T9 — Delete best level (best_tick fallback fires)
// BIDs at 299998, 299999, 300000. Delete 300000. best_bid falls back to 299999.
// ---------------------------------------------------------------------------

static void test_t9_delete_best_level_fallback() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::BID, 299998U, 500000ULL));
    o.upsert(side_t::BID, 299998U, 500000ULL);
    assert(b.upsert_by_tick(side_t::BID, 299999U, 600000ULL));
    o.upsert(side_t::BID, 299999U, 600000ULL);
    assert(b.upsert_by_tick(side_t::BID, 300000U, 700000ULL));
    o.upsert(side_t::BID, 300000U, 700000ULL);
    CHECK(b, o);

    // Delete the best level — must trigger hierarchical bitmap fallback.
    bool ok = b.upsert_by_tick(side_t::BID, 300000U, 0ULL);
    assert(ok == true);
    o.upsert(side_t::BID, 300000U, 0ULL);
    CHECK(b, o);

    // Hand-calc: after deleting 300000, next best bid is 299999.
    assert(b.best_bid() == 299999U);
}

// ---------------------------------------------------------------------------
// T10 — Delete sole level (side becomes empty → TICK_INVALID)
// ---------------------------------------------------------------------------

static void test_t10_delete_sole_level_side_becomes_empty() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::BID, 300000U, 1000000ULL));
    o.upsert(side_t::BID, 300000U, 1000000ULL);
    CHECK(b, o);

    bool ok = b.upsert_by_tick(side_t::BID, 300000U, 0ULL);
    assert(ok == true);
    o.upsert(side_t::BID, 300000U, 0ULL);
    CHECK(b, o);

    assert(b.best_bid() == TICK_INVALID);
}

// ---------------------------------------------------------------------------
// T11 — Interleaved bid/ask, spread check
// BIDs at 299990–299999 (10 levels). ASKs at 300001–300010 (10 levels).
// Verify best_bid, best_ask, then walk them down one step each.
// ---------------------------------------------------------------------------

static void test_t11_interleaved_bid_ask_spread() {
    Book   b(267000ULL);
    Oracle o;

    // Insert 10 BID levels at 299990..299999.
    for (uint32_t t = 299990U; t <= 299999U; ++t) {
        assert(b.upsert_by_tick(side_t::BID, t, 100000000ULL));
        o.upsert(side_t::BID, t, 100000000ULL);
        CHECK(b, o);
    }

    // Insert 10 ASK levels at 300001..300010.
    for (uint32_t t = 300001U; t <= 300010U; ++t) {
        assert(b.upsert_by_tick(side_t::ASK, t, 100000000ULL));
        o.upsert(side_t::ASK, t, 100000000ULL);
        CHECK(b, o);
    }

    // Hand-calc: best_bid = 299999, best_ask = 300001.
    assert(b.best_bid() == 299999U);
    assert(b.best_ask() == 300001U);

    // Delete best bid 299999 → fallback to 299998.
    assert(b.upsert_by_tick(side_t::BID, 299999U, 0ULL));
    o.upsert(side_t::BID, 299999U, 0ULL);
    CHECK(b, o);
    assert(b.best_bid() == 299998U);

    // Delete best ask 300001 → fallback to 300002.
    assert(b.upsert_by_tick(side_t::ASK, 300001U, 0ULL));
    o.upsert(side_t::ASK, 300001U, 0ULL);
    CHECK(b, o);
    assert(b.best_ask() == 300002U);
}

// ---------------------------------------------------------------------------
// T12 — reset() clears all state
// Add several levels. Call reset(new_base). All must be TICK_INVALID / zero qty.
// ---------------------------------------------------------------------------

static void test_t12_reset_clears_all_state() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::BID, 300000U, 1000000ULL));
    assert(b.upsert_by_tick(side_t::ASK, 300001U, 2000000ULL));
    assert(b.upsert_by_tick(side_t::BID, 299999U, 3000000ULL));

    // Reset to a new base.
    b.reset(280000ULL);
    o.clear();

    CHECK(b, o);
    assert(b.best_bid() == TICK_INVALID);
    assert(b.best_ask() == TICK_INVALID);

    // All three ticks that were set — they're now in a different base or
    // simply not present. level_qty on the old ticks should return 0 if
    // they're outside the new window, or 0 (cleared) if inside.
    // New base = 280000, window covers 280000..345535.
    // 300000 is inside: distance = 20000, within WINDOW_SIZE=65536 → cleared.
    assert(b.level_qty(side_t::BID, 300000U) == 0ULL);
    assert(b.level_qty(side_t::ASK, 300001U) == 0ULL);
    assert(b.level_qty(side_t::BID, 299999U) == 0ULL);
}

// ---------------------------------------------------------------------------
// T13 — Ticks outside window return false from upsert_by_tick
//
// TICK_INVALID (0xFFFFFFFF) is explicitly rejected by upsert_by_tick.
// A tick that maps outside the window (distance > WINDOW_MASK) is also rejected.
//
// base_tick = 267000.
// A tick below base maps to a large uint64_t distance (underflow), > WINDOW_MASK.
// Hand-calc: distance = (uint64_t)266999 - 267000 = 0xFFFFFFFFFFFFFFFF > WINDOW_MASK.
// ---------------------------------------------------------------------------

static void test_t13_tick_outside_window_rejected() {
    Book b(267000ULL);

    // TICK_INVALID is explicitly rejected.
    assert(b.upsert_by_tick(side_t::BID, TICK_INVALID, 1000000ULL) == false);

    // Tick below base: distance underflows → rejected.
    // 266999 < 267000 → uint64_t distance is huge.
    assert(b.upsert_by_tick(side_t::BID, 266999U, 1000000ULL) == false);

    // Tick at upper edge: 267000 + 65535 = 332535 → distance 65535 = WINDOW_MASK → OK.
    assert(b.upsert_by_tick(side_t::BID, 332535U, 1000000ULL) == true);

    // Tick one beyond upper edge: 267000 + 65536 = 332536 → distance 65536 > WINDOW_MASK → rejected.
    assert(b.upsert_by_tick(side_t::BID, 332536U, 1000000ULL) == false);
}

// ---------------------------------------------------------------------------
// T14 — needs_rebase trigger
//
// base_tick = 267000. REBASE_MARGIN = 8192.
// needs_rebase triggers when bid_widx < REBASE_MARGIN, i.e. bid_widx < 8192.
//
// For bid_widx = 8191: absolute_tick = 267000 + 8191 = 275191.
//   distance = 275191 - 267000 = 8191 < 8192 → triggers.
// For bid_widx = 8192: absolute_tick = 267000 + 8192 = 275192.
//   distance = 275192 - 267000 = 8192 → not < REBASE_MARGIN → does NOT trigger.
//
// Empty book → needs_rebase = false (both sides use centre = WINDOW_SIZE/2 = 32768).
// ---------------------------------------------------------------------------

static void test_t14_needs_rebase() {
    Book b(267000ULL);

    // Empty book — both sides at centre, no trigger.
    assert(b.needs_rebase() == false);

    // Insert BID at widx = 8192 (absolute 275192). Not within margin.
    assert(b.upsert_by_tick(side_t::BID, 275192U, 1000000ULL));
    assert(b.needs_rebase() == false);

    // Delete that level, insert at widx = 8191 (absolute 275191). Triggers.
    assert(b.upsert_by_tick(side_t::BID, 275192U, 0ULL));
    assert(b.upsert_by_tick(side_t::BID, 275191U, 1000000ULL));
    assert(b.needs_rebase() == true);
}

// ---------------------------------------------------------------------------
// T14b — needs_rebase trigger on ask side (high edge)
//
// base_tick = 267000. WINDOW_SIZE = 65536. REBASE_MARGIN = 8192.
// High edge: ask_widx >= 65536 - 8192 = 57344.
//
// ask_widx = 57344: absolute_tick = 267000 + 57344 = 324344 → triggers.
// ask_widx = 57343: absolute_tick = 267000 + 57343 = 324343 → does NOT trigger.
// ---------------------------------------------------------------------------

static void test_t14b_needs_rebase_ask_high_edge() {
    Book b(267000ULL);

    assert(b.needs_rebase() == false);

    // Insert ASK at widx = 57343 (absolute 324343). Not within high margin.
    assert(b.upsert_by_tick(side_t::ASK, 324343U, 1000000ULL));
    assert(b.needs_rebase() == false);

    // Delete it, insert at widx = 57344 (absolute 324344). Triggers.
    assert(b.upsert_by_tick(side_t::ASK, 324343U, 0ULL));
    assert(b.upsert_by_tick(side_t::ASK, 324344U, 1000000ULL));
    assert(b.needs_rebase() == true);
}

// ---------------------------------------------------------------------------
// T15 — Stress: 5000 BID + 5000 ASK levels, batch check, then drain both sides
//
// BID ticks: 295000, 295002, 295004, ..., 295000 + 2×4999 = 304998 (even steps).
// ASK ticks: 305001, 305003, 305005, ..., 305001 + 2×4999 = 315000 - 1 = 314999.
// All ticks must be inside window [267000, 332535].
// Hand-calc: 304998 < 332535 ✓; 314999 < 332535 ✓.
// ---------------------------------------------------------------------------

static void test_t15_stress_5000_bid_5000_ask() {
    Book   b(267000ULL);
    Oracle o;

    static constexpr uint32_t N = 5000U;

    // Insert 5000 BID levels at even ticks from 295000.
    for (uint32_t i = 0U; i < N; ++i) {
        uint32_t tick = 295000U + i * 2U;
        assert(b.upsert_by_tick(side_t::BID, tick, 100000000ULL));
        o.upsert(side_t::BID, tick, 100000000ULL);
    }

    // Insert 5000 ASK levels at odd ticks from 305001.
    for (uint32_t i = 0U; i < N; ++i) {
        uint32_t tick = 305001U + i * 2U;
        assert(b.upsert_by_tick(side_t::ASK, tick, 100000000ULL));
        o.upsert(side_t::ASK, tick, 100000000ULL);
    }

    // Single invariant check after full batch.
    CHECK(b, o);

    // Hand-calc: best_bid = 295000 + 2×4999 = 304998.
    //            best_ask = 305001.
    assert(b.best_bid() == 304998U);
    assert(b.best_ask() == 305001U);

    // Delete all BID levels. best_bid must reach TICK_INVALID.
    for (uint32_t i = 0U; i < N; ++i) {
        uint32_t tick = 295000U + i * 2U;
        assert(b.upsert_by_tick(side_t::BID, tick, 0ULL));
        o.upsert(side_t::BID, tick, 0ULL);
    }
    assert(b.best_bid() == TICK_INVALID);

    // Delete all ASK levels. best_ask must reach TICK_INVALID.
    for (uint32_t i = 0U; i < N; ++i) {
        uint32_t tick = 305001U + i * 2U;
        assert(b.upsert_by_tick(side_t::ASK, tick, 0ULL));
        o.upsert(side_t::ASK, tick, 0ULL);
    }
    assert(b.best_ask() == TICK_INVALID);

    CHECK(b, o);
}

// ---------------------------------------------------------------------------
// T16 — parse_price (via the public upsert() string API)
//
// parse_price is private static. We test it indirectly via upsert(), which
// calls parse_price internally. A successful upsert followed by a correct
// level_qty confirms the parse was correct.
//
// All expected ticks are hand-calculated:
//   "3000.00" → int_part=3000, frac_cents=0  → tick = 3000×100 + 0  = 300000
//   "3000.01" → int_part=3000, frac_cents=1  → tick = 3000×100 + 1  = 300001
//   "2999.99" → int_part=2999, frac_cents=99 → tick = 2999×100 + 99 = 299999
//   "0.01"    → int_part=0,    frac_cents=1  → tick = 0×100    + 1  = 1
//   "10000000.00" → int_part=10000000, frac_cents=0 → tick = 1000000000
//                   BUT 1000000000 < UINT32_MAX (4294967295) → fits tick_t.
//                   Distance from base 267000 = 999733000 > WINDOW_MASK = 65535 → rejected.
//   ""        → TICK_INVALID → upsert returns false.
//
// All upserts use base_tick = 267000; only ticks inside the window should succeed.
// ---------------------------------------------------------------------------

static void test_t16_parse_price_via_upsert() {
    Book b(267000ULL);

    // "3000.00" → tick 300000. Distance from base 267000 = 33000, inside window.
    {
        const char* price = "3000.00";
        const char* qty   = "0.01000000";  // 0.01 ETH = 1000000 units
        bool ok = b.upsert(side_t::BID, price, 7U, qty, 10U);
        assert(ok == true);
        // Verify level appeared at tick 300000.
        assert(b.level_qty(side_t::BID, 300000U) == 1000000ULL);
        // Clean up.
        assert(b.upsert_by_tick(side_t::BID, 300000U, 0ULL));
    }

    // "3000.01" → tick 300001. Distance 33001, inside window.
    {
        const char* price = "3000.01";
        const char* qty   = "0.01000000";
        bool ok = b.upsert(side_t::BID, price, 7U, qty, 10U);
        assert(ok == true);
        assert(b.level_qty(side_t::BID, 300001U) == 1000000ULL);
        assert(b.upsert_by_tick(side_t::BID, 300001U, 0ULL));
    }

    // "2999.99" → tick 299999. Distance 32999, inside window.
    {
        const char* price = "2999.99";
        const char* qty   = "0.01000000";
        bool ok = b.upsert(side_t::BID, price, 7U, qty, 10U);
        assert(ok == true);
        assert(b.level_qty(side_t::BID, 299999U) == 1000000ULL);
        assert(b.upsert_by_tick(side_t::BID, 299999U, 0ULL));
    }

    // "0.01" → tick 1. Distance from base 267000 = 1 - 267000 = underflow → outside window.
    // upsert must return false (parse succeeds to tick 1, but window rejects it).
    {
        const char* price = "0.01";
        const char* qty   = "0.01000000";
        bool ok = b.upsert(side_t::BID, price, 4U, qty, 10U);
        assert(ok == false);
    }

    // "10000000.00" → tick 1000000000. Outside window. Must return false.
    {
        const char* price = "10000000.00";
        const char* qty   = "0.01000000";
        bool ok = b.upsert(side_t::BID, price, 11U, qty, 10U);
        assert(ok == false);
    }

    // "" (empty) → parse_price returns TICK_INVALID → upsert returns false.
    {
        bool ok = b.upsert(side_t::BID, "", 0U, "0.01000000", 10U);
        assert(ok == false);
    }

    // "30.0x" → non-digit character → parse_price returns TICK_INVALID → false.
    {
        const char* price = "30.0x";
        bool ok = b.upsert(side_t::BID, price, 5U, "0.01000000", 10U);
        assert(ok == false);
    }
}

// ---------------------------------------------------------------------------
// T17 — apply_snapshot
// reset() followed by apply_snapshot() for both sides. Verify levels applied
// correctly and best_bid/best_ask are correct.
// ---------------------------------------------------------------------------

static void test_t17_apply_snapshot() {
    Book b(267000ULL);

    static constexpr uint32_t SNAP_COUNT = 5U;

    // BID snapshot: ticks 299996..300000 (hand-computed), qtys 100000000..500000000.
    tick_t bid_ticks[SNAP_COUNT] = { 299996U, 299997U, 299998U, 299999U, 300000U };
    qty_t  bid_qtys[SNAP_COUNT]  = { 100000000ULL, 200000000ULL, 300000000ULL,
                                     400000000ULL, 500000000ULL };

    // ASK snapshot: ticks 300001..300005, qtys 600000000..1000000000.
    tick_t ask_ticks[SNAP_COUNT] = { 300001U, 300002U, 300003U, 300004U, 300005U };
    qty_t  ask_qtys[SNAP_COUNT]  = { 600000000ULL, 700000000ULL, 800000000ULL,
                                     900000000ULL, 1000000000ULL };

    b.reset(267000ULL);
    uint32_t applied_bid = b.apply_snapshot(side_t::BID, bid_ticks, bid_qtys, SNAP_COUNT);
    uint32_t applied_ask = b.apply_snapshot(side_t::ASK, ask_ticks, ask_qtys, SNAP_COUNT);

    assert(applied_bid == SNAP_COUNT);
    assert(applied_ask == SNAP_COUNT);

    // Verify best_bid = 300000 (highest bid tick), best_ask = 300001 (lowest ask tick).
    assert(b.best_bid() == 300000U);
    assert(b.best_ask() == 300001U);

    // Spot-check a few levels.
    assert(b.level_qty(side_t::BID, 300000U) == 500000000ULL);
    assert(b.level_qty(side_t::BID, 299996U) == 100000000ULL);
    assert(b.level_qty(side_t::ASK, 300001U) == 600000000ULL);
    assert(b.level_qty(side_t::ASK, 300005U) == 1000000000ULL);
}

// ---------------------------------------------------------------------------
// T18 — rebase() repositions window and preserves live levels
//
// Start with base 267000. Insert BID at 300000, ASK at 300001.
// Rebase to 290000 (rounded down to 64-tick boundary: 290000 & ~63 = 289984).
// After rebase: both levels must still be present and best_bid/best_ask correct.
//
// Hand-calc:
//   new_base = 289984 (290000 rounded down to 64-tick boundary).
//   new_widx for BID 300000 = 300000 - 289984 = 10016, inside window (< 65536). ✓
//   new_widx for ASK 300001 = 300001 - 289984 = 10017, inside window. ✓
// ---------------------------------------------------------------------------

static void test_t18_rebase_preserves_levels() {
    Book b(267000ULL);

    assert(b.upsert_by_tick(side_t::BID, 300000U, 1000000ULL));
    assert(b.upsert_by_tick(side_t::ASK, 300001U, 2000000ULL));

    assert(b.best_bid() == 300000U);
    assert(b.best_ask() == 300001U);

    b.rebase(290000ULL);

    // After rebase, levels must still be present.
    assert(b.best_bid() == 300000U);
    assert(b.best_ask() == 300001U);
    assert(b.level_qty(side_t::BID, 300000U) == 1000000ULL);
    assert(b.level_qty(side_t::ASK, 300001U) == 2000000ULL);

    // window_base() must be 290000 rounded to 64-tick boundary.
    // 290000 & ~63 = 290000 & 0xFFFFFFC0 = 289984.
    assert(b.window_base() == 289984ULL);
}

// ---------------------------------------------------------------------------
// T19 — apply_snapshot with null pointers returns 0
// ---------------------------------------------------------------------------

static void test_t19_apply_snapshot_null_returns_zero() {
    Book b(267000ULL);
    b.reset(267000ULL);

    uint32_t r1 = b.apply_snapshot(side_t::BID, nullptr, nullptr, 5U);
    uint32_t r2 = b.apply_snapshot(side_t::ASK, nullptr, nullptr, 5U);
    assert(r1 == 0U);
    assert(r2 == 0U);
}

// ---------------------------------------------------------------------------
// T20 — Double-delete is a safe no-op
// Delete the same level twice. Second delete must return true (upsert_by_tick
// accepts qty=0 for an already-cleared slot), and invariants must hold.
// ---------------------------------------------------------------------------

static void test_t20_double_delete_is_safe() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::BID, 300000U, 1000000ULL));
    o.upsert(side_t::BID, 300000U, 1000000ULL);
    CHECK(b, o);

    // First delete.
    assert(b.upsert_by_tick(side_t::BID, 300000U, 0ULL));
    o.upsert(side_t::BID, 300000U, 0ULL);
    CHECK(b, o);

    // Second delete of the same tick — must not corrupt.
    assert(b.upsert_by_tick(side_t::BID, 300000U, 0ULL));
    // Oracle already removed it; calling upsert with qty=0 on absent tick is a no-op.
    CHECK(b, o);

    assert(b.best_bid() == TICK_INVALID);
    assert(b.level_qty(side_t::BID, 300000U) == 0ULL);
}

// ---------------------------------------------------------------------------
// T21 — move constructor transfers ownership
// ---------------------------------------------------------------------------

static void test_t21_move_constructor() {
    Book b(267000ULL);
    assert(b.upsert_by_tick(side_t::BID, 300000U, 1000000ULL));
    assert(b.upsert_by_tick(side_t::ASK, 300001U, 2000000ULL));

    Book moved(static_cast<Book&&>(b));

    assert(moved.best_bid() == 300000U);
    assert(moved.best_ask() == 300001U);
    assert(moved.level_qty(side_t::BID, 300000U) == 1000000ULL);
    assert(moved.level_qty(side_t::ASK, 300001U) == 2000000ULL);
}

// ---------------------------------------------------------------------------
// T22 — window_base() returns the base tick set at construction / reset
// ---------------------------------------------------------------------------

static void test_t22_window_base() {
    Book b(267000ULL);
    assert(b.window_base() == 267000ULL);

    b.reset(300000ULL);
    assert(b.window_base() == 300000ULL);
}

// ---------------------------------------------------------------------------
// T23 — BID and ASK at the same absolute tick (independent sides)
// The implementation uses separate book_side_t arrays per side; the same
// tick can exist simultaneously on both sides without corruption.
// (This is an unusual but valid state for the data structure — the caller
// is responsible for normal spread invariants; the book itself must not corrupt.)
// ---------------------------------------------------------------------------

static void test_t23_same_tick_both_sides() {
    Book   b(267000ULL);
    Oracle o;

    assert(b.upsert_by_tick(side_t::BID, 300000U, 1000000ULL));
    o.upsert(side_t::BID, 300000U, 1000000ULL);
    assert(b.upsert_by_tick(side_t::ASK, 300000U, 2000000ULL));
    o.upsert(side_t::ASK, 300000U, 2000000ULL);
    CHECK(b, o);

    assert(b.level_qty(side_t::BID, 300000U) == 1000000ULL);
    assert(b.level_qty(side_t::ASK, 300000U) == 2000000ULL);
    assert(b.best_bid() == 300000U);
    assert(b.best_ask() == 300000U);
}

// ---------------------------------------------------------------------------
// T24 — Window edge ticks (boundary values)
// base_tick = 267000.
// Lowest valid tick:  267000 (widx = 0).
// Highest valid tick: 267000 + 65535 = 332535 (widx = 65535).
// One below:          266999 → rejected (underflow distance).
// One above:          332536 → rejected (distance 65536 > WINDOW_MASK).
// ---------------------------------------------------------------------------

static void test_t24_window_edge_boundary_values() {
    Book   b(267000ULL);
    Oracle o;

    // Lowest valid tick.
    bool ok_low = b.upsert_by_tick(side_t::BID, 267000U, 100000000ULL);
    assert(ok_low == true);
    o.upsert(side_t::BID, 267000U, 100000000ULL);
    CHECK(b, o);
    assert(b.level_qty(side_t::BID, 267000U) == 100000000ULL);

    // Highest valid tick.
    bool ok_high = b.upsert_by_tick(side_t::ASK, 332535U, 200000000ULL);
    assert(ok_high == true);
    o.upsert(side_t::ASK, 332535U, 200000000ULL);
    CHECK(b, o);
    assert(b.level_qty(side_t::ASK, 332535U) == 200000000ULL);

    // One below base: rejected.
    bool ok_below = b.upsert_by_tick(side_t::BID, 266999U, 100000000ULL);
    assert(ok_below == false);
    CHECK(b, o);

    // One above max: rejected.
    bool ok_above = b.upsert_by_tick(side_t::ASK, 332536U, 100000000ULL);
    assert(ok_above == false);
    CHECK(b, o);
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

#define RUN(name) do { \
    test_##name(); \
    printf("PASS: " #name "\n"); \
} while (0)

int main() {
    printf("ETH/USDT L2 Orderbook — Correctness Test Suite\n");
    printf("------------------------------------------------\n");

    RUN(t1_layout_sizes);
    RUN(t2_empty_book);
    RUN(t3_single_upsert_bid);
    RUN(t4_single_upsert_ask);
    RUN(t5_multiple_bids_best_tracks_highest);
    RUN(t6_multiple_asks_best_tracks_lowest);
    RUN(t7_update_existing_level);
    RUN(t8_delete_non_best_level);
    RUN(t9_delete_best_level_fallback);
    RUN(t10_delete_sole_level_side_becomes_empty);
    RUN(t11_interleaved_bid_ask_spread);
    RUN(t12_reset_clears_all_state);
    RUN(t13_tick_outside_window_rejected);
    RUN(t14_needs_rebase);
    RUN(t14b_needs_rebase_ask_high_edge);
    RUN(t15_stress_5000_bid_5000_ask);
    RUN(t16_parse_price_via_upsert);
    RUN(t17_apply_snapshot);
    RUN(t18_rebase_preserves_levels);
    RUN(t19_apply_snapshot_null_returns_zero);
    RUN(t20_double_delete_is_safe);
    RUN(t21_move_constructor);
    RUN(t22_window_base);
    RUN(t23_same_tick_both_sides);
    RUN(t24_window_edge_boundary_values);

    printf("\n------------------------------------------------\n");
    printf("All %d tests passed.\n", 24);
    return 0;
}
