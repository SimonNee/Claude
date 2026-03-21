/* bench_book.cpp — Benchmark harness for the C++ E-mini order book.
 *
 * Mirrors bench_book.c exactly in structure so C vs C++ results are
 * directly comparable (spec: Benchmark B6 — head-to-head).
 *
 * Covers B1–B5:
 *   B1  — add latency (single-level and 100-level)
 *   B2  — cancel latency by depth q={1,5,10,50} × position {head,mid,tail}
 *   B3  — match latency, single level
 *   B4  — match latency, multi-level k={1,5,10}
 *   B5  — best_bid scan, N={1,10,50,138} active levels
 *
 * RDTSC discipline: CPUID+RDTSC start / RDTSCP+CPUID end (idioms.md Idiom 3).
 * Sink: volatile prevents elimination (Pitfall 4).
 * Cache regime: warm.
 *
 * Build: -std=c++17 -O2 -march=native -Wall -Wextra -Wconversion
 *        -Wsign-conversion -Werror -fno-exceptions -flto
 *        (no sanitizers)
 *
 * Run: taskset -c 2 ./bench_book_cpp
 */

#include "book.hpp"
#include "internal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace es::book;
using namespace es::book::internal;

// ---------------------------------------------------------------------------
// RDTSC with serialisation (idioms.md Pitfall 10)
// ---------------------------------------------------------------------------

static inline uint64_t rdtsc_start() noexcept {
    uint32_t hi, lo;
    __asm__ volatile (
        "cpuid\n\t"
        "rdtsc\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        : "=r"(hi), "=r"(lo)
        :: "%rax", "%rbx", "%rcx", "%rdx");
    return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
}

