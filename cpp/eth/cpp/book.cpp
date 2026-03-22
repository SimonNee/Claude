/* book.cpp — ETH/USDT Binance spot L2 orderbook implementation
 *
 * Architecture: ../architect-spec.md
 * Build flags:  -std=c++17 -O2 -march=native -Wall -Wextra
 *               -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *
 * Module decomposition matches spec section "Module Boundaries":
 *   Module 1 — Price/Qty Parser  (parse_price / parse_qty, private static)
 *   Module 2 — Level Index       (internal.hpp :: level_set / level_clear /
 *                                  bitmap_set_bit / bitmap_clear_bit /
 *                                  bitmap_lowest_h / bitmap_highest_h)
 *   Module 3 — Book              (this file)
 *
 * No arena, no order_node_t, no matching engine.
 * No float anywhere inside the book — prices are pure integers throughout.
 *
 * Sanctioned casts:
 *   1. static_cast<tick_t>(absolute_tick) in parse_price() — uint64_t → uint32_t
 *      after explicit overflow check.
 *   2. static_cast<window_idx_t>(...) in window_index() — uint64_t → uint32_t;
 *      WINDOW_MASK guarantees the value fits.
 *   3. static_cast<uint8_t>(s) when indexing sides[] by side_t — same pattern
 *      as ES reference implementation.
 *
 * Combined best-tick strategy:
 *
 *   UPSERT (qty > 0 — SET):
 *     1. Compute widx = window_index(absolute_tick, window_base_tick).
 *     2. Write levels[widx].total_qty = qty  (absolute overwrite).
 *     3. If level was empty: bitmap_set_bit (maintains bitmap + summary).
 *     4. Update best_tick with compare:
 *          BID: best_tick = max(best_tick, tick)   (TICK_INVALID treated as empty)
 *          ASK: best_tick = min(best_tick, tick)   (TICK_INVALID treated as empty)
 *
 *   UPSERT (qty == 0 — DELETE):
 *     1. Write levels[widx].total_qty = 0.
 *     2. bitmap_clear_bit (clears bitmap; clears summary if bitmap word reaches 0).
 *     3. If absolute_tick == best_tick: run hierarchical fallback scan for new best.
 *        If absolute_tick != best_tick: best_tick is unchanged (O(1)).
 *
 * ORDERING REQUIREMENT (spec §Implementation Notes / Ordering constraints):
 *   On delete, bitmap must be cleared BEFORE the fallback scan reads it.
 *   The fallback scan would return the just-deleted level if the bitmap is stale.
 */

#include "book.hpp"
#include "internal.hpp"

#include <cstring>
#include <new>

