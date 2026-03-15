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

### Benchmark Results (all on OU data — corrected, 2026-03-15)

| Metric | Iteration 1 (map) | Iteration 2 (vector) | Delta |
|--------|-------------------|----------------------|-------|
| Cycles/order | 1,175 | 1,262 | **+7%** |

The earlier reported regressions (+44%, +36%) were both measured against the old free
random walk data which produced pathologically high p (unbounded distinct price levels).
On the correct OU data the gap narrows to +7% — a small residual regression.

### Analysis

**Why the small regression persists:**
- agentDuality review identified two concrete mechanisms:
  1. `asks.erase(it)` inside the matching loop causes an O(p) shift every time a level
     is fully drained — at k drained levels this is O(k×p) vs map's O(k log p)
  2. `cancelOrder` bypasses the head-index trick, using `erase(begin() + oi)` which
     causes an O(q) shift within the order vector

**Why we are not reverting:**
- At realistic p (expected 5–20 active levels for a liquid OU book) the vector should win
- The +7% gap is small enough that p instrumentation is needed before any structural verdict
- The struct reorder (Change 3) is unconditionally correct and retained regardless

**Next step:** instrument `bids.size() + asks.size()` during the benchmark run to measure
actual p. If p is in the expected range, the residual regression likely disappears or
reverses.

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