static inline uint64_t rdtsc_end() noexcept {
    uint32_t hi, lo;
    __asm__ volatile (
        "rdtscp\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "cpuid\n\t"
        : "=r"(hi), "=r"(lo)
        :: "%rax", "%rbx", "%rcx", "%rdx");
    return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

static constexpr uint32_t MAX_SAMPLES = 1'000'000U;
static uint64_t g_samples[MAX_SAMPLES];

static void print_stats(const char* label, uint64_t* s, uint32_t n) {
    std::sort(s, s + n);
    uint64_t med = s[n / 2U];
    uint64_t p99 = s[static_cast<uint32_t>(static_cast<double>(n) * 0.99)];
    uint64_t mx  = s[n - 1U];
    std::printf("  %-55s  median=%4lu  p99=%5lu  max=%6lu  cycles\n",
                label,
                static_cast<unsigned long>(med),
                static_cast<unsigned long>(p99),
                static_cast<unsigned long>(mx));
}

// ---------------------------------------------------------------------------
// Constants — same as C harness for identical data layout comparison
// ---------------------------------------------------------------------------

static constexpr double BASE_PRICE_B = 5500.0;
static constexpr double PRICE_100_B  = 5525.0;  // tick 100
static constexpr uint32_t BENCH_N    = 500'000U;

// ---------------------------------------------------------------------------
// B1 — Add latency
// ---------------------------------------------------------------------------

static void bench_B1_add() {
    std::printf("\n[B1] Add latency — warm cache\n");

    // Single level
    {
        Book b(BASE_PRICE_B);

        // Warm-up
        for (uint32_t i = 0U; i < 1000U; ++i) {
            auto id = b.add(side_t::BID, PRICE_100_B, 1U);
            (void)id;
        }
        b.reset();

        volatile uint64_t sink = 0;
        for (uint32_t i = 0U; i < BENCH_N; ++i) {
            uint64_t t0 = rdtsc_start();
            auto id = b.add(side_t::BID, PRICE_100_B, 1U);
            uint64_t t1 = rdtsc_end();
            sink += static_cast<uint64_t>(id);
            g_samples[i] = t1 - t0;
            if (b.impl().arena.next_slot >= MAX_ORDERS - 2U) {
                b.reset();
            }
        }
        if (sink == 0xDEADC0DEDEADC0DEULL) std::abort();
        print_stats("B1a: add single level (tick=100)", g_samples, BENCH_N);
    }

    // Multi-level: 100 ticks
    {
        Book b(BASE_PRICE_B);

        for (uint32_t i = 0U; i < 1000U; ++i) {
            double price = BASE_PRICE_B + static_cast<double>(i % 100U) * 0.25;
            auto id = b.add(side_t::BID, price, 1U);
            (void)id;
        }
        b.reset();

        volatile uint64_t sink = 0;
        for (uint32_t i = 0U; i < BENCH_N; ++i) {
            double price = BASE_PRICE_B + static_cast<double>(i % 100U) * 0.25;
            uint64_t t0 = rdtsc_start();
            auto id = b.add(side_t::BID, price, 1U);
            uint64_t t1 = rdtsc_end();
            sink += static_cast<uint64_t>(id);
            g_samples[i] = t1 - t0;
            if (b.impl().arena.next_slot >= MAX_ORDERS - 2U) {
                b.reset();
            }
        }
        if (sink == 0xDEADC0DEDEADC0DEULL) std::abort();
        print_stats("B1b: add multi-level (100 ticks)", g_samples, BENCH_N);
    }
}

// ---------------------------------------------------------------------------
// B2 — Cancel latency by queue depth
// ---------------------------------------------------------------------------

static void bench_B2_cancel() {
    std::printf("\n[B2] Cancel latency by queue depth — warm cache\n");

    constexpr uint32_t depths[] = { 1U, 5U, 10U, 50U };
    const char* depth_names[]   = { "q=1 ", "q=5 ", "q=10", "q=50" };
    const char* pos_names[]     = { "head", "mid ", "tail" };

    for (uint32_t di = 0; di < 4U; ++di) {
        uint32_t q = depths[di];

        for (uint32_t pi = 0; pi < 3U; ++pi) {
            uint32_t cancel_pos;
            if (pi == 0)      cancel_pos = 0U;
            else if (pi == 1) cancel_pos = q / 2U;
            else              cancel_pos = q - 1U;

            Book b(BASE_PRICE_B);

            order_id_t level_ids[50];
            std::memset(level_ids, 0, sizeof(level_ids));

            // Initial fill to depth q
            for (uint32_t j = 0; j < q; ++j) {
                level_ids[j] = b.add(side_t::ASK, PRICE_100_B, 1U);
            }

            // Helper lambda to update level_ids after a cancel+replenish
            auto update_ids = [&](uint32_t pos, order_id_t new_id) {
                if (pos == 0U) {
                    for (uint32_t k = 0; k + 1U < q; ++k)
                        level_ids[k] = level_ids[k + 1U];
                    level_ids[q - 1U] = new_id;
                } else if (pos == q - 1U) {
                    level_ids[q - 1U] = new_id;
                } else {
                    for (uint32_t k = pos; k + 1U < q; ++k)
                        level_ids[k] = level_ids[k + 1U];
                    level_ids[q - 1U] = new_id;
                }
            };

            // Warm-up: 1000 cancel/replenish cycles
            for (uint32_t w = 0; w < 1000U; ++w) {
                order_id_t target = level_ids[cancel_pos];
                bool ok = b.cancel(target, side_t::ASK, 100U);
                (void)ok;
                auto new_id = b.add(side_t::ASK, PRICE_100_B, 1U);
                update_ids(cancel_pos, new_id);
            }

            volatile uint32_t sink = 0;
            for (uint32_t i = 0; i < BENCH_N; ++i) {
                order_id_t target = level_ids[cancel_pos];
                uint64_t t0 = rdtsc_start();
                bool ok = b.cancel(target, side_t::ASK, 100U);
                uint64_t t1 = rdtsc_end();
                sink += static_cast<uint32_t>(ok);
                g_samples[i] = t1 - t0;

                auto new_id = b.add(side_t::ASK, PRICE_100_B, 1U);
                if (new_id == NULL_IDX) {
                    // Arena full — reset and refill
                    b.reset();
                    for (uint32_t j = 0; j < q; ++j)
                        level_ids[j] = b.add(side_t::ASK, PRICE_100_B, 1U);
                } else {
                    update_ids(cancel_pos, new_id);
                }
            }
            if (sink == 0xDEADU) std::abort();

            char label[80];
            std::snprintf(label, sizeof(label), "B2: cancel %s p=%s", depth_names[di], pos_names[pi]);
            print_stats(label, g_samples, BENCH_N);

            // Promote-decision report for q=10 mid
            if (di == 2U && pi == 1U) {
                std::sort(g_samples, g_samples + BENCH_N);
                uint64_t med = g_samples[BENCH_N / 2U];
                std::printf("  >>> PROMOTE DECISION: q=10 mid-queue median=%lu cycles "
                            "(threshold 50) → %s\n",
                            static_cast<unsigned long>(med),
                            med > 50UL
                              ? "EXCEEDS threshold — review doubly-linked promotion"
                              : "BELOW threshold — singly-linked is adequate");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// B3 — Match latency, single level
// ---------------------------------------------------------------------------

static void bench_B3_match_single() {
    std::printf("\n[B3] Match latency — single level, one resting order — warm cache\n");

    Book b(BASE_PRICE_B);
    // One ASK with huge qty so it never drains
    auto ask_id = b.add(side_t::ASK, PRICE_100_B, MAX_ORDERS);
    (void)ask_id;

    // Warm-up
    for (uint32_t i = 0U; i < 1000U; ++i) {
        auto r = b.match(side_t::BID, PRICE_100_B, 1U, i);
        (void)r;
    }

    volatile uint64_t sink = 0;
    for (uint32_t i = 0U; i < BENCH_N; ++i) {
        uint64_t t0 = rdtsc_start();
        auto r = b.match(side_t::BID, PRICE_100_B, 1U, i);
        uint64_t t1 = rdtsc_end();
        sink += r.fill_count;
        g_samples[i] = t1 - t0;
    }
    if (sink == 0xDEADC0DEDEADC0DEULL) std::abort();

    print_stats("B3: match single level, 1 resting order", g_samples, BENCH_N);
}

// ---------------------------------------------------------------------------
// B4 — Match latency, multi-level k={1,5,10}
// ---------------------------------------------------------------------------

static void bench_B4_match_multi() {
    std::printf("\n[B4] Match latency — multi-level — warm cache\n");

    constexpr uint32_t ks[]  = { 1U, 5U, 10U };
    const char* knames[]     = { "k=1 ", "k=5 ", "k=10" };

    for (uint32_t ki = 0; ki < 3U; ++ki) {
        uint32_t k = ks[ki];
        Book b(BASE_PRICE_B);

        // Warm-up
        for (uint32_t w = 0; w < 200U; ++w) {
            for (uint32_t level = 0; level < k; ++level) {
                double price = BASE_PRICE_B + static_cast<double>(100U + level) * 0.25;
                auto id = b.add(side_t::ASK, price, 1U);
                (void)id;
            }
            double agg = BASE_PRICE_B + static_cast<double>(100U + k - 1U) * 0.25;
            auto r = b.match(side_t::BID, agg, k, 0U);
            (void)r;
            if (b.impl().arena.next_slot >= MAX_ORDERS - k - 10U)
                b.reset();
        }
        b.reset();

        uint32_t n = BENCH_N / k;
        if (n > BENCH_N) n = BENCH_N;

        volatile uint64_t sink = 0;
        for (uint32_t i = 0; i < n; ++i) {
            for (uint32_t level = 0; level < k; ++level) {
                double price = BASE_PRICE_B + static_cast<double>(100U + level) * 0.25;
                auto id = b.add(side_t::ASK, price, 1U);
                (void)id;
            }
            double agg = BASE_PRICE_B + static_cast<double>(100U + k - 1U) * 0.25;

            uint64_t t0 = rdtsc_start();
            auto r = b.match(side_t::BID, agg, k, i);
            uint64_t t1 = rdtsc_end();
            sink += r.fill_count;
            g_samples[i] = t1 - t0;

            if (b.impl().arena.next_slot >= MAX_ORDERS - k - 10U)
                b.reset();
        }
        if (sink == 0xDEADC0DEDEADC0DEULL) std::abort();

        char label[80];
        std::snprintf(label, sizeof(label), "B4: match %s levels", knames[ki]);
        print_stats(label, g_samples, n);
    }
}

// ---------------------------------------------------------------------------
// B5 — best_bid scan
// ---------------------------------------------------------------------------

static void bench_B5_best_bid() {
    std::printf("\n[B5] best_bid scan latency — bitmap warm in L1\n");

    constexpr uint32_t ns[]  = { 1U, 10U, 50U, 138U };
    const char* nnames[]     = { "N=1  ", "N=10 ", "N=50 ", "N=138" };

    for (uint32_t ni = 0; ni < 4U; ++ni) {
        uint32_t nlevels = ns[ni];
        Book b(BASE_PRICE_B);

        for (uint32_t j = 0; j < nlevels; ++j) {
            uint32_t tick = (j * (MAX_TICKS - 1U)) / (nlevels > 1U ? nlevels - 1U : 1U);
            double price = BASE_PRICE_B + static_cast<double>(tick) * 0.25;
            auto id = b.add(side_t::BID, price, 1U);
            (void)id;
        }

        // Warm-up
        for (uint32_t i = 0; i < 1000U; ++i) {
            auto t = b.best_bid();
            (void)t;
        }

        volatile uint64_t sink = 0;
        for (uint32_t i = 0; i < BENCH_N; ++i) {
            uint64_t t0 = rdtsc_start();
            auto t = b.best_bid();
            uint64_t t1 = rdtsc_end();
            sink += static_cast<uint64_t>(t);
            g_samples[i] = t1 - t0;
        }
        if (sink == 0xDEADC0DEDEADC0DEULL) std::abort();

        char label[80];
        std::snprintf(label, sizeof(label), "B5: best_bid %s active levels", nnames[ni]);
        print_stats(label, g_samples, BENCH_N);
    }
}

// ---------------------------------------------------------------------------
// Hardware info
// ---------------------------------------------------------------------------

static void print_hw_info() {
    std::printf("=== Hardware ===\n");

    FILE* f;
    char line[256];
    bool found = false;

    f = std::fopen("/proc/cpuinfo", "r");
    if (f) {
        while (std::fgets(line, sizeof(line), f)) {
            if (!found && std::strncmp(line, "model name", 10) == 0) {
                std::printf("CPU: %s", line + 13);
                found = true;
            }
        }
        std::fclose(f);
    }

    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index0/size 2>/dev/null", "r");
    if (f) { if (std::fgets(line, sizeof(line), f)) std::printf("L1d: %s", line); pclose(f); }

    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index2/size 2>/dev/null", "r");
    if (f) { if (std::fgets(line, sizeof(line), f)) std::printf("L2:  %s", line); pclose(f); }

    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index3/size 2>/dev/null", "r");
    if (f) { if (std::fgets(line, sizeof(line), f)) std::printf("L3:  %s", line); pclose(f); }

    std::printf("Cache regime: WARM (repeated operations on same data in L1/L2)\n");
    std::printf("Core:         pinned to core 2 (taskset -c 2)\n");
    std::printf("isolcpus:     not confirmed (check /proc/cmdline)\n");
    std::printf("Sanitizers:   NONE (bench build)\n");
    std::printf("CXXFLAGS:     -std=c++17 -O2 -march=native -flto\n\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    print_hw_info();

    std::printf("=== C++ Implementation Benchmarks ===\n");
    std::printf("  N=%u iterations per benchmark\n", BENCH_N);
    std::printf("  Reporting: median / p99 / max cycles\n\n");

    bench_B1_add();
    bench_B2_cancel();
    bench_B3_match_single();
    bench_B4_match_multi();
    bench_B5_best_bid();

    return 0;
}
