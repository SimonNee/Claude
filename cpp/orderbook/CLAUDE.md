# cpp/orderbook — Project Instructions

## Purpose

Five-iteration agentASM evolution project. The orderbook is the driver; each iteration
applies progressively more aggressive optimisation using agentASM.

## Build Commands

```bash
# Correctness tests (all iterations)
g++ -std=c++17 -O2 -Wall -o tests tests.cpp orderbook.cpp && ./tests

# Smoke test driver
g++ -std=c++17 -O2 -o main main.cpp orderbook.cpp && ./main

# Benchmarks (Iteration 3+)
g++ -std=c++17 -O2 -o bench bench.cpp orderbook.cpp && ./bench
```

## Iteration Rules

- Each iteration requires explicit user authorisation before starting
- Correctness tests (`tests.cpp`) must pass unchanged at every iteration
- Benchmarks introduced at Iteration 3; results recorded in `status.md`
- agentASM first used at Iteration 3

## Current Iteration

**1 — Naive C++ Baseline**

## Iteration Summary

| Iter | Focus | agentASM |
|------|-------|----------|
| 1 | Naive `std::map` + `std::deque` | No |
| 2 | Cache-friendly flat array + agentTimeAndSpace review | No |
| 3 | RDTSC benchmarks + first asm (`getSpread`, matching loop) | Yes |
| 4 | SIMD price level scan (`cmpps`/`cmppd`) | Yes |
| 5 | Atomics — snapshot publish, `lock xadd` id generation | Yes |
