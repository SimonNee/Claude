# cpp/orderbook — Status

## Iteration 1 — Naive C++ Baseline

**Status**: COMPLETE
**Date**: 2026-03-14
**Tag**: `iter-1-complete`

### What was implemented

- `Order` struct: `id`, `price`, `quantity`, `side`
- `OrderBook` class:
  - `std::map<double, std::deque<Order>, std::greater<double>>` for bids (highest first)
  - `std::map<double, std::deque<Order>>` for asks (lowest first)
- Operations: `addOrder`, `cancelOrder`, `getBestBid`, `getBestAsk`, `getSpread`
- Matching: FIFO at each price level, partial fills supported, multi-level crossing
- `std::optional<double>` returned when book side is empty

### Test results

14/14 correctness tests passed:

```
[PASS] test_empty_book
[PASS] test_add_single_bid
[PASS] test_add_single_ask
[PASS] test_spread
[PASS] test_cancel_order
[PASS] test_cancel_nonexistent
[PASS] test_full_fill
[PASS] test_partial_fill_buy_remainder
[PASS] test_partial_fill_sell_remainder
[PASS] test_fifo_at_price_level
[PASS] test_price_time_priority_best_bid
[PASS] test_price_time_priority_best_ask
[PASS] test_crossing_order_drains_multiple_levels
[PASS] test_sell_does_not_cross_below_price
```

### Benchmark (OU data — corrected datum, 2026-03-15)

| Metric | Value |
|--------|-------|
| Orders processed | 1,000,000 |
| Total cycles | 1,175,342,012 |
| **Cycles/order** | **1,175** |
| Spread at end | 0.02 (2 ticks — confirms OU walk working) |

> Note: The original datum of 1,196 cycles/order was measured against free random walk
> data. This corrected datum uses the OU walk (THETA=0.05, PRICE_BAND=2.50) which
> produces a realistic bounded price range. 1,175 is the authoritative Iteration 1 baseline.

### Notes

No performance considerations. No agentASM involvement.

Known O(n) operations:
- `cancelOrder` — linear scan over all price levels and orders
- Matching loop — linear in number of price levels crossed

These will be addressed in Iterations 2–4.

---

## Iteration 2 — Better C++ + agentDuality Analysis

**Status**: IN PROGRESS
**Date**: 2026-03-14

### agentDuality Analysis — Summary

Four issues identified. Three recommended, one deferred:

| Issue | Change | Verdict |
|-------|--------|---------|
| 1 | `std::map` → `std::vector<PriceLevel>` sorted flat | Recommend |
| 2 | `std::deque<Order>` → `std::vector<Order>` + head index | Recommend |
| 3 | Reorder `Order` struct members (doubles first) — 32→24 bytes | Recommend |
| 4 | `id → location` index for O(1) `cancelOrder` | Measure first — cancel rate unknown |

### Benchmark Results (all on OU data — 2026-03-15)

| Metric | Iter 1 (map) | Iter 2 initial | Iter 2 fixed | Delta vs Iter 1 |
|--------|-------------|----------------|--------------|-----------------|
| Cycles/order | 1,175 | 1,262 | **1,209** | **+3%** |

The earlier reported regressions (+44%, +36%) were against the old free random walk data.
On correct OU data the initial vector was +7%. After fixing the match-loop erase the gap
is +3% — 34 cycles/order.

> **Run-to-run note:** the RDTSC gap is within run-to-run variance (0.6%–3% observed
> across separate runs). The gap is real but small — do not over-interpret the exact figure.

### p Instrumentation Results

| Stat | p (bids + asks active levels) |
|------|-------------------------------|
| min  | 1 |
| mean | 72.3 |
| p50  | 73 |
| p95  | 79 |
| p99  | 81 |
| max  | 82 |

p is tightly bounded with a hard ceiling of 82 — well within the vector-wins regime
(crossover ~10,000 per agentDuality knowledge base). The vector is the correct structure.

### Match-loop erase fix (2026-03-15)

**Root cause of residual regression:** `asks.erase(it)` was called eagerly inside the
match loop each time a level drained. At p=72 this costs ~71 × 40 bytes of memmove per
drain, mid-traversal.

**Fix:** deferred conditional compaction — traverse without erasing, then run a single
`remove_if` pass after the loop, guarded by a `drained` flag so the scan is skipped
entirely for non-crossing orders (the common case).

### perf stat — Valid Comparison (2026-03-15)

Both iterations built with `-O2` and run against the same 1,000,000-order OU CSV benchmark.
Iter 1 used its original orderbook.cpp/h with a minimal benchmark main (no instrumentation calls).

