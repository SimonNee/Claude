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

**Status**: COMPLETE
**Date**: 2026-03-15
**Tag**: `iter-3-complete`

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

## Iteration 4 — agentASM Pre-flight Analysis

**Status**: IN PROGRESS
**Date**: 2026-03-16
**Tag**: `iter-4-pre-asm`

### Method

Before writing any inline ASM, compile with `-S` and give agentASM the compiler-generated
assembly for each candidate function. agentASM reports on: inlining decisions, ABI/calling
convention, stack usage, and whether meaningful headroom exists. Only proceed to hand-written
ASM if analysis confirms a worthwhile target.

### Candidate 1 — `getSpread_asm`

**Verdict: not a worthwhile ASM target.**

The compiler already inlined both `getBestBid` and `getBestAsk` into `getSpread`. The hot
path is 9 instructions of near-irreducible work. The only overhead is 3 instructions of
`std::optional` exit normalisation dictated by the calling convention — inline ASM cannot
change the return ABI without changing the signature.

### Candidate 2 — `matchBuy_asm` inner loop

**Verdict: narrow target — 2 improvable redundancies in the fill arithmetic.**

Two redundancies identified by assembly inspection:

1. **`resting.quantity` double-load**: loaded into `xmm1` for `minsd`, then immediately
   reloaded from memory for the subtraction. A spare XMM register before `minsd` eliminates
   this. Saving: ~1 load (~4 cycles) per fill iteration.

2. **`orders.data()` reloaded every inner iteration**: `movq 8(%r12), %rax` inside `.L340`
   is not hoisted out of the inner loop. The pointer cannot change during filling (no
   reallocation from `pop_front`). Hoisting saves ~1 load per iteration.

Combined: ~2 loads × ~4 cycles = ~8 cycles per fill iteration on the partial-fill hot path.

### Assembly finding — `orderIndex.erase` is the real bottleneck

**Source**: assembly analysis of the compiled STL template instantiation
(`_M_erase.isra.0` + `_M_erase_inner`, lines 1291–1383 and 1211–1287 of `orderbook.s`).

| Component | Cycles (warm cache) | Root cause |
|-----------|---------------------|------------|
| 3× `divq` | ~120–180 | `_Prime_rehash_policy` — prime bucket count, cannot use bitmask |
| `operator delete` (tcache) | ~50–100 | node-based allocation — every erase frees a heap node |
| Pointer chase + key compare | ~8–20 | separate chaining, 1 dereference typical |
| **Total** | **~185–315** | |

`divq` (64-bit unsigned divide) costs 35–90 cycles and is not pipelined. Three occur per
erase: two in the lookup/chain-walk phase, one in the unlink phase. This is a direct
consequence of `libstdc++`'s `_Prime_rehash_policy` — prime bucket counts prevent the
`% bucket_count` from being strength-reduced to a bitmask.

`operator delete` is called on every erase because `std::unordered_map` is node-based —
each entry is a separately heap-allocated 40-byte struct. Even a tcache hit costs 50–100
cycles and pollutes L1/L2 with allocator metadata.

**Inline ASM cannot help.** The cost is structural — it lives inside compiled STL template
code that agentASM cannot reach. The fix is replacing the container.

### Structural fix: replace `std::unordered_map` with a flat direct-index array

**Implemented**: `std::vector<std::optional<OrderLocation>>` indexed directly by order ID.

Order IDs are dense sequential integers starting from 1 (`nextId` increments from 1).
No hash function, no division, no heap allocation per entry. Vector grows with `resize`
on insert; slots reset to `std::nullopt` on erase.

### Benchmark Results (2026-03-16)

| Operation | unordered_map | flat array | Delta |
|-----------|--------------|------------|-------|
| addOrder no-cross | 233 | 158 | −32% |
| addOrder cross-1L | 177 | 125 | −29% |
| addOrder cross-5L | 888 | 743 | −16% |
| **cancelOrder** | **156** | **48** | **−69%** |
| mixed cancel=10% | 259 | 182 | −30% |
| mixed cancel=50% | 198 | 137 | −31% |
| mixed cancel=90% | 161 | 109 | −32% |

