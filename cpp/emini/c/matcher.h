/*
 * matcher.h — Module 4: Matcher interface.
 *
 * The matcher is the only code in the system with simultaneous read/write
 * access to both sides of the book. It is the single entry point for
 * the cross-side matching algorithm.
 *
 * The matcher does not call book_add or book_cancel; it operates directly
 * on the book's internal structures via the book_t pointer.
 *
 * Public entry point: matcher_execute.
 *
 * bitmap_best_ask and bitmap_best_bid are static inline in bitmap.h.
 * Both this TU and book.c include bitmap.h, giving the compiler full
 * visibility for inlining the 138-word bitmap scan in both TUs.
 */

#ifndef MATCHER_H
#define MATCHER_H

#include "book.h"
#include "bitmap.h"

/* -------------------------------------------------------------------------
 * bitmap_is_set_pub — check whether a tick bit is set, for test harness use.
 * Defined in book.c with external linkage; not on the hot path.
 * ---------------------------------------------------------------------- */
bool bitmap_is_set_pub(const book_t *book, side_t side, tick_t tick);

/* -------------------------------------------------------------------------
 * matcher_execute — Module 4 public entry point.
 *
 * Called by book_match after input validation and tick conversion.
 * aggressor_tick has already been validated (< MAX_TICKS) by book_match.
 *
 * aggressor_side: BID → match against ASK side (best ask upward)
 *                 ASK → match against BID side (best bid downward)
 *
 * Writes all fill records and remaining_qty into *out. The caller
 * (book_match) initialises out->fill_count = 0 and
 * out->remaining_qty = quantity before calling. matcher_execute
 * overwrites both fields on return.
 * ---------------------------------------------------------------------- */
void matcher_execute(book_t *book, side_t aggressor_side,
                     tick_t aggressor_tick, qty_t quantity,
                     order_id_t taker_id, fill_result_t *out);

#endif /* MATCHER_H */
