/*
 * bench_book.c — Data-driven benchmark harness for the C E-mini order book.
 *
 * Benchmarks B1–B5 from the spec's Benchmark Contract, driven by real
 * order flow loaded from orders.csv via loader.h / loader.c:
 *
 *   B1 — add latency:     time each book_add_tick for ADD events
 *   B2 — cancel latency:  time each book_cancel for CANCEL events
 *   B3/B4 — match:        time each book_match_tick for MATCH events
 *   B5 — best_bid scan:   call book_best_bid after each ADD, timed separately
 *
 * The CSV tick column is a plain integer. No float reconstruction occurs
 * anywhere in this file. book_add_tick and book_match_tick accept tick_t
 * directly, bypassing price_to_tick() entirely.
 *
 * Measurement discipline:
 *   - CPUID + RDTSC at start; RDTSCP + CPUID at end (serialising both ends)
 *   - volatile sink accumulates return values (prevents dead-code elimination)
 *   - Warm-up pass over the event stream before any measurement
 *   - Median, P99, and max reported
 *   - id_map[row_idx] → order_id_t maps ADD row indices to book-assigned IDs
 *     for CANCEL lookups
 *
 * The loader is NOT timed. It runs once at startup before any benchmark.
 *
 * Build flags (benchmarks — NO sanitizers, LTO on):
 *   gcc -std=c11 -O2 -march=native -Wall -Wextra -Wconversion
 *       -Wsign-conversion -Wsign-compare -Werror -flto
 *       -o bench_book bench_book.c book.o matcher.o loader.o -lm
 *
 * Run: taskset -c 2 ./bench_book
 *
 * Note: for accurate results, pin to an isolated core:
 *   taskset -c 2 ./bench_book
 *   Verify: cat /proc/cmdline | grep isolcpus
 */

#include "book.h"
#include "loader.h"
#include "matcher.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * BOOK_BASE_PRICE — passed to book_create for the float-API path.
 * The benchmark uses book_add_tick / book_match_tick exclusively, so this
 * value is stored in book->base_price but never used on the hot path.
 * It must be a finite positive double; 4400.0 matches the CSV's tick origin.
 */
static const double BOOK_BASE_PRICE = 4400.0;

/* =========================================================================
 * RDTSC with serialisation
 * ====================================================================== */