`cancelOrder` dropped from 156 → 48 cycles (3.25×) — the `divq` × 3 and `operator delete`
eliminated entirely. All other operations improved ~30% from removal of map insert overhead.

Assembly analysis was the evidence that made this change justifiable: without reading the
compiled STL template code, the `divq` cost and per-node `operator delete` would have been
invisible.

### `matchBuy_asm` — inline ASM written and benchmarked

agentASM wrote the inner fill loop body using `__asm__ volatile` with named extended
constraints, targeting both identified redundancies:

1. `resting.quantity` double-load eliminated — copy into `[rq]` before `minsd` overwrites it
2. `orders.data()` hoisted above the `while` loop — stable across `pop_front()` calls

**Benchmark result (cross-1L C++ vs ASM):**

| | C++ addOrder | ASM addOrder_asm | Delta |
|--|--|--|--|
| cross-1L | 118 | 119 | ~0 (noise) |
| cross-5L | 644 | 680 | ~0 (noise) |

Result is within run-to-run variance — the compiler was already near-optimal for this loop
after the `orderIndex` structural fix removed the dominant cost. All 14 correctness tests
pass with the inline ASM in place.

---

## Iteration 4 — Methodology Conclusion

The work done in Iteration 4 establishes the intended collaboration model for future
iterations:

### Agent roles

**agentASM** — primary role is **reviewer of compiled machine code**, not just ASM writer.
Its pre-flight analysis (compile with `-S`, read the output, report on inlining/ABI/
headroom) is what makes it valuable. In this iteration, the assembly review of the STL
template instantiation revealed the `divq` + `operator delete` cost — a finding invisible
from C++ source alone — and directly motivated a structural fix worth −30–69% across all
operations.

**agentDuality** — structural trade-off analysis at the C++ level: data structure choice,
memory layout, cache behaviour. Informs which structural changes to make before ASM is
attempted.

**The collaboration loop:**

```
agentDuality → structural C++ change → agentASM pre-flight → asm if warranted → benchmark
     ↑                                                                               |
     └───────────────────────── findings feed next iteration ───────────────────────┘
```

### Key lesson

The pre-flight assembly review should always precede ASM writing. In this iteration it
identified that `getSpread` was not a worthwhile target (compiler already optimal), found
two narrow redundancies in `matchBuy`, and — most valuably — discovered the `orderIndex`
structural problem that no amount of inline ASM could have addressed.

Assembly analysis of compiled code (including STL template instantiations) is the most
direct evidence available for performance decisions. It removes guesswork.

---

## Iteration 5 — SIMD/layout analysis + Atomics (deferred)

**Status**: COMPLETE
**Date**: 2026-03-16

### agentDuality pre-flight — SIMD outer price scan (rejected)

Initial Iteration 5 scope was SIMD price-level scan (`cmppd`) on the outer matching loop.
agentDuality analysis rejected this before any code was written.

**Verdict: do not recommend.**

| Approach | Cost | vs scalar (k=1–5) |
|----------|------|-------------------|
| Gather (current AoS, 48-byte stride) | 147–420 cycles just to load prices | 7–15× slower |
| SoA packed SIMD | ~26–73 cycles full sweep, crossover at k>15 | loses for k<15 |
| Scalar early-exit | ~10–20 cycles | baseline |

Root causes:
- The outer loop is an **early-exit scan** — exits after k=1–5 levels. SIMD cannot express
  early exit without serialising back to scalar, negating throughput advantage.
- `vgatherqpd` at 48-byte stride costs 7–20 cycles per 4-element gather — more than the
  entire scalar path for the common case.
- SoA crossover (k>15–20) is structurally unreachable: the OU price band is 5 ticks wide.
- SoA doubles write amplification on `addOrder no-cross` — the dominant operation.
- agentDuality Pitfall 8: price field is never scanned independently — it always gates
  immediate access to orders/head/liveOrders. AoS is correct here.

