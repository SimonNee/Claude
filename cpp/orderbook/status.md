# cpp/orderbook — Status

## Iteration 1 — Naive C++ Baseline

**Status**: COMPLETE
**Date**: 2026-03-14

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

### Iteration 1 Baseline Timing (recorded before any changes)

| Metric | Value |
|--------|-------|
| Orders processed | 1,000,000 |
| Total cycles | 1,196,486,293 |
| **Cycles/order** | **1,196** |

This is the null datum. All subsequent iterations are measured against it.

**Key reasoning:**
- At p < 100 price levels the sorted vector beats the map on cache grounds — all levels fit in L1
- Deque 512-byte minimum chunk waste eliminated; inner matching loop becomes a sequential scan
- Order struct reorder is zero-cost — 25% size reduction, better packing density
- cancelOrder O(p*q) scan left unchanged until cancel rate is measured in Iteration 3
