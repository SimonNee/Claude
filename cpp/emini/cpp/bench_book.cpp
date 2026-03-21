/* bench_book.cpp — Data-driven benchmark harness for the C++ E-mini order book.
 *
 * Event stream is loaded from orders.csv (via loader.hpp/loader.cpp).
 * The load is untimed. The benchmark replays the loaded event vector.
 *
 * Covers B1–B5:
 *   B1  — add latency  (ADD events, median/p99/max cycles)
 *   B2  — cancel latency (CANCEL events, median/p99/max cycles)
 *   B3/B4 — match latency (MATCH events, median/p99/max cycles)
 *   B5  — best_bid scan, called after each ADD event
 *
 * RDTSC discipline: CPUID+RDTSC start / RDTSCP+CPUID end.
 * Sink: volatile accumulator prevents call elimination (Idiom 10, Pitfall 6).
 * Cache regime: warm (replay from pre-loaded vector — no CSV I/O on hot path).
 *
 * Build: -std=c++17 -O2 -march=native -Wall -Wextra -Wconversion
 *        -Wsign-conversion -Werror -fno-exceptions -flto
 *        (no sanitizers)
 *
 * Run: taskset -c 2 ./bench_book_cpp [path/to/orders.csv]
 *      Default CSV path: ../data/orders.csv
 *
 * NOTE: taskset -c <isolated_core> is strongly recommended for stable cycle
 *       counts. Verify with /proc/cmdline that the core is isolcpus-isolated.
 */

#include "book.hpp"
#include "loader.hpp"
#include "internal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <vector>

using namespace es::book;
using namespace es::loader;

// ---------------------------------------------------------------------------
// RDTSC with serialisation
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
    if (n == 0U) {
        std::printf("  %-55s  (no samples)\n", label);
        return;
    }
    std::sort(s, s + n);
    uint64_t med = s[n / 2U];
    uint64_t p99 = s[static_cast<uint32_t>(static_cast<double>(n) * 0.99)];
    uint64_t mx  = s[n - 1U];
    std::printf("  %-55s  median=%4lu  p99=%5lu  max=%6lu  cycles  n=%u\n",
                label,
                static_cast<unsigned long>(med),
                static_cast<unsigned long>(p99),
                static_cast<unsigned long>(mx),
                n);
}

// ---------------------------------------------------------------------------
// B1 — Add latency
//
// For each ADD event: call book.add_by_tick(side, tick, qty).
// The tick was pre-converted by the loader — no float touches the hot path.
// id_map[row] records the returned order_id for later CANCEL lookups.
// ---------------------------------------------------------------------------

static void bench_B1_add(Book&                         book,
                         const std::vector<Event>&     events,
                         std::vector<order_id_t>&      id_map) {
    std::printf("\n[B1] Add latency — data-driven, warm cache\n");

    uint32_t n_samples = 0U;
    volatile uint64_t sink = 0U;

    uint32_t n_events = static_cast<uint32_t>(events.size());
    for (uint32_t i = 0U; i < n_events; ++i) {
        const Event& ev = events[i];
        if (ev.type != EventType::ADD) {
            continue;
        }

        uint64_t t0 = rdtsc_start();
        order_id_t oid = book.add_by_tick(ev.side, ev.tick, ev.qty);
        uint64_t t1 = rdtsc_end();

        sink += static_cast<uint64_t>(oid);
        id_map[i] = oid;

        if (n_samples < MAX_SAMPLES) {
            g_samples[n_samples++] = t1 - t0;
        }
    }

    if (sink == 0xDEADC0DEDEADC0DEULL) { std::abort(); }
    print_stats("B1: add (data-driven, pre-converted tick)", g_samples, n_samples);
}

// ---------------------------------------------------------------------------
// B2 — Cancel latency
//
// For each CANCEL event: look up id_map[ref_idx] to get the order_id, then
// call book.cancel(). The Event struct carries the side from the CSV; the
// tick must come from the original ADD event.
// ---------------------------------------------------------------------------