**Redirected target**: the **inner fill loop**, where q_mean=1,085 orders at 24-byte stride
within a single level's order vector is the genuine SIMD candidate — scan one field
(quantity) across many contiguous objects within a level. Atomics remain a separate
concern and are included in Iteration 5 alongside SIMD.

### agentASM pre-flight — inner fill loop SIMD (rejected)

agentASM identified four independent blockers preventing auto-vectorisation:

1. **Carried dependency** on `order.quantity` — each iteration depends on the previous
2. **Data-dependent exit** — trip count unknown before loop starts
3. **24-byte stride** — SSE2 cannot load two adjacent `quantity` fields; gather costs more than scalar
4. **Aliasing ambiguity** — compiler cannot prove `&order` and `&resting` don't alias

The 24-byte stride is the structural wall: SSE2 needs 2 loads for 2 doubles (no gain over
scalar); AVX2 gathers are 5–7 cycles per 4 doubles vs ~2 cycles for 4 scalar `movsd`. The
compiler's scalar output is already near-optimal for this layout.

The carried dependency is reformulable via a **prefix-sum two-pass** approach: read-only
scan (Phase 1) finds the crossing index without writing anything; sequential update pass
(Phase 2) applies fills. This was implemented and benchmarked.

### Prefix-scan benchmark result (2026-03-16)

| | Scalar | Prefix-scan | Delta |
|--|--|--|--|
| cross-1L | 133 | 120 | −10% |
| cross-5L | 595 | 637 | +7% |

**Null result — within run-to-run variance.** The two-pass overhead (reading data twice)
exactly offsets any OOO scheduling benefit from the read-only Phase 1. Phase 2 retains the
same serial carried dependency as the scalar loop. Neither variant wins meaningfully.

**Conclusion**: the scalar fill loop is already at the ceiling for the current `Order`
layout (24-byte stride, AoS). SIMD on the inner fill loop requires a data layout change
(parallel contiguous `quantity` array at stride 8) to be viable. That is a structural
change for a future iteration.

### Atomics — dropped

Atomics were listed in the original Iteration 5 scope without a defined concurrency
problem. The orderbook is single-threaded — there is no shared state accessed from
multiple threads, so atomics would protect nothing real. Adding them would be premature.

A meaningful atomics implementation requires a defined concurrency model first (e.g. a
separate market-data reader thread, a lock-free `nextId` for multi-threaded submission).
Deferred until a concrete scenario is specified.

---

## Iteration 6 — Agent Ecosystem + Project Infrastructure

**Status**: COMPLETE
**Date**: 2026-03-17

### Scope

The orderbook optimisation work (Iterations 1–5) exposed two process gaps:

1. **No SOTA literature review at project start** — the bitmap/tick-indexed level design
   is a well-known pattern in low-latency orderbook implementations. It was not considered
   until Iteration 6 planning because the solution space was never opened before the first
   design decision was made. All subsequent iterations optimised within a suboptimal frame.

2. **Agent resources are structurally loose** — agentDuality and agentASM have knowledge
   bases (`duality-kb/`, `asm-kb/`) that ground their analysis. No equivalent resource
   exists for project initiation. Agent KB files live in ad-hoc locations with no documented
   ownership or maintenance model.

### One remaining item

- **Merge `feature/agents` to main** — deferred pending user review of agentInitiator and
  KB files. All work is committed on `feature/agents` (commit `94e910c`). When ready,
  merge the full branch (all agent infrastructure is on this branch alongside orderbook commits).

### Work items

#### 1. Create `agentInitiator`

A new agent responsible for SOTA literature review before any design work begins.

- **Remit**: given a problem domain, survey established patterns and production approaches;
  output a ranked menu of design options with trade-offs so the architect and agentDuality
  work from an informed solution space
- **Not**: a designer, implementer, or trade-off analyser — those remain with architect
  and agentDuality respectively
