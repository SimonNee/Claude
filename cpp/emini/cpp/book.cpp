/* book.cpp — E-mini S&P 500 limit order book implementation
 *
 * Architecture: architect-spec.md
 * Build flags:  -std=c++17 -O2 -march=native -Wall -Wextra
 *               -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *
 * Module decomposition matches spec section "Module Boundaries":
 *   Module 1 — Arena       (internal.hpp :: arena_alloc)
 *   Module 2 — Level Queue (internal.hpp :: queue_*)
 *   Module 3 — Bitmap      (internal.hpp :: level_set / level_clear)
 *   Module 4 — Matcher     (matcher.hpp  :: Matcher::execute)
 *   Module 5 — Book        (this file)
 *
 * Combined best-bid/ask strategy:
 *
 *   ADD PATH:
 *     1. If level was empty, call level_set (maintains bitmap + summary).
 *     2. Update best_tick with a compare — no scan.
 *        BID: best_tick = max(best_tick, tick)   if best_tick != TICK_INVALID
 *        ASK: best_tick = min(best_tick, tick)   if best_tick != TICK_INVALID
 *        When side was empty (TICK_INVALID), best_tick = tick directly.
 *
 *   CANCEL / MATCH DRAIN PATH (in internal.hpp queue_remove / queue_dequeue_head):
 *     If the level empties AND tick == best_tick, call bitmap_highest_h /
 *     bitmap_lowest_h for the new best (hierarchical fallback — 2 TZCNT/LZCNT).
 *     Never falls back to the 138-word flat scan.
 */

#include "book.hpp"
#include "internal.hpp"
#include "matcher.hpp"

#include <cstring>
#include <new>