namespace eth::book {

using namespace eth::book::internal;

// ---------------------------------------------------------------------------
// sizeof(Book::Impl) verification (spec §Static Assertions)
//
// Computed: 2 × sizeof(book_side_t) + 2 × sizeof(uint64_t)
//         = 2 × 532,616 + 16
//         = 1,065,232 + 16
//         = 1,065,248
//
// 1,065,232 is a multiple of 8, so window_base_tick (uint64_t, 8-byte aligned)
// is placed immediately after sides[2] with no padding inserted.
// ---------------------------------------------------------------------------

static_assert(sizeof(Book::Impl) == 1065248U, "Book::Impl layout changed");

// ---------------------------------------------------------------------------
// parse_price — convert Binance price string to absolute tick (spec §API Boundary)
//
// Algorithm:
//   1. Scan to find decimal point.
//   2. Parse integer part (digits before '.') into uint64_t int_part.
//   3. Parse first two fractional digits (cents) into uint64_t frac_cents.
//      - If fewer than 2 digits, right-pad with zeros.
//      - Digits beyond position 2 are ignored.
//   4. absolute_tick = int_part * 100 + frac_cents.
//   5. Return static_cast<tick_t>(absolute_tick) after overflow check.
//
// No float.  No atof, strtod, sscanf.  Pure integer string parse.
//
// Returns TICK_INVALID on empty input, non-digit character, or overflow.
// ---------------------------------------------------------------------------

tick_t Book::parse_price(const char* s, size_t len) noexcept {
    if (len == 0U || s == nullptr) {
        return TICK_INVALID;
    }

    uint64_t int_part = 0U;
    size_t   i        = 0U;

    // Parse integer part (digits before decimal point or end of string).
    while (i < len && s[i] != '.') {
        uint8_t c = static_cast<uint8_t>(s[i]);
        if (c < static_cast<uint8_t>('0') || c > static_cast<uint8_t>('9')) {
            return TICK_INVALID;
        }
        // Overflow guard: int_part * 100 must fit in uint32_t after adding frac_cents.
        // Binance max price is $10,000,000 → int_part max = 10,000,000.
        // uint64_t can hold this without overflow in the accumulation step.
        // We check overflow below before the final cast.
        int_part = int_part * 10U + static_cast<uint64_t>(c - static_cast<uint8_t>('0'));
        ++i;
    }

    uint64_t frac_cents = 0U;

    if (i < len && s[i] == '.') {
        ++i;  // skip the decimal point

        // Read first fractional digit (tens of cents, e.g. '6' → 60).
        if (i < len) {
            uint8_t c = static_cast<uint8_t>(s[i]);
            if (c < static_cast<uint8_t>('0') || c > static_cast<uint8_t>('9')) {
                return TICK_INVALID;
            }
            frac_cents = static_cast<uint64_t>(c - static_cast<uint8_t>('0')) * 10U;
            ++i;
        }
        // else: no fractional digits after '.'; frac_cents stays 0.

        // Read second fractional digit (units of cents, e.g. '7' → 7).
        if (i < len && s[i] != '.') {
            uint8_t c = static_cast<uint8_t>(s[i]);
            if (c < static_cast<uint8_t>('0') || c > static_cast<uint8_t>('9')) {
                return TICK_INVALID;
            }
            frac_cents += static_cast<uint64_t>(c - static_cast<uint8_t>('0'));
            ++i;
        }
        // Ignore any remaining fractional digits beyond position 2 (spec note 9).
    }

    // absolute_tick = int_part * 100 + frac_cents
    uint64_t absolute_tick = int_part * 100U + frac_cents;

    // Overflow check: must fit in tick_t (uint32_t).
    // At max price $10,000,000: tick = 1,000,000,000 — fits uint32_t (max ~4.29B).
    if (absolute_tick > static_cast<uint64_t>(UINT32_MAX) - 1U) {
        return TICK_INVALID;
    }

    // Sanctioned cast 1: uint64_t → uint32_t after explicit bounds check.
    return static_cast<tick_t>(absolute_tick);
}

// ---------------------------------------------------------------------------
// parse_qty — convert Binance quantity string to scaled qty_t (spec §API Boundary)
//
// Algorithm:
//   1. Parse integer part → uint64_t int_part.
//   2. Parse fractional part → up to 8 digits; accumulate into uint64_t frac_part.
//      - If fewer than 8 digits, multiply frac_part by 10^(8 - digit_count).
//      - Ignore digits beyond position 8.
//   3. Return int_part * QTY_SCALE + frac_part.
//
// Returns QTY_INVALID (UINT64_MAX) on empty input, non-digit, or overflow.
// Returns 0 for "0.0" or "0.00000000" — the deletion sentinel in upsert context.
// ---------------------------------------------------------------------------

qty_t Book::parse_qty(const char* s, size_t len) noexcept {
    if (len == 0U || s == nullptr) {
        return QTY_INVALID;
    }

    uint64_t int_part = 0U;
    size_t   i        = 0U;

    // Parse integer part.
    while (i < len && s[i] != '.') {
        uint8_t c = static_cast<uint8_t>(s[i]);
        if (c < static_cast<uint8_t>('0') || c > static_cast<uint8_t>('9')) {
            return QTY_INVALID;
        }
        // Overflow check: int_part * QTY_SCALE must not overflow uint64_t.
        // uint64_t max = ~1.84e19; QTY_SCALE = 1e8.
        // int_part max before overflow: ~1.84e11 ETH.
        // Total ETH supply ~1.2e8; reasonable bound is int_part < 1e12.
        // Guard: if int_part > UINT64_MAX / QTY_SCALE, return QTY_INVALID.
        if (int_part > (UINT64_MAX / QTY_SCALE)) {
            return QTY_INVALID;
        }
        int_part = int_part * 10U + static_cast<uint64_t>(c - static_cast<uint8_t>('0'));
        ++i;
    }

    uint64_t frac_part   = 0U;
    uint32_t frac_digits = 0U;

    if (i < len && s[i] == '.') {
        ++i;  // skip decimal point
        while (i < len && frac_digits < 8U) {
            uint8_t c = static_cast<uint8_t>(s[i]);
            if (c < static_cast<uint8_t>('0') || c > static_cast<uint8_t>('9')) {
                return QTY_INVALID;
            }
            frac_part = frac_part * 10U + static_cast<uint64_t>(c - static_cast<uint8_t>('0'));
            ++frac_digits;
            ++i;
        }
        // Ignore remaining fractional digits beyond position 8.
        // Right-pad frac_part with zeros to reach 8 digits (multiply by remaining power of 10).
        while (frac_digits < 8U) {
            frac_part *= 10U;
            ++frac_digits;
        }
    }

    // Overflow check before final multiply: int_part * QTY_SCALE.
    if (int_part > (UINT64_MAX / QTY_SCALE)) {
        return QTY_INVALID;
    }
    uint64_t result = int_part * QTY_SCALE;

    // Check: result + frac_part must not overflow uint64_t.
    if (frac_part > UINT64_MAX - result) {
        return QTY_INVALID;
    }
    return result + frac_part;
}

// ---------------------------------------------------------------------------
// Book::Book  (spec §Interface Specification / constructor)
//
// Heap-allocates Impl (~1.04 MB). With -fno-exceptions, new calls
// std::terminate on allocation failure — callers must ensure sufficient memory.
// ---------------------------------------------------------------------------

Book::Book(uint64_t initial_base_tick) {
    impl_ = static_cast<Impl*>(::operator new(sizeof(Impl)));

    // Zero all levels, bitmaps, summaries, and pad fields.
    std::memset(impl_, 0, sizeof(Impl));

    // memset sets best_tick fields to 0, but TICK_INVALID = 0xFFFFFFFF.
    // Must explicitly set TICK_INVALID on both sides (spec §reset implementation note).
    impl_->sides[0].best_tick  = TICK_INVALID;
    impl_->sides[1].best_tick  = TICK_INVALID;
    impl_->window_base_tick    = initial_base_tick;
    // _impl_pad already zero from memset.
}

// ---------------------------------------------------------------------------
// Book::~Book
// ---------------------------------------------------------------------------

Book::~Book() noexcept {
    if (impl_) {
        ::operator delete(impl_);
        impl_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Book::Book (move constructor)
// ---------------------------------------------------------------------------

Book::Book(Book&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

// ---------------------------------------------------------------------------
// upsert_impl<IsBid> — core hot-path implementation
//
// IsBid = true  → BID side: sides[0]; best_tick = max(best_tick, tick)
// IsBid = false → ASK side: sides[1]; best_tick = min(best_tick, tick)
//
// The template parameter removes the side-direction branch from the hot path.
// Both instantiations are compiled; the public upsert() dispatches to the
// correct one via a single if/else at the API boundary.
//
// Ordering (spec §Ordering constraints within upsert_impl):
//   SET path:  write level → set bitmap → update best_tick
//   DELETE path: write level → clear bitmap → fallback scan (if best level deleted)
// ---------------------------------------------------------------------------

template<bool IsBid>
bool Book::upsert_impl(tick_t absolute_tick, qty_t qty) noexcept {
    constexpr uint8_t side_idx = IsBid ? 0U : 1U;
    book_side_t& sd = impl_->sides[side_idx];

    // Compute window-relative slot index.
    // Overflow/bounds check: if the absolute_tick is outside the current window,
    // the masked subtraction still produces a value in [0, WINDOW_SIZE), but it
    // may map to the wrong slot.  We reject ticks that are truly outside the
    // window by checking the raw (unmasked) distance (spec §upsert error conditions).
    uint64_t distance = static_cast<uint64_t>(absolute_tick) - impl_->window_base_tick;
    if (distance > static_cast<uint64_t>(WINDOW_MASK)) {
        // Tick is outside the current window — reject.
        return false;
    }
    // Sanctioned cast 2: uint64_t → uint32_t; distance <= WINDOW_MASK < UINT32_MAX.
    window_idx_t widx = static_cast<window_idx_t>(distance);

    if (qty > 0U) {
        // ----------------------------------------------------------------
        // SET path (qty > 0)
        // ----------------------------------------------------------------
        // Step 1: write level qty (absolute overwrite — not accumulate).
        // Step 2: set bitmap + summary if level was previously empty.
        level_set(sd, widx, qty);

        // Step 3: update best_tick — compare-and-replace, no scan.
        // IsBid: best_tick = highest tick seen.  ASK: best_tick = lowest tick seen.
        // Special case: TICK_INVALID (0xFFFFFFFF) means side was empty.
        // For BID: new tick is always < UINT32_MAX, so the naive comparison
        // tick > TICK_INVALID is false; must check TICK_INVALID explicitly.
        if constexpr (IsBid) {
            if (sd.best_tick == TICK_INVALID || absolute_tick > sd.best_tick) {
                sd.best_tick = absolute_tick;
            }
        } else {
            if (sd.best_tick == TICK_INVALID || absolute_tick < sd.best_tick) {
                sd.best_tick = absolute_tick;
            }
        }

    } else {
        // ----------------------------------------------------------------
        // DELETE path (qty == 0)
        // ----------------------------------------------------------------
        // Step 1: zero the level.
        // Step 2: clear bitmap bit; conditionally clear summary bit.
        // MUST happen before the fallback scan (bitmap must be current).
        level_clear(sd, widx);

        // Step 3: update best_tick.
        if (absolute_tick == sd.best_tick) {
            // The best level was just deleted — run hierarchical fallback scan.
            // IsBid: scan for highest remaining window slot.
            // ASK:   scan for lowest remaining window slot.
            // Both return the absolute tick (not just the slot index).
            if constexpr (IsBid) {
                sd.best_tick = bitmap_highest_h(sd, impl_->window_base_tick);
            } else {
                sd.best_tick = bitmap_lowest_h(sd, impl_->window_base_tick);
            }
            // bitmap_highest_h / bitmap_lowest_h return TICK_INVALID when empty.
        }
        // If the deleted level was not the best level, best_tick is unchanged (O(1)).
    }

    return true;
}

// Explicit instantiation — compiler must emit both; used for correctness and
// benchmark so the linker finds them regardless of optimisation inlining.
template bool Book::upsert_impl<true> (tick_t, qty_t) noexcept;
template bool Book::upsert_impl<false>(tick_t, qty_t) noexcept;

// ---------------------------------------------------------------------------
// Book::upsert  (spec §Interface Specification / upsert)
// Primary public API — parses strings, then dispatches to upsert_impl.
// ---------------------------------------------------------------------------

bool Book::upsert(side_t      side,
                  const char* price_str, size_t price_len,
                  const char* qty_str,   size_t qty_len) noexcept {
    // Validate side.
    if (side != side_t::BID && side != side_t::ASK) {
        return false;
    }
    // Require book to be initialised.
    if (impl_->window_base_tick == NULL_BASE_TICK) {
        return false;
    }
    // API boundary: parse price string to absolute tick (no float).
    tick_t absolute_tick = parse_price(price_str, price_len);
    if (absolute_tick == TICK_INVALID) {
        return false;
    }
    // API boundary: parse qty string to scaled integer (no float).
    qty_t qty = parse_qty(qty_str, qty_len);
    if (qty == QTY_INVALID) {
        return false;
    }
    // Dispatch: resolve IsBid at the boundary, not inside the hot path.
    if (side == side_t::BID) {
        return upsert_impl<true>(absolute_tick, qty);
    } else {
        return upsert_impl<false>(absolute_tick, qty);
    }
}

// ---------------------------------------------------------------------------
// Book::upsert_by_tick  — benchmark hook; bypasses parsing
// ---------------------------------------------------------------------------

bool Book::upsert_by_tick(side_t side,
                           tick_t absolute_tick,
                           qty_t  qty) noexcept {
    if (side != side_t::BID && side != side_t::ASK) {
        return false;
    }
    if (absolute_tick == TICK_INVALID) {
        return false;
    }
    if (qty == QTY_INVALID) {
        return false;
    }
    if (impl_->window_base_tick == NULL_BASE_TICK) {
        return false;
    }
    if (side == side_t::BID) {
        return upsert_impl<true>(absolute_tick, qty);
    } else {
        return upsert_impl<false>(absolute_tick, qty);
    }
}

// ---------------------------------------------------------------------------
// Book::needs_rebase  (spec §Interface Specification / needs_rebase)
//
// Returns true if either best_bid or best_ask is within REBASE_MARGIN ticks
// of the window edge.
//
// Let bid_widx = window_index(best_bid_tick, window_base_tick)
//     ask_widx = window_index(best_ask_tick, window_base_tick)
//
// Returns true if:
//   bid_widx < REBASE_MARGIN                (best bid near low edge)
//   ask_widx >= WINDOW_SIZE - REBASE_MARGIN  (best ask near high edge)
//
// When a side is empty (TICK_INVALID), treat its widx as WINDOW_SIZE / 2
// (centre — does not trigger rebase by itself).
// ---------------------------------------------------------------------------

bool Book::needs_rebase() const noexcept {
    constexpr window_idx_t centre     = WINDOW_SIZE / 2U;
    constexpr window_idx_t high_edge  = WINDOW_SIZE - REBASE_MARGIN;

    tick_t bid_tick = impl_->sides[0].best_tick;
    tick_t ask_tick = impl_->sides[1].best_tick;

    window_idx_t bid_widx = (bid_tick != TICK_INVALID)
        ? window_index(bid_tick, impl_->window_base_tick)
        : centre;

    window_idx_t ask_widx = (ask_tick != TICK_INVALID)
        ? window_index(ask_tick, impl_->window_base_tick)
        : centre;

    return (bid_widx < REBASE_MARGIN) || (ask_widx >= high_edge);
}

// ---------------------------------------------------------------------------
// Book::rebase  (spec §Interface Specification / rebase)
//
// Cold-path operation.  Option A: allocate a new Impl, populate it from old
// state, swap impl_ pointers, free old Impl.
//
// Algorithm:
//   1. Round new_base_tick down to nearest 64-tick boundary.
//   2. Allocate new_impl via operator new.
//   3. Zero-initialise new_impl (memset).
//   4. For each side, for each window slot:
//      - If levels[widx].total_qty > 0:
//        - old_absolute_tick = window_base_tick + widx
//        - new_widx = (old_absolute_tick - new_base_tick) & WINDOW_MASK
//        - If new_widx fits within [0, WINDOW_SIZE): copy level; update bitmap.
//   5. Recompute best_tick for each side from the new bitmap.
//   6. Set new_impl->window_base_tick = new_base_tick.
//   7. Swap impl_ with new_impl; free old_impl.
// ---------------------------------------------------------------------------

void Book::rebase(uint64_t new_base_tick) noexcept {
    if (new_base_tick == NULL_BASE_TICK) {
        return;
    }
    // Round down to nearest 64-tick boundary (spec precondition).
    new_base_tick = new_base_tick & ~static_cast<uint64_t>(63U);

    Impl* new_impl = static_cast<Impl*>(::operator new(sizeof(Impl)));
    std::memset(new_impl, 0, sizeof(Impl));
    new_impl->sides[0].best_tick = TICK_INVALID;
    new_impl->sides[1].best_tick = TICK_INVALID;
    new_impl->window_base_tick   = new_base_tick;

    for (uint32_t s = 0U; s < 2U; ++s) {
        const book_side_t& old_side = impl_->sides[s];
        book_side_t&       new_side = new_impl->sides[s];

        for (uint32_t widx = 0U; widx < WINDOW_SIZE; ++widx) {
            qty_t qty = old_side.levels[widx].total_qty;
            if (qty == 0U) {
                continue;
            }
            // Reconstruct absolute tick from old window base + slot.
            uint64_t old_abs = impl_->window_base_tick + static_cast<uint64_t>(widx);

            // Compute new window-relative slot.
            uint64_t new_dist = old_abs - new_base_tick;
            if (new_dist > static_cast<uint64_t>(WINDOW_MASK)) {
                // Level falls outside new window — drop it silently.
                continue;
            }
            window_idx_t new_widx = static_cast<window_idx_t>(new_dist);

            // Copy level to new window.
            new_side.levels[new_widx].total_qty = qty;
            bitmap_set_bit(new_side, new_widx);
        }

        // Recompute best_tick for this side from the new bitmaps.
        if (s == 0U) {
            // BID side: highest tick.
            new_side.best_tick = bitmap_highest_h(new_side, new_base_tick);
        } else {
            // ASK side: lowest tick.
            new_side.best_tick = bitmap_lowest_h(new_side, new_base_tick);
        }
    }

    Impl* old_impl = impl_;
    impl_ = new_impl;
    ::operator delete(old_impl);
}

// ---------------------------------------------------------------------------
// Book::reset  (spec §Interface Specification / reset)
//
// Clears all state and re-initialises with a new window base.
// Cold path — called on snapshot re-sync or feed reconnect.
// ---------------------------------------------------------------------------

void Book::reset(uint64_t new_base_tick) noexcept {
    // memset zeros all level qtys, all bitmap words, all summary words, and pads.
    std::memset(impl_, 0, sizeof(Impl));

    // memset sets best_tick to 0; must explicitly set TICK_INVALID.
    impl_->sides[0].best_tick = TICK_INVALID;
    impl_->sides[1].best_tick = TICK_INVALID;
    impl_->window_base_tick   = new_base_tick;
}

// ---------------------------------------------------------------------------
// Book::apply_snapshot  (spec §Interface Specification / apply_snapshot)
//
// Bulk-apply pre-parsed ticks and qtys to one side.
// Caller must call reset() before apply_snapshot().
// Returns the number of levels successfully applied.
// ---------------------------------------------------------------------------

uint32_t Book::apply_snapshot(side_t        side,
                               const tick_t* ticks,
                               const qty_t*  qtys,
                               uint32_t      count) noexcept {
    if (ticks == nullptr || qtys == nullptr) {
        return 0U;
    }
    if (side != side_t::BID && side != side_t::ASK) {
        return 0U;
    }

    uint32_t applied = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        if (upsert_by_tick(side, ticks[i], qtys[i])) {
            ++applied;
        }
    }
    return applied;
}

// ---------------------------------------------------------------------------
// Book::level_qty  (spec §Interface Specification / level_qty)
// Test helper — not on the hot path.
// ---------------------------------------------------------------------------

qty_t Book::level_qty(side_t s, tick_t absolute_tick) const noexcept {
    if (s != side_t::BID && s != side_t::ASK) {
        return 0U;
    }
    if (absolute_tick == TICK_INVALID) {
        return 0U;
    }
    if (impl_->window_base_tick == NULL_BASE_TICK) {
        return 0U;
    }
    uint64_t distance = static_cast<uint64_t>(absolute_tick) - impl_->window_base_tick;
    if (distance > static_cast<uint64_t>(WINDOW_MASK)) {
        return 0U;
    }
    window_idx_t widx = static_cast<window_idx_t>(distance);
    return this->side(s).levels[widx].total_qty;
}

// ---------------------------------------------------------------------------
// Book::window_base  (spec §Interface Specification / window_base)
// ---------------------------------------------------------------------------

uint64_t Book::window_base() const noexcept {
    return impl_->window_base_tick;
}

} // namespace eth::book
