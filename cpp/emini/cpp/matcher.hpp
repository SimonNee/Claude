/* matcher.hpp — E-mini S&P 500 order book matcher (Module 4)
 *
 * Architecture: architect-spec.md, Module Boundaries / Module 4: Matcher
 *
 * The Matcher is the only code in the system with simultaneous read/write
 * access to both sides of the book. It hides the cross-side matching algorithm.
 *
 * Public interface: Matcher::execute — one static method (spec mandates a
 * single public method named execute).
 *
 * The Matcher calls bitmap_best_ask / bitmap_best_bid and queue_dequeue_head;
 * it is above the Bitmap and Queue modules in the dependency graph.
 * It does not call book_cancel or book_add.
 */

#pragma once

#include "book.hpp"

namespace es::book {

class Matcher {
public:
    // Deleted — Matcher is a pure namespace-style module with no state.
    Matcher()                          = delete;
    Matcher(const Matcher&)            = delete;
    Matcher& operator=(const Matcher&) = delete;

    // Execute matching of an incoming aggressive order against the resting book.
    //
    // aggressor_side — the side of the incoming (taker) order
    // price          — the limit price of the incoming order (external double)
    // quantity       — quantity to fill
    // taker_id       — order_id to record in fill records; not verified inside
    //
    // Returns a fill_result_t describing all fills generated.
    // On invalid arguments or no crossing: fill_count = 0, remaining_qty = quantity.
    [[nodiscard]] static fill_result_t execute(Book::Impl&  impl,
                                               side_t       aggressor_side,
                                               double       price,
                                               qty_t        quantity,
                                               order_id_t   taker_id) noexcept;
};

} // namespace es::book
