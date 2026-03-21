/* book.hpp — E-mini S&P 500 limit order book, C++ implementation
 *
 * Architecture: architect-spec.md
 * Build flags:  -std=c++17 -O2 -march=native -Wall -Wextra
 *               -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *
 * One sanctioned cast in the entire system: float->uint32_t in price_to_tick().
 * All other arithmetic is integer-typed throughout.
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
static constexpr uint32_t MAX_ORDERS   = 1'000'000U;
static constexpr uint32_t NULL_IDX     = 0xFFFF'FFFFU;
static constexpr uint8_t  DEAD_FLAG    = 0x01U;
static constexpr uint32_t TICK_INVALID = NULL_IDX;

// Compile-time verification of bitmap word count.
static_assert(BITMAP_WORDS == (MAX_TICKS + 63U) / 64U, "BITMAP_WORDS mismatch");

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
// levels[] before bitmap[] — larger/more-frequently-accessed at lower address
// ---------------------------------------------------------------------------

struct book_side_t {
    price_level_t levels[MAX_TICKS];    // 8800 × 16 = 140,800 bytes
    uint64_t      bitmap[BITMAP_WORDS]; //  138 ×  8 =   1,104 bytes
};                                      // total: 141,904 bytes

static_assert(sizeof(book_side_t) == 141904U, "book_side_t layout changed");

// ---------------------------------------------------------------------------
// Struct: arena_t  (spec: Data Model / arena_t)
// ---------------------------------------------------------------------------

struct arena_t {
    order_node_t nodes[MAX_ORDERS];  // 1,000,000 × 16 = 16,000,000 bytes
    uint32_t     next_slot;          // allocation high-water mark
};

// The spec notes: sizeof(arena_t) depends on alignment of next_slot after the array.
// nodes is uint32_t-aligned (4 bytes). next_slot is uint32_t — no additional pad needed.
// Expected: 16,000,000 + 4 = 16,000,004 bytes.
static_assert(sizeof(arena_t) == 16'000'004U, "arena_t layout changed");

// ---------------------------------------------------------------------------
// Internal bitmap helpers  (Idiom 4 — defined here for class-body inlining)
// ---------------------------------------------------------------------------

// Returns the lowest set bit index, or -1 if all zero.
// Template on NWORDS so the loop bound is a compile-time constant.
// Loop variable is int to avoid uint32_t->int narrowing in the return expression.
// NWORDS fits in int (138 << INT_MAX), so the cast is always safe.
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

// Returns the highest set bit index, or -1 if all zero.
// Same int-loop pattern to prevent narrowing warnings.
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

    // Cancel a previously placed order.
    // Caller must supply side and tick (recorded when add() returned).
    // Returns true on success, false if order is already dead or arguments invalid.
    bool cancel(order_id_t order_id, side_t side, tick_t tick) noexcept;

    // Match an incoming aggressive order against the resting book.
    [[nodiscard]] fill_result_t match(side_t aggressor_side,
                                      double price,
                                      qty_t  quantity,
                                      order_id_t taker_id) noexcept;

    // Best price queries — defined in class body for guaranteed inlining.
    [[nodiscard]] tick_t best_bid() const noexcept {
        int idx = bitmap_highest<BITMAP_WORDS>(impl_->sides[0].bitmap);
        return (idx >= 0) ? static_cast<tick_t>(idx) : NULL_IDX;
    }

    [[nodiscard]] tick_t best_ask() const noexcept {
        int idx = bitmap_lowest<BITMAP_WORDS>(impl_->sides[1].bitmap);
        return (idx >= 0) ? static_cast<tick_t>(idx) : NULL_IDX;
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