namespace es::book {

using namespace es::book::internal;

// ---------------------------------------------------------------------------
// Book::Book  (spec: Interface Specification / book_create / C++ constructor)
// ---------------------------------------------------------------------------

Book::Book(double base_price) {
    // Precondition: base_price must be finite and positive (spec)
    if (!std::isfinite(base_price) || base_price <= 0.0) {
        // Precondition violation is a programming error — hard fail.
        std::terminate();
    }

    // Heap-allocate Impl (~15.5 MB; too large for the stack)
    impl_ = static_cast<Impl*>(::operator new(sizeof(Impl)));

    // Initialise all price levels to empty sentinel.
    // NULL_IDX == 0xFFFFFFFF != 0, so memset alone is insufficient for head/tail.
    for (uint32_t s = 0U; s < 2U; ++s) {
        book_side_t& sd = impl_->sides[s];
        for (uint32_t t = 0U; t < MAX_TICKS; ++t) {
            sd.levels[t].head_idx  = NULL_IDX;
            sd.levels[t].tail_idx  = NULL_IDX;
            sd.levels[t].count     = 0U;
            sd.levels[t].total_qty = 0U;
        }
        std::memset(sd.bitmap,  0, sizeof(sd.bitmap));
        std::memset(sd.summary, 0, sizeof(sd.summary));
        sd.best_tick  = TICK_INVALID;  // side starts empty
        sd._side_pad  = 0U;
    }

    impl_->arena.next_slot = 0U;
    impl_->base_price      = base_price;
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
// book_add_impl — shared add logic used by add() and add_by_tick().
//
// Factored out to avoid duplicating the best_tick update logic.
// Not a member function — internal linkage within this TU.
// ---------------------------------------------------------------------------

static order_id_t book_add_impl(Book::Impl& impl,
                                side_t      side,
                                tick_t      tick,
                                qty_t       quantity) noexcept {
    slot_idx_t slot = arena_alloc(impl.arena, quantity);
    if (slot == NULL_IDX) {
        return NULL_IDX;
    }

    book_side_t&   sd    = impl.sides[static_cast<uint8_t>(side)];
    price_level_t& level = sd.levels[tick];

    // level_set maintains both bitmap[] and summary[] when a level
    // transitions from empty to non-empty (synchronous, Module 3).
    if (level.count == 0U) {
        level_set(sd, tick);
    }

    // Enqueue at FIFO tail (Module 2).
    queue_enqueue(level, impl.arena, slot);

    // ADD PATH: update best_tick — no scan.
    //
    // BID side (side_index 0): best bid = highest tick.
    //   If side was empty or the new tick is higher, update.
    // ASK side (side_index 1): best ask = lowest tick.
    //   If side was empty or the new tick is lower, update.
    if (side == side_t::BID) {
        if (sd.best_tick == TICK_INVALID || tick > sd.best_tick) {
            sd.best_tick = tick;
        }
    } else {
        if (sd.best_tick == TICK_INVALID || tick < sd.best_tick) {
            sd.best_tick = tick;
        }
    }

    return slot;
}

// ---------------------------------------------------------------------------
// Book::add  (spec: Interface Specification / book_add)
// ---------------------------------------------------------------------------

order_id_t Book::add(side_t side, double price, qty_t quantity) noexcept {
    // Validate side
    if (side != side_t::BID && side != side_t::ASK) {
        return NULL_IDX;
    }
    // Validate quantity
    if (quantity == 0U) {
        return NULL_IDX;
    }
    // API boundary: convert price to internal tick (sanctioned cast is inside price_to_tick)
    tick_t tick = price_to_tick(price, impl_->base_price);
    if (tick == TICK_INVALID) {
        return NULL_IDX;
    }

    return book_add_impl(*impl_, side, tick, quantity);
}

// ---------------------------------------------------------------------------
// Book::add_by_tick  — tick-direct path; skips price_to_tick().
// Used by the data-driven benchmark (loader pre-converts prices to ticks).
// ---------------------------------------------------------------------------

order_id_t Book::add_by_tick(side_t side, tick_t tick, qty_t quantity) noexcept {
    if (side != side_t::BID && side != side_t::ASK) {
        return NULL_IDX;
    }
    if (quantity == 0U) {
        return NULL_IDX;
    }
    if (tick >= MAX_TICKS) {
        return NULL_IDX;
    }

    return book_add_impl(*impl_, side, tick, quantity);
}

// ---------------------------------------------------------------------------
// Book::cancel  (spec: Interface Specification / book_cancel)
// ---------------------------------------------------------------------------

bool Book::cancel(order_id_t order_id, side_t side, tick_t tick) noexcept {
    // Validate arguments
    if (side != side_t::BID && side != side_t::ASK) {
        return false;
    }
    if (tick >= MAX_TICKS) {
        return false;
    }
    if (order_id >= MAX_ORDERS || order_id >= impl_->arena.next_slot) {
        return false;
    }

    // Check DEAD_FLAG before touching the level (spec: idempotent cancel guard)
    order_node_t& node = impl_->arena.nodes[order_id];
    if (node.flags & DEAD_FLAG) {
        return false;
    }

    book_side_t& sd = this->side(side);

    // queue_remove<IsBid> performs O(1) head-cancel or O(q) mid-queue scan,
    // sets DEAD_FLAG on the node, clears bitmap/summary if level empties,
    // and updates best_tick via hierarchical fallback if the best level drained.
    if (side == side_t::BID) {
        return queue_remove<true>(sd, sd.levels[tick], impl_->arena, tick, order_id);
    } else {
        return queue_remove<false>(sd, sd.levels[tick], impl_->arena, tick, order_id);
    }
}

// ---------------------------------------------------------------------------
// Book::match  (spec: Interface Specification / book_match)
// Delegated entirely to Matcher (Module 4) — the only code with simultaneous
// read/write access to both sides.
// ---------------------------------------------------------------------------

fill_result_t Book::match(side_t     aggressor_side,
                          double     price,
                          qty_t      quantity,
                          order_id_t taker_id) noexcept {
    return Matcher::execute(*impl_, aggressor_side, price, quantity, taker_id);
}

// ---------------------------------------------------------------------------
// Book::match_by_tick  — tick-direct path; skips price_to_tick().
// Used by the data-driven benchmark (loader pre-converts ticks directly).
// ---------------------------------------------------------------------------

fill_result_t Book::match_by_tick(side_t     aggressor_side,
                                  tick_t     tick,
                                  qty_t      quantity,
                                  order_id_t taker_id) noexcept {
    return Matcher::execute_by_tick(*impl_, aggressor_side, tick, quantity, taker_id);
}

// ---------------------------------------------------------------------------
// Book::level_count / Book::level_qty  (not on hot path — used by tests)
// ---------------------------------------------------------------------------

uint32_t Book::level_count(side_t s, tick_t tick) const noexcept {
    if (s != side_t::BID && s != side_t::ASK) { return 0U; }
    if (tick >= MAX_TICKS) { return 0U; }
    return this->side(s).levels[tick].count;
}

qty_t Book::level_qty(side_t s, tick_t tick) const noexcept {
    if (s != side_t::BID && s != side_t::ASK) { return 0U; }
    if (tick >= MAX_TICKS) { return 0U; }
    return this->side(s).levels[tick].total_qty;
}

// ---------------------------------------------------------------------------
// Book::reset  (spec: Interface Specification / book_reset)
// Session boundary only — not on the hot path.
// ---------------------------------------------------------------------------

void Book::reset() noexcept {
    for (uint32_t s = 0U; s < 2U; ++s) {
        book_side_t& sd = impl_->sides[s];
        for (uint32_t t = 0U; t < MAX_TICKS; ++t) {
            sd.levels[t].head_idx  = NULL_IDX;
            sd.levels[t].tail_idx  = NULL_IDX;
            sd.levels[t].count     = 0U;
            sd.levels[t].total_qty = 0U;
        }
        std::memset(sd.bitmap,  0, sizeof(sd.bitmap));
        std::memset(sd.summary, 0, sizeof(sd.summary));
        sd.best_tick = TICK_INVALID;
        sd._side_pad = 0U;
    }
    impl_->arena.next_slot = 0U;
    // base_price is retained across reset (spec)
}

} // namespace es::book
