/* matcher.cpp — E-mini S&P 500 order book matcher implementation (Module 4)
 *
 * Architecture: architect-spec.md, Module Boundaries / Module 4: Matcher
 * Build flags:  -std=c++17 -O2 -march=native -Wall -Wextra
 *               -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *
 * The Matcher receives a Book::Impl& and operates directly on the internal
 * data structures. It is the only code with simultaneous read/write access to
 * both sides.
 *
 * Matching algorithm (spec: Interface Specification / book_match):
 *   BID aggressor → match against ASK side, from best_ask upward while
 *                   best_ask_tick <= aggressor_tick and remaining_qty > 0
 *   ASK aggressor → match against BID side, from best_bid downward while
 *                   best_bid_tick >= aggressor_tick and remaining_qty > 0
 *
 * Per-level: consume head orders until the level is drained or remaining_qty
 * reaches zero. Partial fills leave the head node in place with decremented
 * quantity. Full fills dequeue the head node and mark it DEAD. If a level
 * empties, the bitmap bit is cleared synchronously.
 *
 * Match terminates when:
 *   - remaining_qty == 0, or
 *   - no more crossing prices exist, or
 *   - fill_count == 64  (spec-defined bound)
 *
 * Combined best-bid/ask strategy in match_core:
 *
 *   FAST PATH — level selection:
 *     Read maker_side.best_tick directly — one field load, no bitmap scan.
 *     The field is kept current by the add / cancel paths.
 *
 *   FALLBACK PATH — on drain:
 *     queue_dequeue_head<IsBid> updates best_tick via hierarchical bitmap
 *     (bitmap_highest_h / bitmap_lowest_h) when the best level empties.
 *     The 138-word flat scan is never called.
 *
 * IsBid template on match_core selects the correct direction:
 *   IsBid = true  → aggressor is BID, maker is ASK (want lowest ask)
 *   IsBid = false → aggressor is ASK, maker is BID (want highest bid)
 *
 * The maker side's IsBid for the queue operations is the OPPOSITE direction:
 *   maker is ASK (IsBid=false for queue) when aggressor is BID
 *   maker is BID (IsBid=true  for queue) when aggressor is ASK
 */

#include "matcher.hpp"
#include "internal.hpp"

