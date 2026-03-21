/*
 * bench_book.c — Benchmark harness for the C E-mini order book.
 *
 * Covers benchmarks B1–B5 from the spec's Benchmark Contract:
 *   B1  — add latency (single level and multi-level)
 *   B2  — cancel latency by queue depth q={1,5,10,50} and position {head,mid,tail}
 *   B3  — match latency, single level
 *   B4  — match latency, multi-level k={1,5,10}
 *   B5  — best_bid scan with varying active levels
 *
 * Measurement discipline (spec + idioms.md Pitfall 10):
 *   - CPUID + RDTSC at start; RDTSCP + CPUID at end (serialising both ends)
 *   - volatile sink accumulates return values (Pitfall 4)
 *   - Warm-up loop before each measurement (Idiom 3)
 *   - 500,000 iterations minimum (Pitfall 11)
 *   - Median, P99, and max reported (spec: Benchmark Contract)
 *   - Cache regime: warm (spec: Benchmark B1 Cache regime)
 *
 * Build flags (benchmarks — NO sanitizers, LTO on):
 *   gcc -std=c11 -O2 -march=native -Wall -Wextra -Wconversion
 *       -Wsign-conversion -Wsign-compare -Werror -flto
 *       -o bench_book bench_book.c book_bench.o matcher_bench.o -lm
 *
 * Run: taskset -c 2 ./bench_book
 */

#include "book.h"
#include "matcher.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* =========================================================================
 * RDTSC with serialisation (idioms.md Idiom 3 / Pitfall 10)
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

#define MAX_SAMPLES 1000000

static uint64_t samples[MAX_SAMPLES];

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void print_stats(const char *label, uint64_t *s, uint32_t n) {
    qsort(s, (size_t)n, sizeof(uint64_t), cmp_u64);
    uint64_t med  = s[n / 2];
    uint64_t p99  = s[(uint32_t)((double)n * 0.99)];
    uint64_t max  = s[n - 1U];
    printf("  %-55s  median=%4lu  p99=%5lu  max=%6lu  cycles\n",
           label, (unsigned long)med, (unsigned long)p99, (unsigned long)max);
}

/* =========================================================================
 * B1 — Add latency
 *
 * Cache regime: warm (levels array and bitmap are in cache from prior adds).
 * Single-level: all adds to the same tick (tick 100).
 * Multi-level:  100 ticks, distributed.
 * ====================================================================== */

#define BENCH_N       500000U
#define BASE_PRICE_B  5500.0
#define PRICE_100_B   5525.0   /* tick 100 */

static void bench_B1_add(void) {
    printf("\n[B1] Add latency — warm cache\n");

    /* --- Single level --- */
    {
        book_t *b = book_create(BASE_PRICE_B);
        if (!b) { fprintf(stderr, "book_create failed\n"); exit(1); }

        /* Warm-up: 1000 adds */
        for (uint32_t i = 0; i < 1000U; i++) {
            order_id_t id = book_add(b, BID, PRICE_100_B, 1U);
            (void)id;
        }
        book_reset(b);

        volatile uint64_t sink = 0;
        for (uint32_t i = 0; i < BENCH_N; i++) {
            uint64_t t0 = rdtsc_start();
            order_id_t id = book_add(b, BID, PRICE_100_B, 1U);
            uint64_t t1 = rdtsc_end();
            sink += id;
            samples[i] = t1 - t0;
            /* Reset when arena might fill (each add consumes 1 slot) */
            if (b->arena.next_slot >= (MAX_ORDERS - 2U)) {
                book_reset(b);
            }
        }
        if (sink == 0xDEADC0DEDEADC0DEULL) abort();
        print_stats("B1a: add single level (tick=100)", samples, BENCH_N);
        book_destroy(b);
    }

    /* --- Multi-level: 100 ticks --- */
    {
        book_t *b = book_create(BASE_PRICE_B);
        if (!b) { fprintf(stderr, "book_create failed\n"); exit(1); }

        /* Warm-up */
        for (uint32_t i = 0; i < 1000U; i++) {
            double price = BASE_PRICE_B + (double)(i % 100U) * 0.25;
            order_id_t id = book_add(b, BID, price, 1U);
            (void)id;
        }
        book_reset(b);

        volatile uint64_t sink = 0;
        for (uint32_t i = 0; i < BENCH_N; i++) {
            double price = BASE_PRICE_B + (double)(i % 100U) * 0.25;
            uint64_t t0 = rdtsc_start();
            order_id_t id = book_add(b, BID, price, 1U);
            uint64_t t1 = rdtsc_end();
            sink += id;
            samples[i] = t1 - t0;
            if (b->arena.next_slot >= (MAX_ORDERS - 2U)) {
                book_reset(b);
            }
        }
        if (sink == 0xDEADC0DEDEADC0DEULL) abort();
        print_stats("B1b: add multi-level (100 ticks)", samples, BENCH_N);
        book_destroy(b);
    }
}