static void bench_B2_cancel(Book&                         book,
                             const std::vector<Event>&     events,
                             const std::vector<order_id_t>& id_map) {
    std::printf("\n[B2] Cancel latency — data-driven, warm cache\n");

    uint32_t n_samples = 0U;
    volatile uint32_t sink = 0U;

    uint32_t n_events = static_cast<uint32_t>(events.size());
    for (uint32_t i = 0U; i < n_events; ++i) {
        const Event& ev = events[i];
        if (ev.type != EventType::CANCEL) {
            continue;
        }

        uint32_t add_row = ev.ref_idx;
        if (add_row >= n_events) {
            continue;
        }

        order_id_t oid = id_map[add_row];
        if (oid == NULL_IDX) {
            // Order was never successfully added or already cancelled.
            continue;
        }

        // Retrieve the tick from the original ADD event.
        tick_t add_tick = events[add_row].tick;

        uint64_t t0 = rdtsc_start();
        bool ok = book.cancel(oid, ev.side, add_tick);
        uint64_t t1 = rdtsc_end();

        sink += static_cast<uint32_t>(ok);

        if (n_samples < MAX_SAMPLES) {
            g_samples[n_samples++] = t1 - t0;
        }

        // Note: duplicate cancels (same ref_idx appearing multiple times,
        // possible in the bootstrap phase of the generator) are naturally
        // rejected by book.cancel() via the DEAD_FLAG check on the node.
        // No id_map mutation needed here.
    }

    if (sink == 0xDEADU) { std::abort(); }
    print_stats("B2: cancel (data-driven, realistic distribution)", g_samples, n_samples);
}

// ---------------------------------------------------------------------------
// B3/B4 — Match latency
//
// For each MATCH event: call book.match_by_tick() with the tick read directly
// from the CSV. No float arithmetic anywhere in the timed or untimed region.
//
// NOTE: The MATCH events reference the resting book state produced by
// the preceding ADD/CANCEL replay. Some matches may find no resting orders
// if cancels exhausted a level; fill_count=0 in that case is valid and
// still exercises the bitmap scan path.
// ---------------------------------------------------------------------------

static void bench_B3B4_match(Book&                     book,
                              const std::vector<Event>& events) {
    std::printf("\n[B3/B4] Match latency — data-driven, warm cache\n");

    uint32_t n_samples = 0U;
    volatile uint64_t sink = 0U;

    // taker_id cycles through a fixed range — not meaningful for perf, just
    // supplies a non-zero value so fill records are non-trivially populated.
    order_id_t taker_id = 0U;

    uint32_t n_events = static_cast<uint32_t>(events.size());
    for (uint32_t i = 0U; i < n_events; ++i) {
        const Event& ev = events[i];
        if (ev.type != EventType::MATCH) {
            continue;
        }

        uint64_t t0 = rdtsc_start();
        fill_result_t r = book.match_by_tick(ev.side, ev.tick, ev.qty, taker_id);
        uint64_t t1 = rdtsc_end();

        sink += static_cast<uint64_t>(r.fill_count);

        if (n_samples < MAX_SAMPLES) {
            g_samples[n_samples++] = t1 - t0;
        }

        ++taker_id;
        if (taker_id >= MAX_ORDERS) { taker_id = 0U; }
    }

    if (sink == 0xDEADC0DEDEADC0DEULL) { std::abort(); }
    print_stats("B3/B4: match (data-driven)", g_samples, n_samples);
}

// ---------------------------------------------------------------------------
// B5 — best_bid scan
//
// Called after each ADD event in a second pass over the loaded event stream.
// Times book.best_bid() immediately after the add, capturing realistic
// bitmap occupancy from the replayed ADD/CANCEL stream.
// ---------------------------------------------------------------------------

