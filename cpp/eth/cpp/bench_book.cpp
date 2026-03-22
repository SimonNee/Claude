/* bench_book.cpp — Data-driven benchmark harness for the ETH/USDT L2 orderbook.
 *
 * Event stream is loaded from events.csv (via loader.hpp/loader.cpp).
 * The load is untimed. The benchmark replays the loaded event vector.
 *
 * Covers B1–B4:
 *   B1 — UPSERT latency  (UPSERT events, median/p99/max cycles)
 *   B2 — DELETE latency  (DELETE events, median/p99/max cycles;
 *                         many will be no-ops — measures realistic fast path)
 *   B3 — best_bid latency (after each UPSERT, on a fresh book)
 *   B4 — best_ask latency (same pass as B3)
 *
 * RDTSC discipline: CPUID+RDTSC start / RDTSCP+CPUID end.
 * Sink: volatile accumulator prevents call elimination (Idiom 10, Pitfall 6).
 * Cache regime: WARM (replay from pre-loaded vector — no CSV I/O on hot path).
 *
 * Build: -std=c++17 -O2 -march=native -Wall -Wextra -Wconversion
 *        -Wsign-conversion -Werror -fno-exceptions -flto
 *        (no sanitizers)
 *
 * Run: taskset -c 2 ./bench_book_cpp [path/to/events.csv]
 *      Default CSV path: ../data/events.csv
 *
 * NOTE: taskset -c <isolated_core> is strongly recommended for stable cycle
 *       counts. Verify with /proc/cmdline that the core is isolcpus-isolated.
 */

#include "book.hpp"
#include "loader.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <vector>

using namespace eth::book;
using namespace eth::loader;

// ---------------------------------------------------------------------------
// RDTSC with serialisation
//
// rdtsc_start:  CPUID (full pipeline drain) → RDTSC
// rdtsc_end:    RDTSCP (serialising read) → CPUID (prevent reorder of following code)
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
// Statistics helpers
// ---------------------------------------------------------------------------

static constexpr uint32_t MAX_SAMPLES = 1'000'000U;

// g_samples is module-scope, not stack-allocated — avoids stack overflow on
// systems with 8 MB stack limits (1M uint64_t = 8 MB exactly at the limit).
static uint64_t g_samples[MAX_SAMPLES];

static void print_stats(const char* label, uint64_t* s, uint32_t n) {
    if (n == 0U) {
        std::printf("  %-55s  (no samples)\n", label);
        return;
    }
    std::sort(s, s + n);
    uint64_t med = s[n / 2U];
    // p99 index: floor(0.99 * n). Using integer arithmetic to avoid float.
    // n <= 1,000,000; n*99 <= 99,000,000 < UINT32_MAX — no overflow.
    // Cast to uint32_t for the subscript to match the array index type.
    uint32_t p99_idx = (n * 99U) / 100U;
    uint64_t p99 = s[p99_idx];
    uint64_t mx  = s[n - 1U];
    std::printf("  %-55s  median=%4lu  p99=%5lu  max=%6lu  cycles  n=%u\n",
                label,
                static_cast<unsigned long>(med),
                static_cast<unsigned long>(p99),
                static_cast<unsigned long>(mx),
                n);
}

// ---------------------------------------------------------------------------
// Compute window_base_tick from the loaded event set.
//
// Centres the window on the mean tick of all loaded events.
// Overflow note: sum_ticks accumulates uint32_t ticks into uint64_t.
// Max tick = ~1,000,000,000 (ETH at $10M); 1M events → sum < 10^15 < UINT64_MAX.
// ---------------------------------------------------------------------------

static uint64_t compute_base_tick(const std::vector<Event>& events) noexcept {
    uint64_t sum_ticks = 0U;
    for (const Event& ev : events) {
        sum_ticks += static_cast<uint64_t>(ev.tick);
    }
    uint64_t n_ev      = static_cast<uint64_t>(events.size());
    uint64_t mean_tick = sum_ticks / n_ev;
    uint64_t base_tick = (mean_tick > static_cast<uint64_t>(WINDOW_SIZE) / 2U)
                         ? mean_tick - static_cast<uint64_t>(WINDOW_SIZE) / 2U
                         : 0ULL;
    return base_tick;
}

// ---------------------------------------------------------------------------
// B1 — UPSERT latency
//
// Replay all events in order. Time only UPSERT events (skip DELETEs).
// Call book.upsert_by_tick(ev.side, ev.tick, ev.qty).
// Volatile sink on return value.
//
// After B1 completes, the book holds all UPSERT state.
// B2 runs on this same book state (do not reset between B1 and B2).
// ---------------------------------------------------------------------------

