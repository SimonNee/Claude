/* book.hpp — ETH/USDT Binance spot L2 orderbook, C++ implementation
 *
 * Architecture: ../architect-spec.md
 * Build flags:  -std=c++17 -O2 -march=native -Wall -Wextra
 *               -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *
 * No float anywhere inside the book. Prices are parsed from strings as pure
 * integers. No arena, no order_node_t, no FIFO queues, no matching engine.
 *
 * Best-tick strategy:
 *   Fast path  — book_side_t::best_tick  : single field load, no scan
 *   Fallback   — two-level hierarchical bitmap (SUMMARY_WORDS TZCNT/LZCNT)
 *                called only when the best level is deleted
 *
 * Sanctioned casts (the only two permitted in the entire system):
 *   1. static_cast<tick_t>(absolute_tick) inside parse_price() — uint64_t →
 *      uint32_t after overflow bounds check.
 *   2. static_cast<window_idx_t>(...) inside window_index() — uint64_t →
 *      uint32_t; the WINDOW_MASK guarantees the value fits.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <new>

// ---------------------------------------------------------------------------
// Namespace
// ---------------------------------------------------------------------------

namespace eth::book {

// ---------------------------------------------------------------------------
// Fundamental constants (spec: Data Model / Fundamental Constants)
// ---------------------------------------------------------------------------

static constexpr uint32_t WINDOW_SIZE    = 65536U;
static constexpr uint32_t WINDOW_MASK    = 65535U;
static constexpr uint32_t BITMAP_WORDS   = 1024U;
static constexpr uint32_t SUMMARY_WORDS  = 16U;
static constexpr uint32_t REBASE_MARGIN  = 8192U;
static constexpr uint64_t QTY_SCALE      = 100000000ULL;
static constexpr uint32_t TICK_INVALID   = 0xFFFFFFFFU;
static constexpr uint64_t NULL_BASE_TICK = 0xFFFFFFFFFFFFFFFFULL;
static constexpr uint64_t QTY_INVALID    = UINT64_MAX;

// Compile-time verification of constant relationships (spec: Static Assertions)
static_assert(WINDOW_SIZE  == 65536U,                      "WINDOW_SIZE mismatch");
static_assert(WINDOW_MASK  == WINDOW_SIZE - 1U,            "WINDOW_MASK mismatch");
static_assert(BITMAP_WORDS == WINDOW_SIZE / 64U,           "BITMAP_WORDS mismatch");
static_assert(SUMMARY_WORDS == (BITMAP_WORDS + 63U) / 64U, "SUMMARY_WORDS mismatch");
static_assert(REBASE_MARGIN == WINDOW_SIZE / 8U,           "REBASE_MARGIN mismatch");

// ---------------------------------------------------------------------------
// Primary type aliases (spec: Data Model / Primary Types)
// ---------------------------------------------------------------------------

using tick_t       = uint32_t;   // absolute integer price index; 1 tick = $0.01
using window_idx_t = uint32_t;   // window-relative slot index: (tick - base) & WINDOW_MASK
using qty_t        = uint64_t;   // scaled integer; scale 10^8; 0 = absent level
using abs_tick_t   = uint64_t;   // absolute tick for window base; uint64_t avoids overflow

// enum class prevents int/side_t confusion (spec: Primary Types, C++ note)
enum class side_t : uint8_t { BID = 0, ASK = 1 };

// ---------------------------------------------------------------------------
// Struct: price_level_t  (spec: Data Model / price_level_t)
// Layout: total_qty(0)
// ---------------------------------------------------------------------------

struct price_level_t {
    qty_t total_qty;   // offset 0 — scaled qty (units of 10^-8 ETH); 0 = empty slot
};                     // total: 8 bytes

static_assert(sizeof(price_level_t)  == 8U, "price_level_t layout changed");
static_assert(alignof(price_level_t) == 8U, "price_level_t alignment changed");

// ---------------------------------------------------------------------------
// Struct: book_side_t  (spec: Data Model / book_side_t)
//
// Field layout (deliberate — matches spec offset table):
//   levels[]   at offset      0 — 65,536 × 8 = 524,288 bytes (most accessed)
//   bitmap[]   at offset 524288 —  1,024 × 8 =   8,192 bytes (set/cleared per upsert)
//   best_tick  at offset 532480 —              4 bytes (fast-path query field)
//   _side_pad  at offset 532484 —              4 bytes (keeps summary[] 8-byte aligned)
//   summary[]  at offset 532488 —     16 × 8 =   128 bytes (fallback only)
//
// Total: 532,616 bytes
// ---------------------------------------------------------------------------

struct book_side_t {
    price_level_t levels[WINDOW_SIZE];    // offset      0 — 524,288 bytes
    uint64_t      bitmap[BITMAP_WORDS];   // offset 524288 —   8,192 bytes
    tick_t        best_tick;              // offset 532480 —       4 bytes (fast path)
    uint32_t      _side_pad;              // offset 532484 —       4 bytes (align summary[])
    uint64_t      summary[SUMMARY_WORDS]; // offset 532488 —     128 bytes (fallback path)
};                                        // total:             532,616 bytes

static_assert(sizeof(book_side_t)  == 532616U, "book_side_t layout changed");
static_assert(alignof(book_side_t) ==      8U, "book_side_t alignment changed");

// ---------------------------------------------------------------------------
// window_index — convert absolute tick to window-relative slot index
//
// Called only inside upsert_impl<IsBid>. The sanctioned cast from uint64_t to
// uint32_t is safe because the WINDOW_MASK guarantees the result fits in 16 bits.
// [[nodiscard]] enforced per spec note 11.
// ---------------------------------------------------------------------------

[[nodiscard]] inline window_idx_t
window_index(tick_t absolute_tick, uint64_t window_base_tick) noexcept {
    // Sanctioned cast: uint64_t → uint32_t; value is <= WINDOW_MASK = 65535 < UINT32_MAX.
    return static_cast<window_idx_t>(
        (static_cast<uint64_t>(absolute_tick) - window_base_tick) & WINDOW_MASK
    );
}

// ---------------------------------------------------------------------------
// Book class (spec: Module Boundaries / Module 3: Book)
//
// Wraps Impl (heap-allocated, ~1.04 MB) with RAII semantics.
// Hot small functions defined in class body for guaranteed inlining (Idiom 6).
// No virtual dispatch, no STL containers, no float.
// ---------------------------------------------------------------------------

class Book {
public:
    // -----------------------------------------------------------------------
    // Construction / destruction / move
    // -----------------------------------------------------------------------

    // Precondition: initial_base_tick is a valid absolute tick (or NULL_BASE_TICK
    // if the window base is not yet known — call reset() before first upsert).
    // With -fno-exceptions, new calls std::terminate on allocation failure.
    explicit Book(uint64_t initial_base_tick);
    ~Book() noexcept;

    Book(const Book&)            = delete;
    Book& operator=(const Book&) = delete;

    Book(Book&& other) noexcept;
    Book& operator=(Book&&)      = delete;

    // -----------------------------------------------------------------------
    // Primary hot-path API
    // -----------------------------------------------------------------------

    // upsert: set level qty if qty > 0; delete level if qty == 0.
    // price_str and qty_str are Binance decimal strings.
    // Returns false if parsing fails or the tick is out of the current window.
    [[nodiscard]] bool upsert(side_t      side,
                              const char* price_str, size_t price_len,
                              const char* qty_str,   size_t qty_len) noexcept;

    // upsert_by_tick: bypass parsing — benchmark hook.
    // Precondition: absolute_tick != TICK_INVALID, qty != QTY_INVALID.
    [[nodiscard]] bool upsert_by_tick(side_t side,
                                      tick_t absolute_tick,
                                      qty_t  qty) noexcept;

    // -----------------------------------------------------------------------
    // Best price queries — FAST PATH (Idiom 6: class-body inline)
    //
    // Returns best_tick directly — a single 4-byte field load.  No scan.
    // best_tick is maintained synchronously on every upsert.
    // Returns TICK_INVALID when the side is empty or the book is uninitialised.
    // -----------------------------------------------------------------------

    [[nodiscard]] tick_t best_bid() const noexcept {
        // Fast path: one field load.  best_tick is always current.
        return impl_->sides[0].best_tick;
    }

    [[nodiscard]] tick_t best_ask() const noexcept {
        // Fast path: one field load.  best_tick is always current.
        return impl_->sides[1].best_tick;
    }

    // -----------------------------------------------------------------------
    // Window management
    // -----------------------------------------------------------------------

    // needs_rebase: true if best bid or ask is within REBASE_MARGIN of window edge.
    // Call after each upsert; invoke rebase() when true (cold path).
    [[nodiscard]] bool needs_rebase() const noexcept;

    // rebase: cold-path window repositioning (Option A: temporary allocation).
    // new_base_tick is rounded down to the nearest 64-tick boundary.
    void rebase(uint64_t new_base_tick) noexcept;

    // reset: clear all state, set new window base. Called on snapshot re-sync.
    void reset(uint64_t new_base_tick) noexcept;

    // -----------------------------------------------------------------------
    // Snapshot apply
    // -----------------------------------------------------------------------

    // apply_snapshot: bulk-apply pre-parsed ticks and qtys to one side.
    // Caller must call reset() before apply_snapshot().
    // Returns the count of levels successfully applied.
    uint32_t apply_snapshot(side_t        side,
                            const tick_t* ticks,
                            const qty_t*  qtys,
                            uint32_t      count) noexcept;

    // -----------------------------------------------------------------------
    // Test / inspection helpers (not on hot path)
    // -----------------------------------------------------------------------

    [[nodiscard]] qty_t    level_qty(side_t side, tick_t absolute_tick) const noexcept;
    [[nodiscard]] uint64_t window_base() const noexcept;

    // -----------------------------------------------------------------------
    // Internal data layout (exposed for the invariant checker in test_book.cpp)
    // -----------------------------------------------------------------------

    struct Impl {
        book_side_t sides[2];          // sides[0] = BID, sides[1] = ASK
        uint64_t    window_base_tick;  // absolute tick at window slot 0; NULL_BASE_TICK if uninit
        uint64_t    _impl_pad;         // explicit pad; keeps sizeof(Impl) unambiguous
    };

    // The checker needs raw field access.  Const access is sufficient.
    const Impl& impl() const noexcept { return *impl_; }

private:
    Impl* impl_;   // heap-allocated; ~1.04 MB — too large for the stack

    // Side-indexed accessors (inline; no virtual dispatch)
    [[nodiscard]] book_side_t& side(side_t s) noexcept {
        return impl_->sides[static_cast<uint8_t>(s)];
    }
    [[nodiscard]] const book_side_t& side(side_t s) const noexcept {
        return impl_->sides[static_cast<uint8_t>(s)];
    }

    // Template dispatch: IsBid = true → BID (highest tick wins)
    //                    IsBid = false → ASK (lowest tick wins)
    // Removes the side-direction branch from the hot path entirely.
    template<bool IsBid>
    bool upsert_impl(tick_t absolute_tick, qty_t qty) noexcept;

    // Parser boundary functions — private static, no heap access, no float.
    [[nodiscard]] static tick_t parse_price(const char* s, size_t len) noexcept;
    [[nodiscard]] static qty_t  parse_qty  (const char* s, size_t len) noexcept;
};

// sizeof(Book::Impl) is asserted in book.cpp after the full Impl definition
// is visible to the compiler.  Expected value: 2 × 532,616 + 8 + 8 = 1,065,248.

} // namespace eth::book
