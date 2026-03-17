# Latest Benchmarks

**Iteration**: 8
**Date**: 2026-03-17
**Build**: `g++ -std=c++17 -O2 -march=native -o bench bench.cpp orderbook.cpp`

## Iteration 7 — C++ baseline

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

## Iteration 8 — ASM variants (cross paths only)

| Operation | N | cycles/op | vs C++ |
|-----------|---|-----------|--------|
| addOrder_asm crossing 1 level | 100,000 | 68 | −1% |
| addOrder_asm crossing 5 levels | 100,000 | 330 | −29% |
