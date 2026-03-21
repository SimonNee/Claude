# Agents Project Status

## Current State: Orderbook Project Planned — Iteration 1 Pending

**Date:** 2026-03-14
**Branch:** `feature/agents` (from `main`)

---

## What's Done

### agentASM ✓ COMPLETE

- **Agent definition**: `.claude/agents/agentASM.md`
  - Mandatory workflow: read pitfalls first, consult phrasebook, choose syntax, write, checklist
  - Wired to `cpp/asm-kb/` knowledge base
  - Structured output format with constraint notes and compile test step

- **Knowledge base**: `cpp/asm-kb/` (7 files, 1,660 lines)

  | File | Contents |
  |------|----------|
  | `README.md` | Navigation and mandatory reading order |
  | `syntax.md` | GCC `__asm__` template, AT&T vs Intel, `volatile`, local labels |
  | `registers.md` | x86-64 GPR table, AT&T/Intel naming, caller/callee-saved, SIMD |
  | `constraints.md` | Constraint letters, `=`/`+`/`&` modifiers, clobber list |
  | `patterns.md` | C++ → asm phrasebook (arithmetic, bitwise, memory, atomics, SIMD, timing) |
  | `calling-conv.md` | System V AMD64 ABI — args, return values, register preservation |
  | `pitfalls.md` | 12-point pre-flight checklist and documented failure modes |

### agentDuality ✗ NOT STARTED

- **Name**: `agentDuality` — time and space are duals; you can almost always trade one for the other
- Scope: Big-O complexity, space trade-offs, vectorisation, parallelism, cache efficiency
- No knowledge base or agent definition written yet
- **Will be exercised in Iteration 2 of the orderbook project** (see below)

---

## Active Project: C++ Orderbook — agentASM Evolution Driver

**Location:** `cpp/orderbook/`
**Purpose:** Drive the evolution of agentASM usage across 5 iterations, applying asm to hot
paths once benchmarking identifies them. The orderbook itself is deliberately simple (limit
orders, price-time priority).

### File Structure

```
cpp/orderbook/
├── CLAUDE.md           project instructions for this subdirectory
├── orderbook.h         shared header (updated each iteration)
├── orderbook.cpp       implementation (updated each iteration)
├── main.cpp            driver / smoke test harness
├── tests.cpp           correctness tests (all iterations)
├── bench.cpp           benchmarks (Iteration 3+)
└── status.md           per-project status file
```

### Execution Policy

**Each iteration requires explicit user authorisation before implementation begins.**
Present a one-paragraph summary, wait for approval, then execute.

### Verification (all iterations)

```bash
g++ -std=c++17 -O2 -Wall -o tests tests.cpp orderbook.cpp && ./tests
# Iter 3+:
g++ -std=c++17 -O2 -o bench bench.cpp orderbook.cpp && ./bench
```

---

## Iteration Roadmap

### Iteration 1 — Naive C++ Baseline (PENDING — awaiting user authorisation)

**Goal**: Correct, readable orderbook. No performance considerations.

- `std::map<double, std::deque<Order>>` for bids (descending) and asks (ascending)
- `Order` struct: `id`, `price`, `quantity`, `side`
- Operations: `addOrder`, `cancelOrder`, `getBestBid`, `getBestAsk`, `getSpread`
- Matching: FIFO at each price level, partial fills supported
- Tests: add/cancel/match/spread, edge cases (empty book, full fill, partial fill)
- **No agentASM involvement**

---

### Iteration 2 — Better C++ + agentDuality Analysis (PENDING)

**Goal**: Replace `std::map` with cache-friendly structures. agentDuality reviews
Iteration 1 code and recommends trade-offs before changes are made.

- Replace `std::map` with sorted `std::vector<PriceLevel>` (flat array, binary search insert)
- Cache-aligned `Order` structs (`alignas(64)`)
- Contiguous order storage per price level
- Run agentDuality on Iter 1 → document analysis in `status.md`
- **No agentASM involvement yet**

---

### Iteration 3 — Benchmark + First agentASM (PENDING)

**Goal**: Measure before touching asm. Apply agentASM to first confirmed hot path.

- Benchmarking in `bench.cpp` using RDTSC (from `cpp/asm-kb/patterns.md` — Timing section)
- Measure: order insertion, matching loop, spread calculation (1M order stress run, ns/op)
- **agentASM targets** (after measurement confirms):
  - `getSpread()` — `ask - bid` as inline asm subtraction
  - `getBestBid()` / `getBestAsk()` — branchless load with CMOV if applicable
  - Quantity arithmetic in the matching loop
