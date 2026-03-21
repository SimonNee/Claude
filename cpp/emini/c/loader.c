/*
 * loader.c — CSV event loader implementation.
 *
 * Parses orders.csv (header + data rows) into a heap-allocated event_t array.
 * The CSV tick column is a plain integer; no float arithmetic occurs here.
 * No float ever enters this file.
 *
 * CSV format (header): event_type,side,tick,quantity,ref_idx
 *
 * Parse strategy:
 *   - fgets line-by-line: avoids any dynamic token allocation.
 *   - sscanf with %10s for event_type and side strings: bounded, no overflow.
 *   - %u for tick, quantity, ref_idx: all unsigned integer fields.
 *   - Row index is 0-based counting from the first data row (header excluded).
 *     This matches the ref_idx values in the CSV.
 *
 * No-cast rule compliance:
 *   - All integer intermediates are uint32_t or explicitly sized.
 *   - The only narrowing casts are (tick_t), (qty_t), and (uint32_t) applied
 *     after explicit range checks that bound the values to uint32_t range.
 *   - No float is used anywhere in this file.
 */

#include "loader.h"
#include "book.h"   /* tick_t, qty_t, side_t, TICK_INVALID, MAX_TICKS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* -------------------------------------------------------------------------
 * Internal constants
 * ---------------------------------------------------------------------- */

/* Initial allocation capacity; grown by doubling if needed. */
#define INITIAL_CAPACITY 131072U

/*
 * LINE_BUF: fgets buffer size in bytes. Declared as a plain int literal
 * so it can be passed directly to fgets without a cast.
 * event_type(6) + side(3) + price(10) + qty(6) + ref_idx(10) +
 * delimiters + newline < 64 bytes; 256 is generous.
 */
#define LINE_BUF 256

/* Maximum token widths for sscanf — prevents buffer overrun. */
#define MAX_TYPE_LEN 10
#define MAX_SIDE_LEN 10

/* -------------------------------------------------------------------------
 * Internal: parse one CSV data line.
 *
 * Fills *ev on success. Returns 1 on success, 0 to skip (invalid row).
 * row_idx is the 0-based data-row index (header not counted).
 * ---------------------------------------------------------------------- */

static int parse_row(const char *line, uint32_t row_idx, event_t *ev)
{
    char type_str[MAX_TYPE_LEN + 1];
    char side_str[MAX_SIDE_LEN + 1];
    unsigned int tick_u;
    unsigned int qty_u;
    unsigned int ref_u;

    /*
     * sscanf format: two bounded strings, then three unsigned integers.
     * %10s reads at most 10 characters into an 11-byte buffer — safe.
     * %u reads tick, quantity, and ref_idx as unsigned int (uint32_t range
     * on all LP64 platforms; confirmed by the static_assert below).
     *
     * The CSV column order is: event_type,side,tick,quantity,ref_idx
     */
    _Static_assert(sizeof(unsigned int) == sizeof(uint32_t),
                   "unsigned int must be 32 bits for sscanf %u to uint32_t");

    int n = sscanf(line, "%10[^,],%10[^,],%u,%u,%u",
                   type_str, side_str, &tick_u, &qty_u, &ref_u);
    if (n != 5) {
        fprintf(stderr, "loader: malformed row %u (sscanf returned %d): %s",
                row_idx, n, line);
        return 0;
    }

    /* --- event_type --- */
    event_type_t etype;
    if (strcmp(type_str, "ADD") == 0) {
        etype = EV_ADD;
    } else if (strcmp(type_str, "CANCEL") == 0) {
        etype = EV_CANCEL;
    } else if (strcmp(type_str, "MATCH") == 0) {
        etype = EV_MATCH;
    } else {
        fprintf(stderr, "loader: unknown event_type '%s' at row %u\n",
                type_str, row_idx);
        return 0;
    }

    /* --- side --- */
    side_t side;
    if (strcmp(side_str, "BID") == 0) {
        side = BID;
    } else if (strcmp(side_str, "ASK") == 0) {
        side = ASK;
    } else {
        fprintf(stderr, "loader: unknown side '%s' at row %u\n",
                side_str, row_idx);
        return 0;
    }

    /* --- tick (for ADD and MATCH; CANCEL rows have tick=0 in the CSV) --- */
    tick_t tick = (tick_t)tick_u;   /* uint32_t → tick_t (same underlying type) */
    if (etype == EV_ADD || etype == EV_MATCH) {
        /*
         * Validate non-zero ticks are within the book's range [0, MAX_TICKS-1].
         * tick_u == 0 is accepted for ADD/MATCH only if the CSV actually wrote 0;
         * that would be a malformed ADD, so reject it.
         */
        if (tick_u == 0U || tick_u >= MAX_TICKS) {
            fprintf(stderr, "loader: tick %u out of range [1, %u) at row %u\n",
                    tick_u, MAX_TICKS, row_idx);
            return 0;
        }
    } else {
        /* EV_CANCEL: tick must be 0 in the new CSV format */
        tick = 0U;
    }

    /* --- qty --- */
    qty_t qty = 0U;
    if (etype == EV_ADD || etype == EV_MATCH) {
        if (qty_u == 0U) {
            fprintf(stderr, "loader: zero quantity at row %u\n", row_idx);
            return 0;
        }
        qty = (qty_t)qty_u;   /* uint32_t → qty_t (same underlying type) */
    }

    /* --- ref_idx --- */
    uint32_t ref_idx = (uint32_t)ref_u;   /* unsigned int to uint32_t; same width, explicit cast */

    /* --- fill the event struct using designated initialisers --- */
    /* _pad is omitted: compound literal zero-fills all unnamed fields. */
    *ev = (event_t){
        .type    = etype,
        .side    = side,
        .tick    = tick,
        .qty     = qty,
        .ref_idx = ref_idx
    };

    return 1;
}

