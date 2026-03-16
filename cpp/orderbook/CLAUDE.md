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
- agentASM introduced at Iteration 4
- **agentASM naming convention**: ASM variants are new functions with `_asm` suffix alongside the originals (e.g., `getSpread_asm`). Do NOT rewrite existing function bodies — keep C++ and ASM versions side-by-side for direct comparison in bench.cpp
- **agentASM pre-flight**: before writing any inline ASM, always compile with `-S` and give agentASM the compiler-generated assembly for the target function. agentASM must review and report on: inlining decisions, parameter passing (ABI/calling convention), stack usage, and whether the compiler output already leaves meaningful headroom. Only proceed to hand-written ASM if the analysis confirms a worthwhile target.
- **Tag on iteration transition**: when the user authorises iteration N+1, tag the current state as `git tag iter-N-complete` before making any changes — this guarantees the tag always captures a confirmed, tested baseline

## Agent Collaboration Model

Established in Iteration 4 — applies to all future iterations:

```
agentDuality → structural C++ change → agentASM pre-flight → ASM if warranted → benchmark
     ↑                                                                               |
     └───────────────────────── findings feed next iteration ───────────────────────┘
```

- **agentDuality**: structural trade-offs — data structure choice, memory layout, cache
  behaviour. Run before making structural C++ changes.
- **agentASM**: primary role is **reviewer of compiled machine code** (`-S` output),
  secondary role is inline ASM writer. Its pre-flight analysis surfaces what the compiler
  has and hasn't done, including inside STL template instantiations. This is the most
  direct performance evidence available — removes guesswork.
- The two agents work hand-in-hand: agentDuality guides structure, agentASM confirms the
  machine-level result and writes targeted `__asm__` where the compiler falls short.

## Current Iteration

**5 — COMPLETE (see status.md)**

## Iteration Summary

| Iter | Focus | agentASM |
|------|-------|----------|
| 1 | Naive `std::map` + `std::deque` | No |
| 2 | Cache-friendly flat array + agentDuality review | No |
| 3 | RDTSC benchmarks, O(1) cancelOrder (id→location index + lazy deletion) | No |
| 4 | agentASM: `getSpread` + matching loop | Yes |
| 5 | SIMD/layout analysis — null result; Atomics deferred (no concurrency model defined) | Yes |
