/* book.hpp — E-mini S&P 500 limit order book, C++ implementation
 *
 * Architecture: architect-spec.md
 * Build flags:  -std=c++17 -O2 -march=native -Wall -Wextra
 *               -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *
 * One sanctioned cast in the entire system: float->uint32_t in price_to_tick().
 * All other arithmetic is integer-typed throughout.
 *
 * Combined best-bid/ask strategy:
 *   Fast path  — book_side_t::best_tick   : single field load, no scan
 *   Fallback   — book_side_t::summary[3]  : 2-level hierarchical bitmap
 *                (2 TZCNT/LZCNT instead of up to 138-word flat scan)
 *
 * The flat 138-word scan (bitmap_lowest / bitmap_highest) is kept for the
 * invariant checker but is NEVER called on the hot path.
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <array>
#include <new>
#include <exception>

// ---------------------------------------------------------------------------
// Namespace
// ---------------------------------------------------------------------------

namespace es::book {

// ---------------------------------------------------------------------------
// Fundamental constants (static constexpr per spec, Data Model section)
// ---------------------------------------------------------------------------

static constexpr uint32_t MAX_TICKS    = 8800U;
static constexpr uint32_t BITMAP_WORDS = 138U;   // ceil(8800 / 64)
// Number of summary words: ceil(BITMAP_WORDS / 64).
// Each summary bit covers 64 flat-bitmap words.
// 138 bitmap words → ceil(138/64) = 3 summary words.
static constexpr uint32_t SUMMARY_WORDS = 3U;
static constexpr uint32_t MAX_ORDERS   = 1'000'000U;
static constexpr uint32_t NULL_IDX     = 0xFFFF'FFFFU;
static constexpr uint8_t  DEAD_FLAG    = 0x01U;
static constexpr uint32_t TICK_INVALID = NULL_IDX;

// Compile-time verification of bitmap word count.
static_assert(BITMAP_WORDS == (MAX_TICKS + 63U) / 64U, "BITMAP_WORDS mismatch");
static_assert(SUMMARY_WORDS == (BITMAP_WORDS + 63U) / 64U, "SUMMARY_WORDS mismatch");

// ---------------------------------------------------------------------------
// Primary type aliases (spec: Primary Types)
// ---------------------------------------------------------------------------

using tick_t     = uint32_t;
using qty_t      = uint32_t;
using order_id_t = uint32_t;
using slot_idx_t = uint32_t;

// enum class side_t prevents int/side_t confusion (spec: Primary Types, C++ note)
enum class side_t : uint8_t { BID = 0, ASK = 1 };

// ---------------------------------------------------------------------------
// Struct: order_node_t  (spec: Data Model / order_node_t)
// Layout: order_id(0) quantity(4) next_idx(8) flags(12) _pad[3](13)
// ---------------------------------------------------------------------------

struct order_node_t {
    uint32_t order_id;   // offset  0 — equals slot index by construction
    uint32_t quantity;   // offset  4 — remaining quantity
    uint32_t next_idx;   // offset  8 — next node in FIFO, NULL_IDX if tail
    uint8_t  flags;      // offset 12 — DEAD_FLAG when cancelled or fully filled
    uint8_t  _pad[3];    // offset 13 — explicit pad to 16 bytes
};                       // total: 16 bytes

static_assert(sizeof(order_node_t)  == 16, "order_node_t layout changed");
static_assert(alignof(order_node_t) ==  4, "order_node_t alignment changed");

// ---------------------------------------------------------------------------
// Struct: price_level_t  (spec: Data Model / price_level_t)
// Layout: head_idx(0) tail_idx(4) count(8) total_qty(12)
// ---------------------------------------------------------------------------

struct price_level_t {
    uint32_t head_idx;   // offset  0 — head of FIFO queue, NULL_IDX if empty
    uint32_t tail_idx;   // offset  4 — tail of FIFO queue, NULL_IDX if empty
    uint32_t count;      // offset  8 — live order count at this level
    uint32_t total_qty;  // offset 12 — aggregate quantity at this level
};                       // total: 16 bytes

static_assert(sizeof(price_level_t)  == 16, "price_level_t layout changed");
static_assert(alignof(price_level_t) ==  4, "price_level_t alignment changed");

// ---------------------------------------------------------------------------
// Struct: fill_t  (spec: Data Model / fill_t)
// ---------------------------------------------------------------------------

struct fill_t {
    order_id_t maker_order_id;   // 4 B — resting order that was matched
    order_id_t taker_order_id;   // 4 B — aggressive order that caused the match
    tick_t     price_tick;       // 4 B — price at which the fill occurred
    qty_t      filled_qty;       // 4 B — quantity exchanged
};                               // total: 16 bytes

static_assert(sizeof(fill_t) == 16, "fill_t layout changed");

// ---------------------------------------------------------------------------
// Struct: fill_result_t  (spec: Data Model / fill_result_t)
// ---------------------------------------------------------------------------

struct fill_result_t {
    fill_t   fills[64];      // maximum fills per order (spec-defined bound)
    uint32_t fill_count;     // number of valid entries in fills[]
    qty_t    remaining_qty;  // quantity not yet matched; 0 if fully filled
};

// ---------------------------------------------------------------------------
// Struct: book_side_t  (spec: Data Model / book_side_t)
//
// Combined best-bid/ask data layout:
//   levels[]    — price-level FIFO queues; 8800 × 16 = 140,800 bytes
//   bitmap[]    — flat 138-word occupancy bitmap; 138 × 8 = 1,104 bytes
//   best_tick   — cached best price for this side (TICK_INVALID if empty)
//                 FAST PATH: single field load, no scan
//   _pad         — 4 bytes explicit pad to align summary[] to 8 bytes
//   summary[]   — 3-word second-level bitmap over bitmap[]
//                 FALLBACK PATH: bit b set ↔ at least one of bitmap[b*64 .. b*64+63] is non-zero
//
// Field order is deliberate:
//   levels[] at offset 0 — most frequently accessed, lowest address
//   bitmap[] at offset 140800 — set/cleared on every add/cancel/fill
//   best_tick at offset 141904 — hot query field, just past bitmap
//   _pad at offset 141908 — keeps summary[] 8-byte aligned
//   summary[] at offset 141912 — fallback only; rarely loaded
//
// Total: 141,936 bytes
// ---------------------------------------------------------------------------

struct book_side_t {
    price_level_t levels[MAX_TICKS];      // offset      0 — 140,800 bytes
    uint64_t      bitmap[BITMAP_WORDS];   // offset 140800 —   1,104 bytes
    tick_t        best_tick;              // offset 141904 —       4 bytes (fast path)
    uint32_t      _side_pad;              // offset 141908 —       4 bytes (align summary[])
    uint64_t      summary[SUMMARY_WORDS]; // offset 141912 —      24 bytes (fallback path)
};                                        // total:           141,936 bytes

static_assert(sizeof(book_side_t) == 141936U, "book_side_t layout changed");

// ---------------------------------------------------------------------------
// Struct: arena_t  (spec: Data Model / arena_t)
// ---------------------------------------------------------------------------

struct arena_t {
    order_node_t nodes[MAX_ORDERS];  // 1,000,000 × 16 = 16,000,000 bytes
    uint32_t     next_slot;          // allocation high-water mark
};

static_assert(sizeof(arena_t) == 16'000'004U, "arena_t layout changed");

// ---------------------------------------------------------------------------
// Flat bitmap helpers (Idiom 4 — kept for the invariant checker, NOT hot path)
//
// These perform the original 138-word linear scan.  The hot path now uses
// book_side_t::best_tick (fast path) or the hierarchical helpers below
// (fallback path).  These flat helpers remain correct and are used only by
// the test suite's invariant checker.
// ---------------------------------------------------------------------------

template<uint32_t NWORDS>
inline int bitmap_lowest(const uint64_t* bits) noexcept {
    static_assert(NWORDS <= 0x7FFF'FFFFU, "NWORDS exceeds int range");
    constexpr int NWORDS_I = static_cast<int>(NWORDS);
    for (int w = 0; w < NWORDS_I; ++w) {
        if (bits[static_cast<uint32_t>(w)]) {
            return w * 64 + __builtin_ctzll(bits[static_cast<uint32_t>(w)]);
        }
    }
    return -1;
}

template<uint32_t NWORDS>
inline int bitmap_highest(const uint64_t* bits) noexcept {
    static_assert(NWORDS <= 0x7FFF'FFFFU, "NWORDS exceeds int range");
    constexpr int NWORDS_I = static_cast<int>(NWORDS);
    for (int w = NWORDS_I - 1; w >= 0; --w) {
        if (bits[static_cast<uint32_t>(w)]) {
            return w * 64 + 63 - __builtin_clzll(bits[static_cast<uint32_t>(w)]);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Hierarchical bitmap helpers — FALLBACK PATH (called when best level drains)
//
// Two-level structure:
//   Level 2 (top):  summary[SUMMARY_WORDS] — one bit per flat-bitmap word
//   Level 1 (flat): bitmap[BITMAP_WORDS]   — one bit per tick
//
// bitmap_lowest_h  → used by best_ask fallback (ASK side: lowest tick wins)
// bitmap_highest_h → used by best_bid fallback (BID side: highest tick wins)
//
// Cost: 2 TZCNT/LZCNT + 3 loads — replaces up to 138-word flat scan.
//
// Defined inline in the header so the compiler inlines them unconditionally
// regardless of LTO cold-call heuristics (Pitfall 8, Idiom 6).
// ---------------------------------------------------------------------------

// Returns the lowest set tick index using the summary, or -1 if empty.
// Algorithm:
//   1. Find the lowest set bit in summary[]  → summary word s_w
//   2. Find the lowest set bit in bitmap[s_w * 64 .. min(s_w*64+63, BITMAP_WORDS-1)]
//   3. TZCNT within that flat-bitmap word gives the tick offset.
inline int bitmap_lowest_h(const book_side_t& side) noexcept {
    // Step 1: lowest non-empty group (TZCNT on summary)
    for (uint32_t sw = 0U; sw < SUMMARY_WORDS; ++sw) {
        if (side.summary[sw] == 0U) {
            continue;
        }
        // summary[sw] is non-zero: bit b within it says bitmap[sw*64 + b] is non-zero.
        uint32_t sb         = static_cast<uint32_t>(__builtin_ctzll(side.summary[sw]));
        uint32_t bmap_word  = sw * 64U + sb;
        // Guard: bmap_word must be in range (summary may have stale bits if
        // BITMAP_WORDS is not a multiple of 64, but our updates are synchronous
        // so this guard is defensive only).
        if (bmap_word >= BITMAP_WORDS) {
            return -1;
        }
        // Step 2: TZCNT within the flat-bitmap word gives the bit offset.
        uint64_t word = side.bitmap[bmap_word];
        if (word == 0U) {
            // Summary bit was set but the flat-bitmap word is zero — should not
            // happen with synchronous updates; skip and keep scanning.
            continue;
        }
        // Tick = bmap_word * 64 + bit_within_word
        int tick_raw = static_cast<int>(bmap_word) * 64
                       + __builtin_ctzll(word);
        if (tick_raw >= static_cast<int>(MAX_TICKS)) {
            return -1;
        }
        return tick_raw;
    }
    return -1;
}

// Returns the highest set tick index using the summary, or -1 if empty.
// Mirror of bitmap_lowest_h: scans summary from high to low.
inline int bitmap_highest_h(const book_side_t& side) noexcept {
    // Step 1: highest non-empty group (LZCNT on summary)
    for (uint32_t sw = SUMMARY_WORDS; sw-- > 0U; ) {
        if (side.summary[sw] == 0U) {
            continue;
        }
        // Highest set bit within this summary word (63 - LZCNT).
        uint32_t sb        = 63U - static_cast<uint32_t>(__builtin_clzll(side.summary[sw]));
        uint32_t bmap_word = sw * 64U + sb;
        if (bmap_word >= BITMAP_WORDS) {
            // sb pointed past the valid range — summary inconsistency; treat as empty.
            return -1;
        }
        uint64_t word = side.bitmap[bmap_word];
        if (word == 0U) {
            // Summary bit set but bitmap word is zero — skip and keep scanning.
            continue;
        }
        int tick_raw = static_cast<int>(bmap_word) * 64
                       + 63 - __builtin_clzll(word);
        if (tick_raw >= static_cast<int>(MAX_TICKS)) {
            return -1;
        }
        return tick_raw;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// API boundary: price_to_tick  (spec: API Boundary / Price conversion)
// The ONE sanctioned cast in the entire system.
// ---------------------------------------------------------------------------

[[nodiscard]] inline tick_t price_to_tick(double price, double base_price) noexcept {
    if (!std::isfinite(price) || !std::isfinite(base_price)) {
        return TICK_INVALID;
    }
    double diff = price - base_price;
    if (diff < 0.0) {
        return TICK_INVALID;
    }
    double scaled = diff * 4.0 + 0.5;
    if (scaled >= static_cast<double>(MAX_TICKS)) {
        return TICK_INVALID;
    }
    // Sanctioned cast: boundary conversion only, not on the hot path.
    tick_t tick = static_cast<tick_t>(scaled);
    if (tick >= MAX_TICKS) {
        return TICK_INVALID;
    }
    return tick;
}

// ---------------------------------------------------------------------------
// Book class (spec: Module Boundaries / Module 5: Book)
// Wraps sides[2] and arena with RAII semantics.
// Hot small functions defined in class body for guaranteed inlining (Idiom 6).
// ---------------------------------------------------------------------------

class Book {
public:
    // Construction / destruction / move semantics
    explicit Book(double base_price);
    ~Book() noexcept;

    Book(const Book&)            = delete;
    Book& operator=(const Book&) = delete;

    Book(Book&& other) noexcept;
    Book& operator=(Book&&)      = delete;

    // -----------------------------------------------------------------------
    // Public API (spec: Interface Specification)
    // -----------------------------------------------------------------------

    // Place a resting order. Returns the assigned order_id, or NULL_IDX on error.
    [[nodiscard]] order_id_t add(side_t side, double price, qty_t quantity) noexcept;

    // Tick-direct add: bypasses price_to_tick(). Used by the data-driven benchmark
    // when the tick was pre-converted by the CSV loader. Not part of the public API.
    [[nodiscard]] order_id_t add_by_tick(side_t side, tick_t tick, qty_t quantity) noexcept;

    // Cancel a previously placed order.
    // Caller must supply side and tick (recorded when add() returned).
    // Returns true on success, false if order is already dead or arguments invalid.
    bool cancel(order_id_t order_id, side_t side, tick_t tick) noexcept;

    // Match an incoming aggressive order against the resting book.
    [[nodiscard]] fill_result_t match(side_t aggressor_side,
                                      double price,
                                      qty_t  quantity,
                                      order_id_t taker_id) noexcept;

    // Tick-direct match: bypasses price_to_tick(). Used by the data-driven
    // benchmark when the tick was pre-converted by the CSV loader.
    // Not part of the public API.
    [[nodiscard]] fill_result_t match_by_tick(side_t     aggressor_side,
                                              tick_t     tick,
                                              qty_t      quantity,
                                              order_id_t taker_id) noexcept;

    // -----------------------------------------------------------------------
    // Best price queries — FAST PATH (Idiom 6: class-body inline)
    //
    // Returns best_tick directly — a single field load.  No bitmap scan.
    // best_tick is maintained synchronously on every add/cancel/match.
    // Returns TICK_INVALID (== NULL_IDX) when the side is empty.
    // -----------------------------------------------------------------------

    [[nodiscard]] tick_t best_bid() const noexcept {
        // Fast path: one field load.  best_tick is always current.
        return impl_->sides[0].best_tick;
    }

    [[nodiscard]] tick_t best_ask() const noexcept {
        // Fast path: one field load.  best_tick is always current.
        return impl_->sides[1].best_tick;
    }

    // Level inspection (not on hot path)
    [[nodiscard]] uint32_t level_count(side_t side, tick_t tick) const noexcept;
    [[nodiscard]] qty_t    level_qty(side_t side, tick_t tick)   const noexcept;

    // Session boundary reset — not on hot path; uses memset.
    void reset() noexcept;

    // -----------------------------------------------------------------------
    // Internal data layout (exposed for the test invariant checker per spec)
    // -----------------------------------------------------------------------

    struct Impl {
        book_side_t sides[2];
        arena_t     arena;
        double      base_price;
    };

    // The checker needs raw field access (spec: Test Suite Contract / Oracle rule 5).
    // Providing const access is sufficient and avoids breaking encapsulation.
    const Impl& impl() const noexcept { return *impl_; }

private:
    Impl* impl_;  // heap-allocated; ~15.5 MB, too large for stack

    // Side-indexed helpers (inline; no virtual dispatch)
    [[nodiscard]] book_side_t&       side(side_t s) noexcept {
        return impl_->sides[static_cast<uint8_t>(s)];
    }
    [[nodiscard]] const book_side_t& side(side_t s) const noexcept {
        return impl_->sides[static_cast<uint8_t>(s)];
    }

    static constexpr uint8_t side_index(side_t s) noexcept {
        return static_cast<uint8_t>(s);
    }
};

// Individual member sizes are verified above; the Impl total depends on
// compiler-inserted padding between arena_t (4-byte aligned) and double
// base_price (8-byte aligned). The per-field static_asserts above are
// the authoritative layout checks — the Impl size is not asserted here
// to avoid a platform-sensitive assertion the implementor cannot verify
// without compiling. agentTest must print sizeof(Book::Impl) in its first test.

} // namespace es::book