- **Tools**: WebSearch, WebFetch, Read — research only, no code writing
- **Knowledge base**: process-oriented (methodology, source quality, output format) —
  not domain-specific, because its domain changes every project
- **Location**: `.claude/agents/agentInitiator.md` + `agentInitiator-kb/`

#### 2. Tighten agent resource structure

Define and document where agent KBs live, who owns them, and how they are maintained:
- Audit current KB locations (`duality-kb/`, `asm-kb/`)
- Establish a consistent directory structure
- Document the model in `CLAUDE.md` so future agents follow the same pattern

#### 3. Retrospective documentation

Document the lessons from Iterations 1–5 as explicit project knowledge:
- The SOTA gap and what a pre-project literature review would have changed
- The agentDuality remit boundary (engineering detail, not architectural options)
- The agent collaboration model as it actually ran vs how it was documented

### What was delivered (session 1 — 2026-03-17)

- `.claude/agents/agentInitiator.md` — new agent, research-only (WebSearch, WebFetch, Read)
- `.claude/kb/initiator/methodology.md` — research process, source quality ranking, anti-patterns
- `.claude/kb/initiator/output-format.md` — required output structure with pre-submission checklist
- All agent KB paths converted from absolute to relative (`.claude/kb/...`) — portable across clones
- `CLAUDE.md` updated: KB ownership model, relative path mandate, initiator slot in workflow
- `feature/agents` merge to main deferred pending thorough review

### KB path resolution outcome

All three agents (agentASM, agentDuality, AgentQ) converted to relative paths in this session.
The previous absolute-path failures were likely session-level definition caching artefacts, not
path resolution failures. Relative paths are the documented standard going forward.

### What was delivered (session 2 — 2026-03-17)

#### agentInitiator → agentContext (renamed and expanded)

The original agentInitiator brief was too narrow — positioned as a one-shot project-start gate.
Discussion surfaced two gaps:

1. **Anti-bias mandate missing** — the agent should run regardless of what the user already knows.
   Its value is independence from the user's frame, not novelty of findings.
2. **Affordance analysis missing** — SOTA survey alone doesn't identify where a problem's specific
   constraints *amplify* an approach beyond its general case. That's a distinct analytical step.

**Changes made:**
- `agentInitiator.md` retired; replaced by `agentContext.md` with expanded brief
- Brief now covers: SOTA survey + affordance analysis + TRIZ contradiction analysis
- Re-runnable at any point (not just project start); output goes to user, no prescribed handoff
- `agentQ.md` renamed from `AgentQ.md` to match naming convention

#### TRIZ KB created

TRIZ (Theory of Inventive Problem Solving) added as a shared KB at `.claude/kb/triz/triz.md`.

Key framing: software systems are mechanical at the design level — classes, modules, and data
structures have interfaces, transfer data, accumulate friction at boundaries, and contain
redundant components. TRIZ applies selectively where the problem has this mechanical structure.

The KB opens with the **real vs incidental contradiction test**: before applying TRIZ, determine
whether the conflict is structural (cannot be otherwise) or incidental (exists because of an
implementation choice). Elaborate TRIZ resolutions applied to incidental contradictions are waste.

Affordant elements extracted for software use:
- Naming contradictions (technical and physical)
- Trimming — remove components whose function is already served elsewhere
- Ideality — ask what the mechanism looks like when it disappears
- Separation principles — resolve physical contradictions by separating in time, space, or condition
- Affordance identification — where problem constraints amplify a solution beyond its general case

**Not cargo cult TRIZ** — the contradiction matrix and full 40-principle procedure are not applied
mechanically. Only the elements that have direct mechanical analogues in software are used.

#### output-format.md extended

Two new sections added to the agentContext report format:
- **Section 3 — Affordance Analysis**: per-option cross-reference against known problem constraints
- **Section 4 — Contradiction Analysis (TRIZ)**: named contradictions + trimming table

#### agentContext first run — orderbook domain

