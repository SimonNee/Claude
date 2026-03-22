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
 */

#include "matcher.hpp"
#include "internal.hpp"

namespace es::book {

using namespace es::book::internal;

// ---------------------------------------------------------------------------
// match_core — shared matching loop, called after the aggressor_tick is known.
//
// Factored out so that execute() (double API) and execute_by_tick() (tick API)
// share a single implementation with no code duplication.
// ---------------------------------------------------------------------------

static fill_result_t match_core(Book::Impl& impl,
                                side_t      aggressor_side,
                                tick_t      aggressor_tick,
                                qty_t       quantity,
                                order_id_t  taker_id) noexcept {
    fill_result_t result;
    result.fill_count    = 0U;
    result.remaining_qty = quantity;

    // Determine maker side (opposite of aggressor).
    // agg_idx is 0 (BID) or 1 (ASK); maker_idx is the complementary value.
    // The subtraction 1U - agg_idx promotes both to unsigned int; explicit cast to uint8_t.
    uint8_t agg_idx   = static_cast<uint8_t>(aggressor_side);
    uint8_t maker_idx = static_cast<uint8_t>(1U - static_cast<uint32_t>(agg_idx));
    book_side_t& maker_side = impl.sides[maker_idx];

    // Matching loop — iterate maker side from its best price toward aggressor price
    while (result.remaining_qty > 0U && result.fill_count < 64U) {

        // Find the best price on the maker side
        int best_raw;
        if (aggressor_side == side_t::BID) {
            // Aggressor is BID → maker is ASK → want lowest ask
            best_raw = bitmap_lowest<BITMAP_WORDS>(maker_side.bitmap);
        } else {
            // Aggressor is ASK → maker is BID → want highest bid
            best_raw = bitmap_highest<BITMAP_WORDS>(maker_side.bitmap);
        }

        // Empty maker side
        if (best_raw < 0) {
            break;
        }

        tick_t best_tick = static_cast<tick_t>(best_raw);

        // Check crossing condition
        // BID aggressor: crosses if best_ask_tick <= aggressor_tick
        // ASK aggressor: crosses if best_bid_tick >= aggressor_tick
        bool crosses;
        if (aggressor_side == side_t::BID) {
            crosses = (best_tick <= aggressor_tick);
        } else {
            crosses = (best_tick >= aggressor_tick);
        }
        if (!crosses) {
            break;
        }

        // Drain the head of the best maker level
        price_level_t& level = maker_side.levels[best_tick];

        if (level.head_idx == NULL_IDX) {
            // Level appears active in bitmap but has no orders — bitmap is stale.
            // This must not happen (spec: bitmap updates are synchronous-only).
            // Treat as empty and clear the bit defensively.
            bitmap_clear(maker_side.bitmap, best_tick);
            break;
        }

        // Capture maker slot index BEFORE queue_dequeue_head advances level.head_idx.
        // The slot index IS the order_id by construction (TRIZ Trimming: order_id
        // field was removed from order_node_t; the arena slot index is identical).
        slot_idx_t    maker_slot = level.head_idx;
        order_node_t& head       = impl.arena.nodes[maker_slot];
        qty_t maker_qty          = head.quantity;
        qty_t fill_qty;

        if (maker_qty <= result.remaining_qty) {
            // Full fill of the maker order
            fill_qty = maker_qty;
            result.remaining_qty -= fill_qty;

            // Record fill — maker_slot == former order_id by construction
            fill_t& f         = result.fills[result.fill_count++];
            f.maker_order_id  = maker_slot;
            f.taker_order_id  = taker_id;
            f.price_tick      = best_tick;
            f.filled_qty      = fill_qty;

            // Dequeue head and mark DEAD; clears bitmap bit if level empties
            queue_dequeue_head(level, impl.arena, maker_side.bitmap, best_tick);

        } else {
            // Partial fill of the maker order — maker stays at head
            fill_qty = result.remaining_qty;
            result.remaining_qty = 0U;

            // Record fill — maker_slot is unchanged (node stays at head with
            // decremented quantity; same slot index, same logical order_id)
            fill_t& f         = result.fills[result.fill_count++];
            f.maker_order_id  = maker_slot;
            f.taker_order_id  = taker_id;
            f.price_tick      = best_tick;
            f.filled_qty      = fill_qty;

            // Partial fill: decrement head node quantity, update level total_qty
            queue_partial_fill_head(level, impl.arena, fill_qty);
            // Level remains live; bitmap bit stays set.
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

    return match_core(impl, aggressor_side, aggressor_tick, quantity, taker_id);
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

    return match_core(impl, aggressor_side, tick, quantity, taker_id);
}

} // namespace es::book