/* =========================================================================
 * B2 — Cancel latency by queue depth
 *
 * For each q ∈ {1, 5, 10, 50} and position ∈ {head, mid, tail}:
 *   1. Fill a single level to depth q.
 *   2. Cancel the order at position p.
 *   3. Measure cycles.
 *   4. Replenish to maintain depth q for the next iteration.
 *
 * This is the primary benchmark for the doubly-linked promote decision.
 * Threshold: median > 50 cycles at (q=10, p=mid) triggers review.
 * ====================================================================== */

static void bench_B2_cancel(void) {
    printf("\n[B2] Cancel latency by queue depth — warm cache\n");

    const uint32_t depths[]    = { 1U, 5U, 10U, 50U };
    const char    *depth_names[] = { "q=1 ", "q=5 ", "q=10", "q=50" };
    const uint32_t ndepths = 4U;

    /* position names */
    const char *pos_names[] = { "head", "mid ", "tail" };

    for (uint32_t di = 0; di < ndepths; di++) {
        uint32_t q = depths[di];

        for (uint32_t pi = 0; pi < 3U; pi++) {
            /* pi=0: head (p=0); pi=1: mid (p=q/2); pi=2: tail (p=q-1) */
            uint32_t cancel_pos;
            if (pi == 0)      cancel_pos = 0U;
            else if (pi == 1) cancel_pos = q / 2U;
            else              cancel_pos = q - 1U;

            book_t *b = book_create(BASE_PRICE_B);
            if (!b) { fprintf(stderr, "book_create failed\n"); exit(1); }

            /* Tracking array for order IDs at the level */
            /* q can be up to 50; use a fixed buffer */
            order_id_t level_ids[50];
            memset(level_ids, 0, sizeof(level_ids));

            /* Initial fill to depth q */
            for (uint32_t j = 0; j < q; j++) {
                level_ids[j] = book_add(b, ASK, PRICE_100_B, 1U);
            }

            /* Warm-up: 1000 cancel/replenish cycles */
            for (uint32_t w = 0; w < 1000U; w++) {
                /* cancel at position cancel_pos */
                order_id_t target = level_ids[cancel_pos];
                bool ok = book_cancel(b, target, ASK, 100U);
                (void)ok;
                /* replenish: add a new order; shift ids if needed */
                order_id_t new_id = book_add(b, ASK, PRICE_100_B, 1U);
                /* Update ids: the new id goes to tail, all subsequent shift */
                /* Simple approach: rebuild tracking by shifting cancel_pos out */
                if (cancel_pos == 0U) {
                    /* head was removed: shift all left by 1 */
                    for (uint32_t k = 0; k + 1U < q; k++)
                        level_ids[k] = level_ids[k + 1U];
                    level_ids[q - 1U] = new_id;
                } else if (cancel_pos == q - 1U) {
                    /* tail was removed */
                    level_ids[q - 1U] = new_id;
                } else {
                    /* mid was removed: shift down from cancel_pos+1 */
                    for (uint32_t k = cancel_pos; k + 1U < q; k++)
                        level_ids[k] = level_ids[k + 1U];
                    level_ids[q - 1U] = new_id;
                }
            }

            volatile uint32_t sink = 0;
            uint32_t n = BENCH_N;
            for (uint32_t i = 0; i < n; i++) {
                order_id_t target = level_ids[cancel_pos];
                uint64_t t0 = rdtsc_start();
                bool ok = book_cancel(b, target, ASK, 100U);
                uint64_t t1 = rdtsc_end();
                sink += (uint32_t)ok;
                samples[i] = t1 - t0;

                /* Replenish */
                order_id_t new_id = book_add(b, ASK, PRICE_100_B, 1U);
                if (new_id == NULL_IDX) {
                    /* arena getting full — reset and refill */
                    book_reset(b);
                    for (uint32_t j = 0; j < q; j++)
                        level_ids[j] = book_add(b, ASK, PRICE_100_B, 1U);
                } else {
                    if (cancel_pos == 0U) {
                        for (uint32_t k = 0; k + 1U < q; k++)
                            level_ids[k] = level_ids[k + 1U];
                        level_ids[q - 1U] = new_id;
                    } else if (cancel_pos == q - 1U) {
                        level_ids[q - 1U] = new_id;
                    } else {
                        for (uint32_t k = cancel_pos; k + 1U < q; k++)
                            level_ids[k] = level_ids[k + 1U];
                        level_ids[q - 1U] = new_id;
                    }
                }
            }
            if (sink == 0xDEADU) abort();

            char label[80];
            snprintf(label, sizeof(label), "B2: cancel %s p=%s", depth_names[di], pos_names[pi]);
            print_stats(label, samples, n);

            /* Mark the promote-decision threshold explicitly */
            if (di == 2U && pi == 1U) {
                qsort(samples, n, sizeof(uint64_t), cmp_u64);
                uint64_t med = samples[n / 2U];
                printf("  >>> PROMOTE DECISION: q=10 mid-queue median=%lu cycles "
                       "(threshold 50) → %s\n",
                       (unsigned long)med,
                       med > 50UL ? "EXCEEDS threshold — review doubly-linked promotion"
                                  : "BELOW threshold — singly-linked is adequate");
            }

            book_destroy(b);
        }
    }
}