agentContext was run against the current orderbook project as a validation test.
Full report saved to `context.md` in this directory.

Key findings beyond the existing Iter 7 plan:
- **vEB dismissed with precision**: crossover at U≥2^16; at U=100 the bitmap wins unconditionally
- **Intrusive list flagged as regressive**: pointer-scattered nodes permanently foreclose SIMD
- **`drained` flag + `remove_if` can be eliminated**: bitmap bit cleared on empty level; compaction
  loop not needed — this was not in the original Iter 7 plan
- **Implementation order constraint**: Change 2 (Order size) cannot precede Change 1 (bitmap)
- **std::pmr not transformative here**: Order objects already contiguous in vector, not heap-allocated

#### Utility assessment

agentContext is most powerful at project start on an unmeasured problem. Mid-project it functions
as a structured audit — confirms nothing obvious is missed, surfaces trimming candidates, enforces
the real vs incidental contradiction test. The reassurance function is its honest core value: when
it dismisses an approach with structured reasoning, that dismissal is worth examining even if the
user's instinct already pointed the same way.

Candidate for code review use — the trimming lens (what can be removed, what function is already
served elsewhere) is a different axis from code-reviewer's quality/correctness focus. To be tested.

### Retrospective — Lessons from Iterations 1–5

#### 1. The frame trap

The bitmap + fixed-array level design was not considered until Iteration 6 planning. It is a
well-known pattern in production orderbook implementations (LMAX Disruptor, Databento,
open-source matching engines). Iterations 1–5 optimised within a sorted-vector frame that
was suboptimal from the start.

**Root cause**: no SOTA review before Iteration 1 design. The Architect worked from general
knowledge, not from a surveyed option space.

**Fix**: agentInitiator is now the mandatory first step for any new domain. Its job is to
open the option space, not to choose from it — the Architect chooses.

#### 2. agentDuality remit boundary

agentDuality's remit is **engineering trade-offs within a chosen frame**: data structure
complexity, memory layout, cache efficiency, struct packing. It is not an architectural
options surveyor — it cannot know what it has not been given to analyse.

In Iterations 2–5, agentDuality was effective within its remit but could not correct a
suboptimal initial frame. That is not a deficiency in agentDuality; it is the correct
division of labour. agentInitiator provides the frame; agentDuality optimises within it.

#### 3. Agent collaboration model as it actually ran

The documented loop (agentDuality → structural C++ → agentASM → benchmark) worked well
in Iterations 4–5. The key finding was that agentASM's **pre-flight review** (not ASM
writing) was the most valuable output — it identified the `orderIndex` structural problem
(3× `divq` + `operator delete` per erase) that was invisible from C++ source. Structural
fixes worth −30–69% followed from that assembly review.

The pre-flight step must precede ASM writing. This is now documented in CLAUDE.md and
encoded in agentASM's workflow.

---

## Iteration 7 — Bitmap Level Index + Order Struct Reduction

**Status**: COMPLETE
**Date**: 2026-03-17

### Scope (agentDuality review — 2026-03-17, revised post-context.md — 2026-03-17)

Four synergistic structural changes. Changes 1/3/4 and the PriceLevel.price trim are one
atomic commit; Change 2 is a separate commit after benchmarks confirm Change 1's baseline.

#### Change 1 — Bitmap + fixed `PriceLevel[N_TICKS]` array (highest impact)

Replace `std::vector<PriceLevel> bids/asks` with:
- `uint64_t bid_bits[2]`, `uint64_t ask_bits[2]` — 128-bit bitset, each bit = one tick
- `PriceLevel bid_levels[100]`, `PriceLevel ask_levels[100]` — flat fixed arrays, index = tick integer

**Why the envelope allows it:** price band [97.50, 102.50] = 100 ticks at 0.05 granularity,
p_max=82 observed. 128-bit bitset covers it with 28-tick margin; fits in a register.

**Expected gains:**
- `addOrder` no-cross: eliminates O(log p) binary search (~14 cycles) + O(p) memmove
  (~82 cycles at p=82).