static void bench_B1_upsert(Book& book, const std::vector<Event>& events) {
    std::printf("\n[B1] UPSERT latency — data-driven, warm cache\n");

    uint32_t n_samples = 0U;
    volatile uint64_t sink = 0U;

    uint32_t n_events = static_cast<uint32_t>(events.size());
    for (uint32_t i = 0U; i < n_events; ++i) {
        const Event& ev = events[i];
        if (ev.type != EventType::UPSERT) {
            continue;
        }

        uint64_t t0 = rdtsc_start();
        bool ok = book.upsert_by_tick(ev.side, ev.tick, ev.qty);
        uint64_t t1 = rdtsc_end();

        // Volatile sink (Idiom 10): accumulate return value to prevent
        // the compiler from eliminating the call under LTO.
        sink += static_cast<uint64_t>(ok);

        if (n_samples < MAX_SAMPLES) {
            g_samples[n_samples++] = t1 - t0;
        }
    }

    // Poison guard: forces sink to be observable.
    if (sink == 0xDEADC0DEDEADC0DEULL) { std::abort(); }

    print_stats("B1: upsert (data-driven, pre-converted tick)", g_samples, n_samples);
}

// ---------------------------------------------------------------------------
// B2 — DELETE latency
//
// Continue from the book state after B1 (same book, no reset).
// Replay all events again. Time only DELETE events.
// Call book.upsert_by_tick(ev.side, ev.tick, 0ULL).
//
// Many DELETE events will be no-ops (level already absent or never set).
// This is intentional: it measures realistic DELETE latency including the
// fast path where the bitmap bit is already clear.
// ---------------------------------------------------------------------------

static void bench_B2_delete(Book& book, const std::vector<Event>& events) {
    std::printf("\n[B2] DELETE latency — data-driven, warm cache (includes fast-path no-ops)\n");

    uint32_t n_samples = 0U;
    volatile uint64_t sink = 0U;

    uint32_t n_events = static_cast<uint32_t>(events.size());
    for (uint32_t i = 0U; i < n_events; ++i) {
        const Event& ev = events[i];
        if (ev.type != EventType::DELETE) {
            continue;
        }

        uint64_t t0 = rdtsc_start();
        bool ok = book.upsert_by_tick(ev.side, ev.tick, 0ULL);
        uint64_t t1 = rdtsc_end();

        sink += static_cast<uint64_t>(ok);

        if (n_samples < MAX_SAMPLES) {
            g_samples[n_samples++] = t1 - t0;
        }
    }

    if (sink == 0xDEADC0DEDEADC0DEULL) { std::abort(); }

    print_stats("B2: delete (data-driven, realistic distribution)", g_samples, n_samples);
}

// ---------------------------------------------------------------------------
// B3 + B4 — best_bid and best_ask latency
//
// Reset the book, set window base again.
// Replay UPSERT events only to build a realistic book state.
// After each UPSERT, call best_bid() (B3) and best_ask() (B4) and time both.
// Both measurements run in a single pass: one loop, two timings per event.
// ---------------------------------------------------------------------------

static void bench_B3B4_best(Book& book, uint64_t base_tick,
                             const std::vector<Event>& events) {
    std::printf("\n[B3/B4] best_bid / best_ask latency — post-upsert, warm cache\n");

    // Reset book and re-apply the computed base tick.
    book.reset(base_tick);

    uint32_t n_bid_samples = 0U;
    uint32_t n_ask_samples = 0U;

    // Two separate sample arrays: B3 writes into g_samples directly;
    // B4 needs its own storage. Declare B4 storage as a second static array.
    // Static: avoids stack overflow (8 MB × 2 would exceed typical stacks).
    static uint64_t g_samples_b4[MAX_SAMPLES];

    volatile uint64_t sink_bid = 0U;
    volatile uint64_t sink_ask = 0U;

    uint32_t n_events = static_cast<uint32_t>(events.size());
    for (uint32_t i = 0U; i < n_events; ++i) {
        const Event& ev = events[i];
        if (ev.type != EventType::UPSERT) {
            continue;
        }

        // Apply the upsert to keep book state current. Untimed.
        // The [[nodiscard]] return is intentionally discarded here; cast to void.
        static_cast<void>(book.upsert_by_tick(ev.side, ev.tick, ev.qty));

        // B3: time best_bid().
        {
            uint64_t t0 = rdtsc_start();
            tick_t bb   = book.best_bid();
            uint64_t t1 = rdtsc_end();
            sink_bid += static_cast<uint64_t>(bb);
            if (n_bid_samples < MAX_SAMPLES) {
                g_samples[n_bid_samples++] = t1 - t0;
            }
        }

        // B4: time best_ask().
        {
            uint64_t t0 = rdtsc_start();
            tick_t ba   = book.best_ask();
            uint64_t t1 = rdtsc_end();
            sink_ask += static_cast<uint64_t>(ba);
            if (n_ask_samples < MAX_SAMPLES) {
                g_samples_b4[n_ask_samples++] = t1 - t0;
            }
        }
    }

    // Poison guards.
    if (sink_bid == 0xDEADC0DEDEADC0DEULL) { std::abort(); }
    if (sink_ask == 0xDEADC0DEDEADC0DEULL) { std::abort(); }

    print_stats("B3: best_bid (post-upsert, data-driven occupancy)", g_samples,    n_bid_samples);
    print_stats("B4: best_ask (post-upsert, data-driven occupancy)", g_samples_b4, n_ask_samples);
}

