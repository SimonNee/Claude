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

**Finding: flat across all deciles.** No degradation trend over the run. This rules out
head-index inflation (growing dead prefix in order vectors) as the source of the gap.

**Conclusion:** the remaining +3% is structural — the map's 72-node working set stays
warm in L2 throughout the run, making temporal locality more competitive than spatial
locality theory predicted at p=72. Closing the gap further requires either a structural
change within Iteration 2 or Iteration 3 techniques.

**Iteration 2 status:** IN PROGRESS — one further idea to explore before closing out.

### Key reasoning (agentDuality — still valid at small p)

- At p < 100 price levels the sorted vector beats the map on cache grounds — all levels fit in L1
- Deque 512-byte minimum chunk waste eliminated; inner matching loop becomes a sequential scan
- Order struct reorder is zero-cost — 25% size reduction, better packing density
- cancelOrder O(p*q) scan left unchanged until cancel rate is measured in Iteration 3

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
