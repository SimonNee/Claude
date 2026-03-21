/*
 * loader.h — CSV event loader for the E-mini S&P 500 order book benchmark.
 *
 * Parses orders.csv into a heap-allocated event_t array. The CSV tick column
 * is already an integer; no float arithmetic occurs in the loader. The loader
 * is not timed.
 *
 * event_t layout (16 bytes):
 *   ref_idx  — row index of the ADD being cancelled; 0 for ADD and MATCH
 *   tick     — integer tick read directly from the CSV; 0 for CANCEL
 *   qty      — quantity for ADD/MATCH; 0 for CANCEL
 *   type     — event_type_t: EV_ADD, EV_CANCEL, or EV_MATCH
 *   side     — side_t: BID or ASK
 *   _pad[2]  — explicit pad to 16 bytes
 *
 * event_type_t follows the same idiom as side_t in book.h: uint8_t typedef
 * with named constants. This keeps the struct at 16 bytes without any
 * attribute extensions, and allows the -Wconversion rule to be enforced.
 *
 * CSV format (header): event_type,side,tick,quantity,ref_idx
 * tick is a plain integer (e.g. 4391). CANCEL rows have tick=0.
 * Valid non-zero tick range: [0, 8799] (MAX_TICKS - 1).
 *
 * Build: -std=c11 -O2 -march=native -Wall -Wextra -Wconversion
 *        -Wsign-conversion -Wsign-compare -Werror
 */

#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include "book.h"   /* side_t, tick_t, qty_t, BID, ASK */

/* -------------------------------------------------------------------------
 * Event type
 *
 * Follows the same pattern as side_t in book.h: uint8_t typedef with
 * named constants. Using uint8_t (not enum) keeps event_t at 16 bytes
 * without relying on compiler-specific enum size attributes.
 * ---------------------------------------------------------------------- */

typedef uint8_t event_type_t;
#define EV_ADD    ((event_type_t)0U)
#define EV_CANCEL ((event_type_t)1U)
#define EV_MATCH  ((event_type_t)2U)

/* -------------------------------------------------------------------------
 * Event struct
 *
 * 16 bytes. Fields ordered to avoid padding waste:
 *   3 × uint32_t = 12 bytes
 *   2 × uint8_t  =  2 bytes  (event_type_t + side_t)
 *   2 × uint8_t  =  2 bytes  (_pad)
 *   Total        = 16 bytes
 *
 * _Static_assert confirms the layout at compile time.
 *
 * ref_idx: for CANCEL events, the 0-based data row index (header excluded)
 *          of the ADD event being cancelled. 0 for ADD and MATCH.
 * tick:    for ADD/MATCH, the integer tick read directly from the CSV.
 *          0 for CANCEL. Valid range when non-zero: [0, MAX_TICKS - 1].
 * ---------------------------------------------------------------------- */

typedef struct {
    uint32_t     ref_idx;   /* offset  0 — ADD row index for cancels; 0 otherwise */
    tick_t       tick;      /* offset  4 — pre-converted price tick; 0 for CANCEL */
    qty_t        qty;       /* offset  8 — quantity; 0 for CANCEL */
    event_type_t type;      /* offset 12 — EV_ADD / EV_CANCEL / EV_MATCH */
    side_t       side;      /* offset 13 — BID or ASK */
    uint8_t      _pad[2];   /* offset 14 — explicit pad to 16 bytes */
} event_t;

_Static_assert(sizeof(event_t) == 16, "event_t layout changed");

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * events_load — open path, parse every data row, and return a heap-allocated
 * event_t array of *count entries.
 *
 * Returns NULL on any error (file not found, allocation failure, parse error).
 * Caller must check the return value before use.
 * Caller owns the returned array and must free() it when done.
 *
 * Rows with invalid event_type strings or out-of-range prices are skipped
 * with a warning to stderr; they do not cause a fatal error.
 *
 * The loader is not timed. Run it once before any benchmark loop.
 */
event_t *events_load(const char *path, uint32_t *count);

#endif /* LOADER_H */