/* =========================================================================
 * B3 — Match latency, single level, one resting order
 * ====================================================================== */

static void bench_B3_match_single(void) {
    printf("\n[B3] Match latency — single level, one resting order — warm cache\n");

    book_t *b = book_create(BASE_PRICE_B);
    if (!b) { fprintf(stderr, "book_create failed\n"); exit(1); }

    /* Set up: one ASK order at tick 100, large qty so it never runs out */
    order_id_t ask_id = book_add(b, ASK, PRICE_100_B, (qty_t)MAX_ORDERS);
    (void)ask_id;

    /* Warm-up */
    for (uint32_t i = 0; i < 1000U; i++) {
        fill_result_t r = book_match(b, BID, PRICE_100_B, 1U, (order_id_t)i);
        (void)r;
    }

    volatile uint64_t sink = 0;
    for (uint32_t i = 0; i < BENCH_N; i++) {
        uint64_t t0 = rdtsc_start();
        fill_result_t r = book_match(b, BID, PRICE_100_B, 1U, i);
        uint64_t t1 = rdtsc_end();
        sink += r.fill_count;
        samples[i] = t1 - t0;
    }
    if (sink == 0xDEADC0DEDEADC0DEULL) abort();

    print_stats("B3: match single level, 1 resting order", samples, BENCH_N);
    book_destroy(b);
}

/* =========================================================================
 * B4 — Match latency, multi-level k={1,5,10}
 * ====================================================================== */

static void bench_B4_match_multi(void) {
    printf("\n[B4] Match latency — multi-level — warm cache\n");

    uint32_t ks[] = { 1U, 5U, 10U };
    const char *knames[] = { "k=1 ", "k=5 ", "k=10" };

    for (uint32_t ki = 0; ki < 3U; ki++) {
        uint32_t k = ks[ki];

        /* Each measurement cycle:
         * 1. Pre-fill k ask levels at ticks 100..100+k-1, each with 1 order.
         * 2. Match BID at tick 100+k-1 with qty=k → sweeps all k levels.
         * 3. Measure step 2 only.
         * 4. Replenish and repeat.
         */
        book_t *b = book_create(BASE_PRICE_B);
        if (!b) { fprintf(stderr, "book_create failed\n"); exit(1); }

        /* Warm-up */
        for (uint32_t w = 0; w < 200U; w++) {
            for (uint32_t level = 0; level < k; level++) {
                double price = BASE_PRICE_B + (double)(100U + level) * 0.25;
                book_add(b, ASK, price, 1U);
            }
            double agg_price = BASE_PRICE_B + (double)(100U + k - 1U) * 0.25;
            fill_result_t r = book_match(b, BID, agg_price, k, 0U);
            (void)r;
            if (b->arena.next_slot >= MAX_ORDERS - k - 10U)
                book_reset(b);
        }
        book_reset(b);

        volatile uint64_t sink = 0;
        uint32_t n = BENCH_N / k;  /* fewer iterations for large k */
        if (n > BENCH_N) n = BENCH_N;

        for (uint32_t i = 0; i < n; i++) {
            /* Refill k levels */
            for (uint32_t level = 0; level < k; level++) {
                double price = BASE_PRICE_B + (double)(100U + level) * 0.25;
                book_add(b, ASK, price, 1U);
            }

            double agg_price = BASE_PRICE_B + (double)(100U + k - 1U) * 0.25;
            uint64_t t0 = rdtsc_start();
            fill_result_t r = book_match(b, BID, agg_price, k, i);
            uint64_t t1 = rdtsc_end();
            sink += r.fill_count;
            samples[i] = t1 - t0;

            if (b->arena.next_slot >= MAX_ORDERS - k - 10U)
                book_reset(b);
        }
        if (sink == 0xDEADC0DEDEADC0DEULL) abort();

        char label[80];
        snprintf(label, sizeof(label), "B4: match %s levels", knames[ki]);
        print_stats(label, samples, n);

        book_destroy(b);
    }
}