namespace es::book {

using namespace es::book::internal;

// ---------------------------------------------------------------------------
// match_core — templated on AggressorIsBid.
//
// AggressorIsBid = true  → BID aggressor, ASK maker side, lowest ask is best
// AggressorIsBid = false → ASK aggressor, BID maker side, highest bid is best
//
// Templating allows the compiler to eliminate the runtime branch on side
// direction entirely and select the correct queue_dequeue_head instantiation
// at compile time.
// ---------------------------------------------------------------------------

template<bool AggressorIsBid>
static fill_result_t match_core(Book::Impl& impl,
                                tick_t      aggressor_tick,
                                qty_t       quantity,
                                order_id_t  taker_id) noexcept {
    fill_result_t result;
    result.fill_count    = 0U;
    result.remaining_qty = quantity;

    // Select the maker side (opposite of aggressor).
    // AggressorIsBid=true  → aggressor side index 0 (BID) → maker index 1 (ASK)
    // AggressorIsBid=false → aggressor side index 1 (ASK) → maker index 0 (BID)
    constexpr uint8_t maker_idx = AggressorIsBid ? 1U : 0U;
    book_side_t& maker_side = impl.sides[maker_idx];

    // Matching loop — consume maker orders from the best price inward.
    while (result.remaining_qty > 0U && result.fill_count < 64U) {

        // FAST PATH: read best_tick — one field load, no bitmap scan.
        tick_t best_tick = maker_side.best_tick;

        // Empty maker side.
        if (best_tick == TICK_INVALID) {
            break;
        }

        // Check crossing condition.
        // BID aggressor (AggressorIsBid=true):  crosses if best_ask_tick <= aggressor_tick
        // ASK aggressor (AggressorIsBid=false): crosses if best_bid_tick >= aggressor_tick
        bool crosses;
        if constexpr (AggressorIsBid) {
            crosses = (best_tick <= aggressor_tick);
        } else {
            crosses = (best_tick >= aggressor_tick);
        }
        if (!crosses) {
            break;
        }

        // Drain the head of the best maker level.
        price_level_t& level = maker_side.levels[best_tick];

        if (level.head_idx == NULL_IDX) {
            // Level appears active in best_tick but has no orders.
            // This must not happen with synchronous updates (spec).
            // Treat as empty: invalidate best_tick defensively and stop.
            maker_side.best_tick = TICK_INVALID;
            break;
        }

        order_node_t& head  = impl.arena.nodes[level.head_idx];
        qty_t         maker_qty = head.quantity;
        qty_t         fill_qty;

        if (maker_qty <= result.remaining_qty) {
            // Full fill of the maker order.
            fill_qty              = maker_qty;
            result.remaining_qty -= fill_qty;

            // Record fill.
            fill_t& f        = result.fills[result.fill_count++];
            f.maker_order_id = head.order_id;
            f.taker_order_id = taker_id;
            f.price_tick     = best_tick;
            f.filled_qty     = fill_qty;

            // Dequeue head and mark DEAD.
            // FALLBACK PATH (inside queue_dequeue_head): if the level empties
            // and best_tick == tick, the hierarchical bitmap finds the new best.
            // MakerIsBid is opposite of AggressorIsBid.
            queue_dequeue_head<!AggressorIsBid>(
                maker_side, level, impl.arena, best_tick);

        } else {
            // Partial fill of the maker order — maker stays at head.
            fill_qty             = result.remaining_qty;
            result.remaining_qty = 0U;

            // Record fill.
            fill_t& f        = result.fills[result.fill_count++];
            f.maker_order_id = head.order_id;
            f.taker_order_id = taker_id;
            f.price_tick     = best_tick;
            f.filled_qty     = fill_qty;

            // Partial fill: decrement head node quantity, update level total_qty.
            // Level remains live; best_tick is unchanged.
            queue_partial_fill_head(level, impl.arena, fill_qty);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Matcher::execute — public double API; price_to_tick() at the boundary.
// ---------------------------------------------------------------------------

fill_result_t Matcher::execute(Book::Impl& impl,
                               side_t      aggressor_side,
                               double      price,
                               qty_t       quantity,
                               order_id_t  taker_id) noexcept {
    fill_result_t early;
    early.fill_count    = 0U;
    early.remaining_qty = quantity;

    if (aggressor_side != side_t::BID && aggressor_side != side_t::ASK) {
        return early;
    }
    if (quantity == 0U) {
        return early;
    }

    // API boundary: convert price (sanctioned cast is inside price_to_tick)
    tick_t aggressor_tick = price_to_tick(price, impl.base_price);
    if (aggressor_tick == TICK_INVALID) {
        return early;
    }

    if (aggressor_side == side_t::BID) {
        return match_core<true>(impl, aggressor_tick, quantity, taker_id);
    } else {
        return match_core<false>(impl, aggressor_tick, quantity, taker_id);
    }
}

// ---------------------------------------------------------------------------
// Matcher::execute_by_tick — tick-direct path; skips price_to_tick().
// Used by the data-driven benchmark when the tick was pre-converted by the
// CSV loader. Not part of the public API.
// ---------------------------------------------------------------------------

fill_result_t Matcher::execute_by_tick(Book::Impl& impl,
                                       side_t      aggressor_side,
                                       tick_t      tick,
                                       qty_t       quantity,
                                       order_id_t  taker_id) noexcept {
    fill_result_t early;
    early.fill_count    = 0U;
    early.remaining_qty = quantity;

    if (aggressor_side != side_t::BID && aggressor_side != side_t::ASK) {
        return early;
    }
    if (quantity == 0U) {
        return early;
    }
    if (tick >= MAX_TICKS) {
        return early;
    }

    if (aggressor_side == side_t::BID) {
        return match_core<true>(impl, tick, quantity, taker_id);
    } else {
        return match_core<false>(impl, tick, quantity, taker_id);
    }
}

} // namespace es::book