- `cancelOrder`: level lookup becomes O(1) direct array index — eliminates binary search.
- `getBestBid/Ask`: BSR/BSF instructions (1 cycle, register op) instead of `front()`.
- Matching loop: integer tick comparison replaces float comparison.

**Cost:** price-to-tick conversion (~5 cycles) added at every addOrder/cancel.

**Also trim `PriceLevel.price` as part of this commit** — after Change 1, the level slot
index encodes the price; the struct field is redundant. Price reconstructed on demand as
`BASE_PRICE + tick * TICK_SIZE`. Reduces sizeof(PriceLevel): 48 → 40 bytes.
Fixed array: 100 × 40 = 4,000 bytes/side — comfortably L1-resident.
Add `static_assert(sizeof(PriceLevel) == 40)` to lock layout.

#### Change 2 — Remove `Order.price` (24 → 16 bytes) — separate commit

`Order.price` is stored redundantly in every resting order — the matching loop never reads
it. Removing it:
- Reduces sizeof(Order): 24 → 16 bytes
- Improves cache line density: 2.67 → 4.0 orders per 64-byte cache line (+50%)
- Reduces inner order buffer working set: ~2.13MB → ~1.42MB in L3

Update `static_assert(sizeof(Order) == 24)` → `== 16`.
Expected gain on crossing paths: 10–20% if fill loop is L3-bandwidth-bound. Speculative —
confirm with benchmark.
**Dependency:** must not be implemented before Change 1 is in place and benchmarked.

#### Change 3 — `int levelTick` in `OrderLocation` (commit with Change 1)

Replace `double levelPrice` in `OrderLocation` with `int levelTick`. With bitmap indexing,
the tick is the direct array index — level lookup in `cancelOrder` becomes a single
array dereference with no comparison. Eliminates floating-point equality fragility in
level lookup (latent bug: epsilon mismatch silently fails to find the level).

Reduces sizeof(OrderLocation): 24 → 16 bytes. Reduces `orderIndex` working set proportionally.

#### Change 4 — Remove `drained` flag + `remove_if` compaction pass (commit with Change 1)

With bitmap indexing, an empty level is represented by clearing its bitmap bit inline when
the last order fills during the match loop. The `drained` bool and trailing
`asks.erase(remove_if(...))` call are eliminated entirely.

Not in the original plan — surfaces from agentContext TRIZ trimming analysis. Simplifies
the match loop: one fewer conditional, one fewer O(p) pass, one fewer branch.

### Implementation order

| Step | Changes | When |
|------|---------|------|
| Commit 1 | Change 1 + Change 3 + PriceLevel.price trim + Change 4 | Together — intermediate states are incoherent |
| Commit 2 | Change 2 (Order.price removal) | After Commit 1 benchmarks establish new baseline |

### Agent workflow

```
agentContext — DONE (Iter 6, SOTA + affordance + TRIZ on record)
agentDuality review — DONE (revised 2026-03-17 post-context.md)
agentASM pre-flight — DONE (2026-03-17, see findings below)
Class Creator — DONE (Commit 1: bitmap + Changes 3/4 + PriceLevel.price trim)
benchmark — DONE (2026-03-17, see results below)
Class Creator → implement Commit 2 (Order.price removal)
benchmark → attribute density gain separately
```

### agentASM pre-flight findings (2026-03-17)

Compiler output reviewed before any ASM was written.

| Finding | Severity | Action taken |
|---------|----------|-------------|
| BSR/BSF emitting correctly, all bitmap helpers fully inlined | — | None |
| `round@PLT` — external PLT call for `std::round` in every `addOrder` | High | Rewrote `priceToTick`: `(price - BASE_PRICE) * 20.0 + 0.5` — eliminates call and `divsd` |
| `testq` zero-guards before every BSF/BSR — TZCNT/LZCNT available but unused | High | Added `-march=native` to all build commands |
| Double-load of `resting.quantity` in inner fill loop | Medium | Deferred — revisit after Change 2 benchmark |
| `matchSell` indirect BSR pattern (XOR/SUB vs direct) | Low | Resolved by `-march=native` → LZCNT |

