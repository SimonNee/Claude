# Benchmark History

Build: `g++ -std=c++17 -O2 -march=native -flto -o bench bench.cpp orderbook.cpp`

> `-flto` added from Iteration 11. Earlier results were built without it and are not
> directly comparable on the getBestBid+Ask+Spread benchmark (LTO exposed a latent
> benchmark flaw — return values were discarded, allowing dead-code elimination).
> All other benchmarks are comparable across iterations.

> Iter 7 is the clean baseline — `-march=native` introduced here. Earlier results
> (Iter 3–6) are in status.md but are not directly comparable (different ISA baseline,
> evolving bench.cpp structure).

---

## Iteration 7 — Bitmap level index + Order struct reduction

**Date**: 2026-03-17 | **Tag**: `iter-7-complete`

| Operation | N | cycles/op |
|-----------|---|-----------|
| addOrder no-cross | 500,000 | 97 |
| addOrder crossing 1 level | 100,000 | 69 |
| addOrder crossing 5 levels | 100,000 | 466 |
| cancelOrder | 500,000 | 12 |
| getBestBid+Ask+Spread (per trio) | 1,000,000 | 38 |
| mixed workload cancel=10% | ~550,000 | 110 |
| mixed workload cancel=50% | ~750,000 | 74 |
| mixed workload cancel=90% | ~950,000 | 50 |

---

## Iteration 11 — LTO cross-TU inlining + benchmark sink fix

**Date**: 2026-03-17 | **Tag**: `iter-11-complete` (pending)
**Build**: `g++ -std=c++17 -O2 -march=native -flto -o bench bench.cpp orderbook.cpp`

| Operation | N | cycles/op | vs Iter 10 |
|-----------|---|-----------|------------|
| addOrder no-cross | 500,000 | 78 | −7% |
| addOrder crossing 1 level | 100,000 | 50 | −14% |
| addOrder crossing 5 levels | 100,000 | 358 | −13% |
| cancelOrder | 500,000 | 12 | −29% |
| getBestBid+Ask+Spread (per trio) | 1,000,000 | 19 | −53%* |
| mixed workload cancel=10% | ~550,000 | 103 | ~0 |
| mixed workload cancel=50% | ~750,000 | 84 | ~0 |
| mixed workload cancel=90% | ~950,000 | 59 | ~0 |

*getBestBid+Ask+Spread: previous 40-cycle figure included optional-unwrap overhead
and was not guarded against dead-code elimination. 19 cycles is the correct baseline.

LTO inlines addOrder, matchBuy, matchSell across the TU boundary into bench call sites,
eliminating 4 callee-saved register push/pop pairs and the call/ret overhead per iteration.
Benchmark sink fix: return values now accumulated into a double to prevent LTO eliminating
pure read-only calls.

---

## Iteration 10 — Replace optional<OrderLocation> with sentinel struct

**Date**: 2026-03-17 | **Tag**: `iter-10-complete` (pending)

| Operation | N | cycles/op | vs Iter 9 |
|-----------|---|-----------|-----------|
| addOrder no-cross | 500,000 | 84 | −13% |
| addOrder crossing 1 level | 100,000 | 58 | −19% |
| addOrder crossing 5 levels | 100,000 | 412 | −11% |
| cancelOrder | 500,000 | 17 | +4% (variance) |
| getBestBid+Ask+Spread (per trio) | 1,000,000 | 40 | ~0 |
| mixed workload cancel=10% | ~550,000 | 102 | ~0 |
| mixed workload cancel=50% | ~750,000 | 85 | +13% (variance) |
| mixed workload cancel=90% | ~950,000 | 61 | ~0 |

`imulq` → `sarq $4` on every orderIndex access (24→16 bytes, power-of-two element size).
Working set reduction: 24→16 bytes/slot, −33% at 500k ids (12MB→8MB).

---

## Iteration 9 — Promote ASM to canonical + tombstone bug fix

**Date**: 2026-03-17 | **Tag**: `iter-9-complete` (pending)

| Operation | N | cycles/op | vs Iter 8 C++ |
|-----------|---|-----------|---------------|
| addOrder no-cross | 500,000 | 96 | ~0 |
| addOrder crossing 1 level | 100,000 | 72 | +4% (variance) |
| addOrder crossing 5 levels | 100,000 | 463 | ~0 |
| cancelOrder | 500,000 | 13 | ~0 |
| getBestBid+Ask+Spread (per trio) | 1,000,000 | 39 | ~0 |
| mixed workload cancel=10% | ~550,000 | 109 | ~0 |
| mixed workload cancel=50% | ~750,000 | 75 | ~0 |
| mixed workload cancel=90% | ~950,000 | 53 | ~0 |

ASM fill arithmetic is now the canonical path. Tombstone skip adds no measurable cost.

---

## Iteration 8 — ASM double-load fix (matchBuy_asm / matchSell_asm)

**Date**: 2026-03-17 | **Tag**: `iter-8-complete`

| Operation | N | cycles/op | vs Iter 7 |
|-----------|---|-----------|-----------|
| addOrder no-cross | 500,000 | 97 | — |
| addOrder crossing 1 level | 100,000 | 69 | — |
| addOrder crossing 5 levels | 100,000 | 466 | — |
| cancelOrder | 500,000 | 12 | — |
| getBestBid+Ask+Spread (per trio) | 1,000,000 | 38 | — |
| mixed workload cancel=10% | ~550,000 | 110 | — |
| mixed workload cancel=50% | ~750,000 | 74 | — |
| mixed workload cancel=90% | ~950,000 | 50 | — |
| addOrder_asm crossing 1 level | 100,000 | 68 | −1% |
| addOrder_asm crossing 5 levels | 100,000 | 330 | −29% |