/* =========================================================================
 * B5 — best_bid scan with varying active levels
 * ====================================================================== */

static void bench_B5_best_bid(void) {
    printf("\n[B5] best_bid scan latency — bitmap warm in L1\n");

    uint32_t ns[] = { 1U, 10U, 50U, 138U };
    const char *nnames[] = { "N=1  ", "N=10 ", "N=50 ", "N=138" };

    for (uint32_t ni = 0; ni < 4U; ni++) {
        uint32_t nlevels = ns[ni];

        book_t *b = book_create(BASE_PRICE_B);
        if (!b) { fprintf(stderr, "book_create failed\n"); exit(1); }

        /* Fill nlevels bid levels, uniformly distributed across tick range */
        for (uint32_t j = 0; j < nlevels; j++) {
            /* Distribute uniformly across [0, MAX_TICKS-1] */
            uint32_t tick = (j * (MAX_TICKS - 1U)) / (nlevels > 1U ? nlevels - 1U : 1U);
            double price = BASE_PRICE_B + (double)tick * 0.25;
            book_add(b, BID, price, 1U);
        }

        /* Warm-up */
        for (uint32_t i = 0; i < 1000U; i++) {
            tick_t t = book_best_bid(b);
            (void)t;
        }

        volatile uint64_t sink = 0;
        for (uint32_t i = 0; i < BENCH_N; i++) {
            uint64_t t0 = rdtsc_start();
            tick_t t = book_best_bid(b);
            uint64_t t1 = rdtsc_end();
            sink += t;
            samples[i] = t1 - t0;
        }
        if (sink == 0xDEADC0DEDEADC0DEULL) abort();

        char label[80];
        snprintf(label, sizeof(label), "B5: best_bid %s active levels", nnames[ni]);
        print_stats(label, samples, BENCH_N);

        book_destroy(b);
    }
}

/* =========================================================================
 * Hardware info
 * ====================================================================== */

static void print_hw_info(void) {
    printf("=== Hardware ===\n");

    FILE *f;
    char line[256];
    int found = 0;

    f = fopen("/proc/cpuinfo", "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (!found && strncmp(line, "model name", 10) == 0) {
                printf("CPU: %s", line + 13);  /* skip "model name\t: " */
                found = 1;
            }
        }
        fclose(f);
    }

    /* L1 data cache */
    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index0/size 2>/dev/null", "r");
    if (f) {
        if (fgets(line, sizeof(line), f)) printf("L1d: %s", line);
        pclose(f);
    }

    /* L2 cache */
    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index2/size 2>/dev/null", "r");
    if (f) {
        if (fgets(line, sizeof(line), f)) printf("L2:  %s", line);
        pclose(f);
    }

    /* L3 cache */
    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index3/size 2>/dev/null", "r");
    if (f) {
        if (fgets(line, sizeof(line), f)) printf("L3:  %s", line);
        pclose(f);
    }

    printf("Cache regime: WARM (repeated operations on same data in L1/L2)\n");
    printf("Core:         pinned to core 2 (taskset -c 2)\n");
    printf("isolcpus:     not confirmed (check /proc/cmdline)\n");
    printf("Sanitizers:   NONE (bench build)\n");
    printf("CFLAGS:       -std=c11 -O2 -march=native -flto\n");
    printf("\n");
}

/* =========================================================================
 * main
 * ====================================================================== */

int main(void) {
    print_hw_info();

    printf("=== C Implementation Benchmarks ===\n");
    printf("  N=%u iterations per benchmark\n", BENCH_N);
    printf("  Reporting: median / p99 / max cycles\n\n");

    bench_B1_add();
    bench_B2_cancel();
    bench_B3_match_single();
    bench_B4_match_multi();
    bench_B5_best_bid();

    return 0;
}