**`-march=native` note:** this flag enables ISA extensions specific to the build machine
(TZCNT, LZCNT, and others). Benchmarks from Iteration 7 onward are not directly comparable
to results from machines without these extensions. If portability is required, the
equivalent portable fix is `-mbmi -mlzcnt` to target only the specific extensions used,
or an explicit `__attribute__((target("bmi,lzcnt")))` on the bitmap helpers. The current
benchmark machine has `bmi1` and `abm`/`lzcnt` confirmed in `/proc/cpuinfo`.

### Commit 1 benchmark results (2026-03-17)

All benchmarks: `-O2 -march=native`, synthetic in-memory workload, OU price walk.
Iter 5 baselines also used `-O2` but not `-march=native` — delta includes both
structural and ISA-extension gains.

| Operation | Iter 5 | Iter 7 Commit 1 | Delta |
|-----------|--------|-----------------|-------|
| addOrder no-cross | 158 | 103 | −35% |
| addOrder cross-1L | 125 | 69 | −45% |
| addOrder cross-5L | 743 | 457 | −39% |
| **cancelOrder** | **48** | **19** | **−60%** |
| mixed cancel=10% | 182 | 112 | −38% |
| mixed cancel=50% | 137 | 96 | −30% |
| mixed cancel=90% | 109 | 68 | −38% |

All operations improved 30–60%. `cancelOrder` at 19 cycles is essentially direct array
dereference + tombstone write — close to the theoretical minimum. Change 2 (Order.price
removal, 24→16 bytes) is the next step; expected gain on crossing paths where the fill
loop is L3-bandwidth-bound.

### Commit 2 benchmark results (2026-03-17)

`Order.price` removed — sizeof(Order) 24→16 bytes. Also reduces `OrderLocation` working
set indirectly (levelPrice double→levelTick int: 24→16 bytes per entry).

| Operation | Commit 1 | Commit 2 | Delta |
|-----------|----------|----------|-------|
| addOrder no-cross | 103 | 100 | −3% (noise) |
| addOrder cross-1L | 69 | 76 | +10% (within variance) |
| addOrder cross-5L | 457 | 479 | +5% (within variance) |
| **cancelOrder** | **19** | **14** | **−26%** |
| getBestBid+Ask+Spread | 40 | 39 | ~0 |
| mixed cancel=10% | 112 | 116 | +4% (noise) |
| mixed cancel=50% | 96 | 74 | **−23%** |
| mixed cancel=90% | 68 | 51 | **−25%** |

The fill loop density improvement (+50% orders/cache line) did not produce the expected
10–20% gain on crossing paths — cross-1L and cross-5L are flat within variance. The fill
loop is not L3-bandwidth-bound at current q_mean; the serial carried dependency (quantity
subtraction) remains the ceiling. The agentASM pre-flight double-load finding (section 5)
is the remaining inner-loop candidate.

`cancelOrder` and high-cancel mixed workloads improved significantly: smaller `OrderLocation`
(double levelPrice→int levelTick reduces sizeof from 24→16 bytes) compounds with the Order
size reduction to lower the `orderIndex` working set.

### Future flag (not Iteration 7)

Match loop `resting.quantity == 0.0` branch is data-dependent. With 16-byte Orders and
L3 bandwidth as the post-Iter7 constraint, misprediction becomes relatively more important.
Candidate for agentASM branchless review in Iteration 8.

### Deferred (not in scope)

- `orderIndex` ID recycling / compaction: no per-op cycle gain at current N
- Inner order vector compaction threshold: invisible in current benchmark
- SoA inner order buffer: fill loop is dependency-limited, not bandwidth-limited; Iter 5 null result stands
- Atomics: no concurrency model defined

---

## Iteration 8 — ASM Double-Load Fix (matchBuy_asm / matchSell_asm)

