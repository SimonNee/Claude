/*
 * bitmap.h — Bitmap scan functions, inlined into every TU that includes
 *            this header.
 *
 * bitmap_best_ask and bitmap_best_bid were previously defined in book.c with
 * external linkage and declared in matcher.h. This caused matcher.c to emit
 * PLT calls for both functions on every outer match iteration instead of
 * inlining the 138-word bitmap scan. Moving them here as static inline
 * gives the compiler full visibility in both book.c and matcher.c.
 *
 * Include this header in every TU that needs the bitmap scan functions.
 * The static keyword prevents multiple-definition errors when this header
 * is included in more than one TU.
 */

#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>
#include "book.h"   /* tick_t, BITMAP_WORDS, MAX_TICKS, TICK_INVALID */

/*
 * bitmap_best_ask — lowest active tick on the ask side.
 *
 * Scans words from index 0 upward; uses TZCNT (__builtin_ctzll).
 * Returns TICK_INVALID if no bit is set within [0, MAX_TICKS).
 */
static inline tick_t bitmap_best_ask(const uint64_t *bitmap)
{
    for (uint32_t w = 0U; w < BITMAP_WORDS; ++w) {
        if (bitmap[w] != 0U) {
            uint32_t bit = (uint32_t)__builtin_ctzll(bitmap[w]);
            return w * 64U + bit;
        }
    }
    return TICK_INVALID;
}

/*
 * bitmap_best_bid — highest active tick on the bid side.
 *
 * Scans words from BITMAP_WORDS-1 downward; uses CLZLL (__builtin_clzll).
 * Returns TICK_INVALID if no bit is set within [0, MAX_TICKS).
 */
static inline tick_t bitmap_best_bid(const uint64_t *bitmap)
{
    for (int w = (int)BITMAP_WORDS - 1; w >= 0; --w) {
        if (bitmap[w] != 0U) {
            uint32_t bit = 63U - (uint32_t)__builtin_clzll(bitmap[w]);
            return (uint32_t)w * 64U + bit;
        }
    }
    return TICK_INVALID;
}

#endif /* BITMAP_H */