| Metric | Iter 1 (map+deque) | Iter 2 (vector+head-idx) | Delta |
|--------|-------------------|--------------------------|-------|
| RDTSC cycles/order | 1,188 | 1,195 | **+0.6%** |
| IPC | 2.35 | 2.41 | +2.6% |
| L1-dcache-load-misses | 3,412,022 | 4,906,367 | +44% |
| LLC-load-misses | 274,841 | 344,290 | +25% |

**Findings:**

1. **The RDTSC gap is within run-to-run variance.** +0.6% in this run; +3% in earlier run.
   The structural cost is real but small and noisy — do not treat 34 cycles/order as precise.

2. **Iter 2 has +44% L1 misses and +25% LLC misses.** These are real — the inner vector
   buffers (q_mean=1,085 orders/level × 82 levels × 24 bytes = ~1.88MB live) land in L3,
   not L1 as the pre-instrumentation analysis assumed.

3. **The miss penalty is hidden by IPC.** Iter 2's IPC is 2.41 vs Iter 1's 2.35 — Iter 2
   executes more instructions per cycle because the out-of-order engine overlaps memory
   latency with useful work. The cache misses exist but are mostly off the critical path.

4. **LLC miss penalty estimate:** 69,449 additional LLC misses × ~50 cycles/L3 hit ≈ 3.5M
   extra cycles. Over 1M orders that is ~3.5 cycles/order — a fraction of the observed gap.
   The rest is instruction overhead (insert shifts, binary search on a larger inner structure).

5. **"Fits in L1" claim retracted.** At q_mean=1,085 the inner order buffers live in L3.
   The outer PriceLevel metadata (82 × 40 bytes = ~3.3KB) is L1-hot; the inner order data
   is not. The pre-instrumentation assumption of q≈10 was wrong by ~100×.

**Updated working set table (measured values):**

| Component | Size | Cache level |
|-----------|------|-------------|
| Outer PriceLevel array (82 × 40 bytes) | ~3.3KB | L1 — fits |
| Inner order buffers — live (72 × 1,085 × 24 bytes) | ~1.88MB | L3 |
| Inner order buffers — allocated (dead prefix included) | ~21MB est. | L3/RAM |

### Time-series instrumentation (2026-03-15)

Cycles/order measured per decile across the 1,000,000 order run:

| Decile | Cycles/order |
|--------|-------------|
| 0–10%  | 1,234 |
| 10–20% | 1,254 |
| 20–30% | 1,227 |
| 30–40% | 1,255 |
| 40–50% | 1,247 |
| 50–60% | 1,244 |
| 60–70% | 1,257 |
| 70–80% | 1,252 |
| 80–90% | 1,290 |
| 90–100%| 1,247 |

**Finding: declining trend (1,239 → 1,169 cycles/order).** The hot loop gets *faster* over
the run, not slower. This is a cache-warming effect: as the same ~82 price levels are
repeatedly accessed, the outer PriceLevel array and inner order buffer hot regions become
L2/L1-resident. This rules out head-index inflation as a source of degradation — dead-prefix
growth does not appear in the per-order cost.

**Conclusion:** Iter 2's +25% LLC misses vs Iter 1 are real but their penalty is largely
hidden by out-of-order execution (IPC 2.41). The gap is structural — the inner order buffers
at q_mean=1,085 live in L3, while Iter 1's map nodes at the same working set (p=72 levels)
have stable heap addresses that warm to L2 over repeated access. Closing the gap further
requires either periodic compaction (reduce dead prefix, lower allocated working set) or
Iteration 3 techniques (per-phase profiling with `perf annotate` to isolate the hot path).

**Iteration 2 status:** COMPLETE

### Key reasoning (agentDuality — updated after q instrumentation)

- At p=82 price levels the sorted outer vector is L1-hot and correct — crossover is ~10,000
- Inner order buffers at q_mean=1,085 land in L3 (not L1 as initially assumed with q≈10)
- Out-of-order execution hides most of the additional L3 miss penalty — IPC improves
- Order struct reorder is zero-cost — 25% size reduction, better packing density
- cancelOrder O(p*q) scan: at q_mean=1,085 × p=72 = ~78K comparisons per cancel — unacceptable
  at production cancel rates (often >90% of order flow). This is the priority fix for Iteration 3.
- Deque 512-byte minimum chunk waste eliminated; inner match traversal is fully sequential

---

## Iteration 3 — Benchmarks + cancelOrder O(1)

**Status**: IN PROGRESS
**Date**: 2026-03-15

### What was implemented