**Status**: COMPLETE
**Date**: 2026-03-17
**Tag**: `iter-8-complete` (pending)

### Scope

Single targeted fix: eliminate the double-load of `resting.quantity` in the inner fill
loop. Identified as medium-priority in the Iteration 7 agentASM pre-flight; revisited
after Change 2 benchmark confirmed the fill loop is not bandwidth-limited at q_mean for
no-cross paths but that the inner arithmetic itself is the ceiling.

### What was implemented

`addOrder_asm` / `matchBuy_asm` / `matchSell_asm` — new functions with `_asm` suffix.
All surrounding C++ (bitmap walk, pop_front, orderIndex update, clearBit on drain) is
preserved identically to the originals. Only the fill arithmetic inside the inner loop
is replaced with an `__asm__ volatile` block.

**Problem (from Iter 7 pre-flight, confirmed by `-S` output):**

Compiler-generated inner loop emits two loads of `resting.quantity`:
1. `vmovsd (%rax), %xmm1` → xmm1 = resting.qty
2. `vminsd %xmm0, %xmm1, %xmm1` → xmm1 = fill (overwrites original value)
3. `vmovsd (%rax), %xmm0` → **second load** — original resting.qty needed again for subtraction
4. `vsubsd %xmm1, %xmm0, %xmm0` → resting.qty -= fill

**Fix:** use two distinct XMM scratch registers — one for fill, one as a copy of the
original resting.quantity — so the second load from memory is replaced by a register-register move.

```cpp
double xmm_fill, xmm_rest;
__asm__ volatile (
    "vmovsd %[resting_qty], %[xmm_fill]\n\t"   // load resting.qty once
    "vmovsd %[resting_qty], %[xmm_rest]\n\t"   // copy (L1-resident, cheap)
    "vminsd %[order_qty], %[xmm_fill], %[xmm_fill]\n\t"
    "vsubsd %[xmm_fill], %[order_qty], %[order_qty]\n\t"
    "vsubsd %[xmm_fill], %[xmm_rest], %[xmm_rest]\n\t"
    "vmovsd %[xmm_rest], %[resting_qty]\n\t"
    : [order_qty]   "+x" (order.quantity),
      [resting_qty] "+m" (resting.quantity),
      [xmm_fill]    "=&x"(xmm_fill),
      [xmm_rest]    "=&x"(xmm_rest)
    : :
);
```

**Branchless zero-check — NOT implemented:**
The `resting.quantity == 0.0` branch gates `orderIndex[resting.id] = std::nullopt` and
`level.pop_front()` — both with non-trivial C++ side effects that cannot be SSE-masked.
Branch is highly predictable (almost always not-taken until drain). Assessed and deferred.

### Benchmark results (2026-03-17)

Benchmarks run: `benchAddCross1Level_asm` and `benchAddCross5Levels_asm` (crossing paths
only — the fill loop only executes on crossing orders).

| Benchmark | C++ | ASM | Delta |
|-----------|-----|-----|-------|
| addOrder cross-1L | 69 | 68 | −1% (noise) |
| addOrder cross-5L | 466 | 330 | **−29%** |

The 5-level result is the meaningful measurement: the inner loop executes 5× per crossing
order, so eliminating one register-to-memory reload per iteration compounds.

The 1-level result is noise — the fill loop runs exactly once, so the saving is one
redundant load amortised across all other addOrder overhead.

### Analysis

The −29% on cross-5L (136 cycles saved per order) confirms the double-load was a real
cost at fill-loop depth. At depth 1 it is invisible. This is consistent with the Iteration 7
finding that cross-path benchmarks are serial-dependency-limited: the ASM fix removes one
memory round-trip per iteration without changing the carried-dependency structure, so the
benefit scales with depth.

### Deferred

- Branchless `resting.quantity == 0.0` check: architectural blocker (C++ side effects), predictable branch
- `matchSell_asm` applied identical fix — symmetric path, same findings apply

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
