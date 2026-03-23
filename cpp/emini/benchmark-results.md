# E-mini Orderbook — Benchmark Results

> Historical runs (Runs 1–10 including C vs C++ comparisons): `benchmark-results-archive.md`

---

## Hardware & Build

| Field | Value |
|---|---|
| CPU | Intel Core i9-10980HK @ 2.40 GHz |
| L1d / L1i | 32 KB / 32 KB |
| L2 | 256 KB (unified) |
| L3 | 16 MB |
| Compiler | GCC 12.2.0 (Debian) |
| C++ flags | `-std=c++17 -O2 -march=native -flto -fno-exceptions` |
| Core | 2 (`taskset -c 2`) |
| `isolcpus` | Active (`isolcpus=2` in `/etc/default/grub`) |

---

## Reproducing

```bash
# Generate workload data (requires q)
cd cpp/emini/data
q generate.q                                          # mixed
q generate.q -cancels 30 -output orders_cancel_heavy.csv
q generate.q -theta 0.0001 -drift 0.005 -output orders_monotonic.csv

# Build and run
cd cpp/emini/cpp
make bench
taskset -c 2 ./bench_book_cpp ../data/orders.csv
taskset -c 2 ./bench_book_cpp ../data/orders_cancel_heavy.csv
taskset -c 2 ./bench_book_cpp ../data/orders_monotonic.csv
```

---

## Current Numbers (Run 10 — combined O(1) best bid/ask)

Implementation: `best_tick` field load (fast path) + two-level hierarchical bitmap fallback on level drain.

| Benchmark | Mixed | Cancel-heavy (30:1) | Monotonic |
|---|---|---|---|
| Add | 38 cy | 38 cy | 38 cy |
| Cancel | 24 cy | 24 cy | 24 cy |
| Match | 24 cy | 24 cy | 26 cy |
| best_bid | 20 cy | 20 cy | 21 cy |

All medians. Warm cache, core 2 pinned, `isolcpus=2` active.

---

## Design Decisions Behind These Numbers

| Operation | Mechanism | Cost driver |
|---|---|---|
| Add | Arena bump + queue enqueue + bitmap/summary set + best_tick compare | ~38 cy — two structure updates |
| Cancel | DEAD_FLAG check + queue remove + conditional bitmap/summary clear | ~24 cy — maintenance overhead vs baseline 22 cy |
| Match | best_tick field load + level drain loop + hierarchical fallback on empty | ~24 cy — hierarchical scan eliminates 138-word sweep |
| best_bid | Single field load from `book_side_t.best_tick` | ~20 cy — RDTSC scaffold overhead dominates |

---

## Approach Comparison (Run 10 — same hardware, same data)

| Approach | best_bid | match | cancel | add |
|---|---|---|---|---|
| Baseline (flat 138-word scan) | 84 cy | 137 cy | 22 cy | 34 cy |
| Hierarchical bitmap only | 26 cy | 26 cy | 22 cy | 36 cy |
| Tracked field only | 20 cy | 28 cy | 24 cy | 36 cy |
| **Combined (active)** | **20 cy** | **24 cy** | **24 cy** | **38 cy** |