// ---------------------------------------------------------------------------
// Hardware info
// ---------------------------------------------------------------------------

static void print_hw_info() {
    std::printf("=== Hardware ===\n");

    FILE* fp;
    char  line[256];
    bool  found = false;

    fp = std::fopen("/proc/cpuinfo", "r");
    if (fp) {
        while (std::fgets(line, static_cast<int>(sizeof(line)), fp)) {
            if (!found && std::strncmp(line, "model name", 10) == 0) {
                // Line format: "model name\t: <value>\n"; skip "model name\t: " (13 chars).
                std::printf("CPU: %s", line + 13);
                found = true;
            }
        }
        std::fclose(fp);
    }

    fp = popen("cat /sys/devices/system/cpu/cpu0/cache/index0/size 2>/dev/null", "r");
    if (fp) {
        if (std::fgets(line, static_cast<int>(sizeof(line)), fp)) {
            std::printf("L1d: %s", line);
        }
        pclose(fp);
    }

    fp = popen("cat /sys/devices/system/cpu/cpu0/cache/index2/size 2>/dev/null", "r");
    if (fp) {
        if (std::fgets(line, static_cast<int>(sizeof(line)), fp)) {
            std::printf("L2:  %s", line);
        }
        pclose(fp);
    }

    fp = popen("cat /sys/devices/system/cpu/cpu0/cache/index3/size 2>/dev/null", "r");
    if (fp) {
        if (std::fgets(line, static_cast<int>(sizeof(line)), fp)) {
            std::printf("L3:  %s", line);
        }
        pclose(fp);
    }

    std::printf("Cache regime: WARM (replay from pre-loaded vector)\n");
    std::printf("Core:         pin with taskset -c <isolated_core>\n");
    std::printf("isolcpus:     check /proc/cmdline\n");
    std::printf("Sanitizers:   NONE (bench build)\n");
    std::printf("CXXFLAGS:     -std=c++17 -O2 -march=native -flto\n\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    const char* csv_path = (argc >= 2) ? argv[1] : "../data/events.csv";

    print_hw_info();

    // -------------------------------------------------------------------
    // Phase 1 — Load CSV (untimed).
    // -------------------------------------------------------------------

    std::printf("=== Loading CSV: %s ===\n", csv_path);
    std::vector<Event> events = load_csv(csv_path);

    if (events.empty()) {
        std::fprintf(stderr, "bench: no events loaded — aborting\n");
        return 1;
    }

    const uint32_t n_events = static_cast<uint32_t>(events.size());

    // Count event types for reporting.
    uint32_t n_upsert = 0U;
    uint32_t n_delete = 0U;
    for (const Event& ev : events) {
        if (ev.type == EventType::UPSERT) {
            ++n_upsert;
        } else {
            ++n_delete;
        }
    }

    std::printf("  Events: %u total (%u UPSERT, %u DELETE)\n\n",
                n_events, n_upsert, n_delete);

    // -------------------------------------------------------------------
    // Compute window base tick from the loaded events.
    // Centre the window on the mean tick of all events.
    // -------------------------------------------------------------------

    uint64_t base_tick = compute_base_tick(events);
    std::printf("  Window base tick: %llu\n\n",
                static_cast<unsigned long long>(base_tick));

    std::printf("=== C++ Data-Driven Benchmarks ===\n");
    std::printf("  Reporting: median / p99 / max cycles\n");

    // -------------------------------------------------------------------
    // Phase 2 — Construct book with computed base tick.
    // -------------------------------------------------------------------

    Book book(base_tick);

    // -------------------------------------------------------------------
    // Phase 3 — B1: replay UPSERT events; book accumulates state.
    // -------------------------------------------------------------------

    bench_B1_upsert(book, events);

    // -------------------------------------------------------------------
    // Phase 4 — B2: replay DELETE events on same book state from B1.
    // Many will be no-ops — measures realistic fast path latency.
    // -------------------------------------------------------------------

    bench_B2_delete(book, events);

    // -------------------------------------------------------------------
    // Phase 5 — B3 + B4: reset book, replay UPSERT only, time queries.
    // bench_B3B4_best calls book.reset() internally.
    // -------------------------------------------------------------------

    bench_B3B4_best(book, base_tick, events);

    return 0;
}