- Record benchmark delta before/after in `status.md`

---

### Iteration 4 — SIMD Price Level Scanning (PENDING)

**Goal**: SSE/AVX to scan price levels in parallel rather than one-by-one.

- Target: inner loop walking price levels looking for a matching price
- Levels stored as contiguous float/double array (requires Iter 2's flat layout)
- **agentASM targets**:
  - Price comparison scan using `cmpps` / `cmppd` (packed compare)
  - Best bid/ask in a level array with SIMD min/max (`minps`, `maxps`)

---

### Iteration 5 — Atomic Operations / Lock-Free Touch (PENDING)

**Goal**: Safe producer/consumer pattern (one writer, snapshot reader) using atomics.

- **Scope**: Not a full lock-free orderbook — one writer, snapshot reader
- **agentASM targets**:
  - Atomic load/store of best bid/ask price (published snapshot)
  - `lock xadd` for order id generation
  - Compiler memory barrier around snapshot publish
- Patterns sourced from `cpp/asm-kb/patterns.md` — Atomics section

---

## agentASM Involvement Summary

| Iteration | agentASM | What it replaces |
|-----------|----------|-----------------|
| 1 | No | — |
| 2 | No (agentDuality analysis) | — |
| 3 | Yes — first use | `getSpread()`, quantity arithmetic |
| 4 | Yes — SIMD | Price level scan loop |
| 5 | Yes — atomics | Snapshot publish, id generation |

---

## Design Decisions

- **Pattern**: Both agents follow the AgentQ model — mandatory reading of a pitfalls doc,
  a phrasebook-style knowledge base, and a structured output format
- **Knowledge base location**: `cpp/asm-kb/` (mirrors `kdb/phrases/docs/` for AgentQ)
- **Pitfalls doc**: Lives in both `asm-kb/pitfalls.md` and referenced from the agent file;
  grows from real incidents discovered during use
- **Status docs**: Per-project local files for now; a top-level summary can be added later
  without changing the local files

---

## Completed Work

### agentDuality ✓ COMPLETE (`584e10f`, `da6eff9`, `1713c91`)
- **Agent definition**: `.claude/agents/agentDuality.md`
  - 5-verdict set: Recommend / Do not recommend / Retain / Conditional / Measure first
  - Step 7: mandatory envelope question before any O(n) verdict
- **Knowledge base**: `cpp/duality-kb/` (6 files)
  - Pitfall 13: "know your envelope — you can't measure unless you know how long your ruler is"
  - Phrasebook includes envelope question section in `tradeoffs.md`

### Iteration 1 ✓ COMPLETE (`fa49a1f`)
- Naive `std::map` + `std::deque` orderbook
- 14/14 correctness tests passing
- RDTSC baseline timing: **1,196 cycles/order** (null datum)

### q Data Generator — PARTIALLY UPDATED (bug blocking)
- `gen_orders.q` updated to OU mean-reverting price walk (THETA=0.05, PRICE_BAND=2.50)
- OU generation verified interactively: prices within [99.59, 100.43], 85 distinct levels
- **BUG**: `{ssr[x;enlist"f";""]} each lines` hangs over 1M lines — too slow
- CSV on disk still contains old free-random-walk data
- **Fix**: replace `each` ssr with single-string ssr over entire file

### Iteration 2 — STRUCTURALLY COMPLETE, benchmark blocked
- All 3 changes applied: struct reorder (32→24 bytes), deque→vector+head, map→vector
- 14/14 correctness tests pass
- Timing regression: 1,196 → 1,727 cycles/order (+44%) — caused by unbounded p in old data
- agentDuality final verdict: **Conditional** (vector wins p<500, map wins p>10,000)
- Benchmark cannot be re-run until data generator bug is fixed

---

## IMMEDIATE NEXT STEP ON RELOAD

**Fix `gen_orders.q` f-suffix bug.** Replace:
```q
cleaned:{ssr[x;enlist"f";""]} each lines
```
With single-string approach (read whole file, one ssr, write back):
```q
content:raze {x,"\n"} each read0 outFile
content:ssr[content;enlist"f";""]
outFile 0: "\n" vs content
```
Or simpler — use `system "sed"` call after save.

Then: regenerate CSV → re-run benchmark → validate Iteration 2 → proceed to Iteration 3.

---

## Iteration Roadmap (remaining)