/* -------------------------------------------------------------------------
 * events_load — public entry point.
 * ---------------------------------------------------------------------- */

event_t *events_load(const char *path, uint32_t *count)
{
    *count = 0U;

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "loader: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }

    /* Allocate initial buffer */
    uint32_t capacity = INITIAL_CAPACITY;
    event_t *events   = (event_t *)malloc((size_t)capacity * sizeof(event_t));
    if (events == NULL) {
        fprintf(stderr, "loader: initial malloc failed (%u entries)\n", capacity);
        fclose(f);
        return NULL;
    }

    char line[LINE_BUF];

    /* Skip header row */
    if (fgets(line, LINE_BUF, f) == NULL) {
        fprintf(stderr, "loader: empty file or missing header\n");
        free(events);
        fclose(f);
        return NULL;
    }

    uint32_t row_idx = 0U;    /* 0-based data row index */
    uint32_t n_loaded = 0U;   /* entries successfully parsed */

    while (fgets(line, LINE_BUF, f) != NULL) {
        /* Skip blank lines */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
            row_idx++;
            continue;
        }

        event_t ev;
        if (!parse_row(line, row_idx, &ev)) {
            /* Skip bad rows; row_idx still advances */
            row_idx++;
            continue;
        }

        /* Grow buffer if needed */
        if (n_loaded == capacity) {
            if (capacity > (uint32_t)UINT32_MAX / 2U) {
                fprintf(stderr, "loader: event array overflow at row %u\n",
                        row_idx);
                free(events);
                fclose(f);
                return NULL;
            }
            uint32_t  new_cap  = capacity * 2U;
            event_t  *new_buf  = (event_t *)realloc(events,
                                     (size_t)new_cap * sizeof(event_t));
            if (new_buf == NULL) {
                fprintf(stderr, "loader: realloc failed at capacity %u\n",
                        new_cap);
                free(events);
                fclose(f);
                return NULL;
            }
            events   = new_buf;
            capacity = new_cap;
        }

        events[n_loaded] = ev;
        n_loaded++;
        row_idx++;
    }

    fclose(f);

    if (n_loaded == 0U) {
        fprintf(stderr, "loader: no valid events parsed from '%s'\n", path);
        free(events);
        return NULL;
    }

    *count = n_loaded;
    return events;
}