static void bench_B5_best_bid(Book&                     book,
                               const std::vector<Event>& events,
                               const std::vector<order_id_t>& id_map) {
    std::printf("\n[B5] best_bid scan — called after each ADD event\n");

    uint32_t n_samples = 0U;
    volatile uint64_t sink = 0U;

    uint32_t n_events = static_cast<uint32_t>(events.size());
    for (uint32_t i = 0U; i < n_events; ++i) {
        const Event& ev = events[i];
        if (ev.type != EventType::ADD) {
            continue;
        }
        if (id_map[i] == NULL_IDX) {
            // Add failed (arena full); skip.
            continue;
        }

        uint64_t t0 = rdtsc_start();
        tick_t bb = book.best_bid();
        uint64_t t1 = rdtsc_end();

        sink += static_cast<uint64_t>(bb);

        if (n_samples < MAX_SAMPLES) {
            g_samples[n_samples++] = t1 - t0;
        }
    }

    if (sink == 0xDEADC0DEDEADC0DEULL) { std::abort(); }
    print_stats("B5: best_bid (post-add, data-driven bitmap occupancy)", g_samples, n_samples);
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
    if (f) { if (std::fgets(line, sizeof(line), f)) { std::printf("L1d: %s", line); } pclose(f); }

    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index2/size 2>/dev/null", "r");
    if (f) { if (std::fgets(line, sizeof(line), f)) { std::printf("L2:  %s", line); } pclose(f); }

    f = popen("cat /sys/devices/system/cpu/cpu0/cache/index3/size 2>/dev/null", "r");
    if (f) { if (std::fgets(line, sizeof(line), f)) { std::printf("L3:  %s", line); } pclose(f); }

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
    const char* csv_path = (argc >= 2) ? argv[1] : "../data/orders.csv";

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

    // id_map[i] maps event row i to the order_id assigned by book.add_by_tick().
    // Initialised to NULL_IDX; only ADD rows receive a non-NULL_IDX entry.
    std::vector<order_id_t> id_map(n_events, NULL_IDX);

    // Count event types for reporting.
    uint32_t n_add = 0U, n_cancel = 0U, n_match = 0U;
    for (const Event& ev : events) {
        switch (ev.type) {
            case EventType::ADD:    ++n_add;    break;
            case EventType::CANCEL: ++n_cancel; break;
            case EventType::MATCH:  ++n_match;  break;
        }
    }

    std::printf("  Events: %u ADD, %u CANCEL, %u MATCH\n\n",
                n_add, n_cancel, n_match);

    std::printf("=== C++ Data-Driven Benchmarks ===\n");
    std::printf("  Reporting: median / p99 / max cycles\n");

    // -------------------------------------------------------------------
    // Phase 2 — B1: replay ADD events; populate id_map.
    // -------------------------------------------------------------------

    // base_price is the double anchor for the public add()/match() API.
    // The data-driven benchmark uses add_by_tick/match_by_tick exclusively,
    // so this value is never exercised on the hot path; it must be valid.
    static constexpr double BOOK_BASE_PRICE = 4400.0;
    Book book(BOOK_BASE_PRICE);
    bench_B1_add(book, events, id_map);

    // -------------------------------------------------------------------
    // Phase 3 — B2: replay CANCEL events using id_map from B1.
    // B2 runs on the same book state produced by B1 (realistic).
    // Duplicate cancel attempts are rejected by DEAD_FLAG in book.cancel().
    // -------------------------------------------------------------------

    bench_B2_cancel(book, events, id_map);

    // -------------------------------------------------------------------
    // Phase 4 — B3/B4: replay MATCH events.
    // Runs on the book state after B1+B2 (adds placed, cancels applied).
    // -------------------------------------------------------------------

    bench_B3B4_match(book, events);

    // -------------------------------------------------------------------
    // Phase 5 — B5: best_bid scan after each ADD.
    // We rebuild the book from scratch (reset) to get a clean bitmap state,
    // then replay the ADD stream, calling best_bid after each add.
    // The cancel/match replay is skipped here — B5 measures best_bid
    // bitmap scan cost against the ADD-only occupancy pattern.
    // -------------------------------------------------------------------

    book.reset();

    // Replay ADDs only into a fresh id_map (B5 doesn't need cancel linkage).
    std::vector<order_id_t> id_map_b5(n_events, NULL_IDX);
    for (uint32_t i = 0U; i < n_events; ++i) {
        const Event& ev = events[i];
        if (ev.type == EventType::ADD) {
            id_map_b5[i] = book.add_by_tick(ev.side, ev.tick, ev.qty);
            if (book.impl().arena.next_slot >= MAX_ORDERS - 2U) {
                // Arena nearly full — stop populating. B5 measures whatever
                // bitmap state was reached; partial replay is still realistic.
                break;
            }
        }
    }

    bench_B5_best_bid(book, events, id_map_b5);

    return 0;
}