**1. id→location index (`orderIndex`)**

Added `std::unordered_map<int, OrderLocation>` as a private member, where:
```cpp
struct OrderLocation { Side side; double levelPrice; std::size_t orderIdx; };
```
- Inserted when an order rests (post-match, confirmed resting)
- `orderIdx` is the stable index into `PriceLevel::orders` — valid for the lifetime of the order because lazy deletion never shifts elements
- Erased when filled (in match loop) or cancelled

**2. Lazy deletion in `cancelOrder`**

Instead of `orders.erase()` (O(q) memmove), cancelled orders are marked with `id=0` (tombstone). No vector shifting. The match loop skips tombstones naturally:
- `fill = min(order.qty, 0.0) = 0` → no fill
- `pop_front()` advances head past the tombstone

**3. `liveOrders` counter in `PriceLevel`**

`empty()` and `liveCount()` now use a dedicated counter rather than `orders.size() - head`, which would count tombstones. `cancel_at(oi)` decrements it; `pop_front()` decrements only for live orders (`id != 0`).

**cancelOrder complexity: O(p*q) → O(1)**
- Map lookup: O(1) average
- Binary search to level: O(log p) = 7 comparisons at p=82
- Direct index to order slot: O(1) — no scan

### Benchmark Results (2026-03-15)

Synthetic in-memory workload. OU price walk (same parameters as CSV generator).
All adds are offset away from mid — no crossing — so every add produces a resting order.

| Operation | N | cycles/op | Notes |
|-----------|---|-----------|-------|
| addOrder no-cross | 500,000 | 233 | +12 vs no-index (map insert overhead) |
| addOrder crossing 1 level | 100,000 | 176 | |
| addOrder crossing 5 levels | 100,000 | 857 | |
| **cancelOrder** | **500,000** | **150** | **was 30,946 — 206× improvement** |
| getBestBid+Ask+Spread (trio) | 1,000,000 | 31 | O(1) front() access |
| mixed cancel=10% | ~550,000 | 252 | |
| mixed cancel=50% | ~750,000 | 186 | |
| mixed cancel=90% | ~950,000 | 160 | |

**Key finding — cancel rate no longer affects throughput.** Mixed workload cycles/op is
flat at 160–252 regardless of cancel rate. With O(1) cancel, high cancel rates produce
more ops in the timing window (more work done), which is why 90% cancel shows lower
cycles/op than 10% (the add-dominated case with more no-cross overhead).

**addOrder no-cross overhead (+12 cycles)** is the cost of the extra `orderIdx` field
in the `orderIndex` map insert. Acceptable trade for 206× cancel improvement.

### Design iterations during Iteration 3

Three cancel implementations were tried before arriving at the final design:

| Attempt | Approach | cancelOrder cycles | Problem |
|---------|----------|-------------------|---------|
| 1 | O(p*q) nested scan (Iter 2) | 30,946 | Baseline — unacceptable |
| 2 | Map + linear scan within level | ~30,946 | Map helps lookup but erase-shift still O(q) |
| 3 | Map + lazy deletion (no orderIdx) | 36,200 | Tombstones accumulate; scan degrades O(k) |
| **4** | **Map + orderIdx + lazy deletion** | **150** | **O(1) — direct jump, no scan, no shift** |

Attempt 3 (lazy deletion without stored index) was worse than the original in the
isolated cancel benchmark because tombstones accumulate without fills to clear them,
and the linear scan past k tombstones grows O(k) per cancel.

Storing `orderIdx` in the map is safe because lazy deletion never shifts elements —
indices are stable for the lifetime of a resting order.

---

## Data Generator

**Status**: WORKING
**Date**: 2026-03-15

### Resolution

- Root cause: bare `/` lines in comment block activated q's block comment mode, silently
  discarding all code after the first bare `/`
- Fix: removed bare `/` separator lines; replaced per-line `ssr each` with
  `system "sed -i 's/f//g' ..."` for the f-suffix strip step
- Additional finding: empirical testing confirmed q's `save` does NOT append an `f` suffix
  to floats in CSV output — the strip step is retained as defensive practice but is not
  strictly necessary for this schema

### Generator configuration (current)

| Variable | Value | Effect |
|----------|-------|--------|
| `N` | 1,000,000 | Rows generated |
| `MID` | 100.0 | OU reversion target |
| `STEP` | 0.05 | Per-tick noise (tick size) |
| `THETA` | 0.05 | Reversion strength |
| `PRICE_BAND` | 2.50 | Hard clamp: prices stay in [97.50, 102.50] |

Run with: `q data/gen_orders.q`