static inline uint64_t rdtsc_start(void) {
    uint32_t hi, lo;
    __asm__ volatile (
        "cpuid\n\t"
        "rdtsc\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        : "=r"(hi), "=r"(lo)
        :: "%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline uint64_t rdtsc_end(void) {
    uint32_t hi, lo;
    __asm__ volatile (
        "rdtscp\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "cpuid\n\t"
        : "=r"(hi), "=r"(lo)
        :: "%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* =========================================================================
 * Statistics helpers
 * ====================================================================== */

/*
 * MAX_SAMPLES: the CSV has 1,000,000 data rows; we need one slot per event
 * of each type. ADD and CANCEL each have up to ~833,330 events. MATCH has
 * up to ~83,337. Allocate for the maximum event count.
 */
#define MAX_SAMPLES 1000000U

static uint64_t samples[MAX_SAMPLES];

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void print_stats(const char *label, uint64_t *s, uint32_t n) {
    if (n == 0U) {
        printf("  %-55s  (no events)\n", label);
        return;
    }
    qsort(s, (size_t)n, sizeof(uint64_t), cmp_u64);
    uint64_t med = s[n / 2U];
    /*
     * P99 index: avoid float-to-integer cast under -Wconversion.
     * Use integer arithmetic: p99_idx = n - n/100 - 1, clamped to [0, n-1].
     * n - n/100 gives the 99th percentile boundary without any floating point.
     */
    uint32_t p99_off = n / 100U;
    uint32_t p99_idx = (n > p99_off + 1U) ? (n - p99_off - 1U) : (n - 1U);
    uint64_t p99 = s[p99_idx];
    uint64_t max = s[n - 1U];
    printf("  %-55s  median=%4lu  p99=%5lu  max=%6lu  cycles\n",
           label, (unsigned long)med, (unsigned long)p99, (unsigned long)max);
}

/* =========================================================================
 * Benchmark state
 *
 * id_map maps event row_idx → order_id_t assigned by book_add.
 * Initialised to NULL_IDX. Only ADD events write into it.
 * CANCEL events read from it using event.ref_idx.
 * ====================================================================== */

static order_id_t *id_map;   /* heap-allocated, count entries */

/*
 * setup_id_map — allocate and initialise the id_map array.
 * Called once before any benchmark. count is the total number of CSV events.
 */
static int setup_id_map(uint32_t count)
{
    id_map = (order_id_t *)malloc((size_t)count * sizeof(order_id_t));
    if (id_map == NULL) {
        fprintf(stderr, "bench: malloc failed for id_map (%u entries)\n", count);
        return 0;
    }
    /* NULL_IDX == UINT32_MAX; we can memset with 0xFF bytes to fill all fields */
    memset(id_map, 0xFF, (size_t)count * sizeof(order_id_t));
    return 1;
}

/*
 * reset_id_map — re-fill id_map with NULL_IDX sentinels between bench passes.
 */
static void reset_id_map(uint32_t count)
{
    memset(id_map, 0xFF, (size_t)count * sizeof(order_id_t));
}

/* =========================================================================
 * Warm-up pass
 *
 * Replay the full event stream once, untimed, to bring the book and id_map
 * into a realistic state and warm the caches. The book is reset afterward
 * so each benchmark starts from a clean state with a warmed cache hierarchy.
 * ====================================================================== */

static void warmup(book_t *book, const event_t *events, uint32_t count)
{
    reset_id_map(count);
    book_reset(book);

    for (uint32_t i = 0U; i < count; i++) {
        const event_t *ev = &events[i];

        if (ev->type == EV_ADD) {
            order_id_t oid = book_add_tick(book, ev->side, ev->tick, ev->qty);
            id_map[i] = oid;

            /* Arena reset guard: if the arena is nearly full, reset and clear map */
            if (book->arena.next_slot >= (MAX_ORDERS - 2U)) {
                book_reset(book);
                reset_id_map(count);
            }

        } else if (ev->type == EV_CANCEL) {
            uint32_t ref = ev->ref_idx;
            if (ref < count && id_map[ref] != NULL_IDX) {
                order_id_t oid = id_map[ref];
                /*
                 * book_cancel requires side and tick of the ADD.
                 * The ADD event at ref carries the correct side and tick.
                 */
                (void)book_cancel(book, oid, events[ref].side, events[ref].tick);
                id_map[ref] = NULL_IDX;
            }

        } else if (ev->type == EV_MATCH) {
            fill_result_t r = book_match_tick(book, ev->side, ev->tick, ev->qty,
                                              (order_id_t)i);
            (void)r;
        }
    }

    book_reset(book);
    reset_id_map(count);
}

/* =========================================================================
 * B1 — Add latency
 *
 * Times each book_add call for ADD events in the loaded stream.
 * The book is kept live between calls (not reset per-iteration) to reflect
 * realistic in-session state. Arena reset guard prevents exhaustion.
 * ====================================================================== */

static void bench_B1_add(book_t *book, const event_t *events, uint32_t count)
{
    printf("\n[B1] Add latency — data-driven\n");

    reset_id_map(count);
    book_reset(book);

    uint32_t n_samples = 0U;
    volatile uint64_t sink = 0U;

    for (uint32_t i = 0U; i < count; i++) {
        const event_t *ev = &events[i];
        if (ev->type != EV_ADD)
            continue;

        uint64_t t0 = rdtsc_start();
        order_id_t oid = book_add_tick(book, ev->side, ev->tick, ev->qty);
        uint64_t t1 = rdtsc_end();

        sink += (uint64_t)oid;
        id_map[i] = oid;

        if (n_samples < MAX_SAMPLES) {
            samples[n_samples] = t1 - t0;
            n_samples++;
        }

        if (book->arena.next_slot >= (MAX_ORDERS - 2U)) {
            book_reset(book);
            reset_id_map(count);
        }
    }

    if (sink == 0xDEADC0DEDEADC0DEULL) abort();
    print_stats("B1: add latency (CSV event stream)", samples, n_samples);
}

/* =========================================================================
 * B2 — Cancel latency
 *
 * Times each book_cancel call for CANCEL events in the loaded stream.
 * Requires a prior ADD pass to populate id_map. We replay the full stream
 * untimed first to build a realistic book state, then replay timing only the
 * CANCEL calls.
 *
 * Strategy: two-pass.
 *   Pass 1 (untimed): replay all ADD events, recording id_map.
 *   Pass 2 (timed):   replay CANCEL events only, looking up id_map[ref_idx].
 * ====================================================================== */

static void bench_B2_cancel(book_t *book, const event_t *events, uint32_t count)
{
    printf("\n[B2] Cancel latency — data-driven\n");

    /* Pass 1: populate id_map by replaying ADDs */
    reset_id_map(count);
    book_reset(book);

    for (uint32_t i = 0U; i < count; i++) {
        const event_t *ev = &events[i];
        if (ev->type != EV_ADD)
            continue;
        order_id_t oid = book_add_tick(book, ev->side, ev->tick, ev->qty);
        id_map[i] = oid;

        if (book->arena.next_slot >= (MAX_ORDERS - 2U)) {
            book_reset(book);
            reset_id_map(count);
        }
    }

    /* Pass 2: time the cancels */
    uint32_t n_samples = 0U;
    volatile uint32_t sink = 0U;

    for (uint32_t i = 0U; i < count; i++) {
        const event_t *ev = &events[i];
        if (ev->type != EV_CANCEL)
            continue;

        uint32_t ref = ev->ref_idx;
        if (ref >= count || id_map[ref] == NULL_IDX)
            continue;   /* ref points to an ADD that was never recorded */

        order_id_t oid = id_map[ref];

        uint64_t t0 = rdtsc_start();
        bool ok = book_cancel(book, oid, events[ref].side, events[ref].tick);
        uint64_t t1 = rdtsc_end();

        sink += (uint32_t)ok;
        id_map[ref] = NULL_IDX;   /* mark consumed */

        if (n_samples < MAX_SAMPLES) {
            samples[n_samples] = t1 - t0;
            n_samples++;
        }
    }

    if (sink == 0xDEADU) abort();
    print_stats("B2: cancel latency (CSV event stream)", samples, n_samples);
}

/* =========================================================================
 * B3/B4 — Match latency
 *
 * Times each book_match call for MATCH events in the loaded stream.
 * A prior ADD pass is needed to ensure the book has resting liquidity.
 * We replay ADDs untimed, then time the MATCHes.
 * ====================================================================== */

static void bench_B3B4_match(book_t *book, const event_t *events, uint32_t count)
{
    printf("\n[B3/B4] Match latency — data-driven\n");

    /* Populate the book with all ADD events first */
    reset_id_map(count);
    book_reset(book);

    for (uint32_t i = 0U; i < count; i++) {
        const event_t *ev = &events[i];
        if (ev->type != EV_ADD)
            continue;
        order_id_t oid = book_add_tick(book, ev->side, ev->tick, ev->qty);
        id_map[i] = oid;

        if (book->arena.next_slot >= (MAX_ORDERS - 2U)) {
            book_reset(book);
            reset_id_map(count);
        }
    }

    /* Time the match calls */
    uint32_t n_samples = 0U;
    volatile uint64_t sink = 0U;

    for (uint32_t i = 0U; i < count; i++) {
        const event_t *ev = &events[i];
        if (ev->type != EV_MATCH)
            continue;

        uint64_t t0 = rdtsc_start();
        fill_result_t r = book_match_tick(book, ev->side, ev->tick, ev->qty,
                                          (order_id_t)i);
        uint64_t t1 = rdtsc_end();

        sink += (uint64_t)r.fill_count;

        if (n_samples < MAX_SAMPLES) {
            samples[n_samples] = t1 - t0;
            n_samples++;
        }
    }

    if (sink == 0xDEADC0DEDEADC0DEULL) abort();
    print_stats("B3/B4: match latency (CSV event stream)", samples, n_samples);
}

/* =========================================================================
 * B5 — best_bid scan
 *
 * Calls book_best_bid after each ADD event, timing the scan separately.
 * Structure unchanged from the synthetic version.
 * ====================================================================== */

static void bench_B5_best_bid(book_t *book, const event_t *events, uint32_t count)
{
    printf("\n[B5] best_bid scan latency — data-driven\n");

    reset_id_map(count);
    book_reset(book);

    uint32_t n_samples = 0U;
    volatile uint64_t sink = 0U;

    for (uint32_t i = 0U; i < count; i++) {
        const event_t *ev = &events[i];
        if (ev->type != EV_ADD)
            continue;

        /* Add untimed (we time the scan, not the add) */
        order_id_t oid = book_add_tick(book, ev->side, ev->tick, ev->qty);
        id_map[i] = oid;

        if (book->arena.next_slot >= (MAX_ORDERS - 2U)) {
            book_reset(book);
            reset_id_map(count);
        }

        /* Time the best_bid scan */
        uint64_t t0 = rdtsc_start();
        tick_t best = book_best_bid(book);
        uint64_t t1 = rdtsc_end();

        sink += (uint64_t)best;

        if (n_samples < MAX_SAMPLES) {
            samples[n_samples] = t1 - t0;
            n_samples++;
        }
    }

    if (sink == 0xDEADC0DEDEADC0DEULL) abort();
    print_stats("B5: best_bid after each ADD (CSV event stream)", samples, n_samples);
}

/* =========================================================================
 * Hardware info
 * ====================================================================== */

/*
 * LINE_HW is the fgets buffer size for hardware info lines. Declared as a
 * plain int literal so it can be passed to fgets without a cast.
 */
#define LINE_HW 256

static void print_hw_info(void) {
    printf("=== Hardware ===\n");

    FILE *f;
    char line[LINE_HW];
    int found = 0;

    f = fopen("/proc/cpuinfo", "r");
    if (f) {
        while (fgets(line, LINE_HW, f)) {
            if (!found && strncmp(line, "model name", 10) == 0) {
                printf("CPU: %s", line + 13);
                found = 1;
            }
        }
        fclose(f);
    }

    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index0/size 2>/dev/null", "r");
    if (f) {
        if (fgets(line, LINE_HW, f)) printf("L1d: %s", line);
        pclose(f);
    }

    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index2/size 2>/dev/null", "r");
    if (f) {
        if (fgets(line, LINE_HW, f)) printf("L2:  %s", line);
        pclose(f);
    }

    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index3/size 2>/dev/null", "r");
    if (f) {
        if (fgets(line, LINE_HW, f)) printf("L3:  %s", line);
        pclose(f);
    }

    printf("Cache regime: WARM (event stream replayed after warm-up pass)\n");
    printf("Core:         pin with taskset -c 2 ./bench_book\n");
    printf("isolcpus:     not confirmed (check /proc/cmdline)\n");
    printf("Sanitizers:   NONE (bench build)\n");
    printf("CFLAGS:       -std=c11 -O2 -march=native -flto\n");
    printf("\n");
}

/* =========================================================================
 * main
 * ====================================================================== */

int main(int argc, char *argv[])
{
    /*
     * CSV path: first argument if provided, otherwise the default relative
     * to the source tree. Use an absolute path for reproducibility.
     */
    const char *csv_path = (argc > 1) ? argv[1]
                                       : "../data/orders.csv";

    print_hw_info();

    /* ---- Load CSV (not timed) ---- */
    printf("Loading event stream from: %s\n", csv_path);
    uint32_t count = 0U;
    event_t *events = events_load(csv_path, &count);
    if (events == NULL) {
        fprintf(stderr, "bench: failed to load CSV '%s'\n", csv_path);
        return 1;
    }
    printf("Loaded %u events\n\n", count);

    /* ---- id_map (not timed) ---- */
    if (!setup_id_map(count)) {
        free(events);
        return 1;
    }

    /* ---- Book (not timed) ---- */
    book_t *book = book_create(BOOK_BASE_PRICE);
    if (book == NULL) {
        fprintf(stderr, "bench: book_create failed\n");
        free(id_map);
        free(events);
        return 1;
    }

    printf("=== C Implementation Benchmarks ===\n");
    printf("  Event stream: %u rows\n", count);
    printf("  Reporting: median / p99 / max cycles\n\n");

    /* ---- Warm-up pass (not timed) ---- */
    printf("Running warm-up pass...\n");
    warmup(book, events, count);
    printf("Warm-up complete.\n");

    /* ---- Benchmarks ---- */
    bench_B1_add(book, events, count);
    bench_B2_cancel(book, events, count);
    bench_B3B4_match(book, events, count);
    bench_B5_best_bid(book, events, count);

    /* ---- Cleanup ---- */
    book_destroy(book);
    free(id_map);
    free(events);

    return 0;
}
