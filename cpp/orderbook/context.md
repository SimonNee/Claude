# agentContext Report — Iteration 7 Pre-flight

**Generated**: 2026-03-17
**Purpose**: SOTA survey + affordance + TRIZ analysis for re-evaluation of Iteration 7 scope.
**Domain**: Single-instrument CLOB, C++, single-threaded. OU price band 100 ticks, p_max=82, q_mean=1085.

---

## Solution Space

| Option | Core idea | Wins when | Evidence |
|--------|-----------|-----------|----------|
| 1 — Sorted vector (current) | Binary search + memmove on insert | p variable, unbounded range | This project, measured |
| 2 — Bitmap + fixed array (planned) | tick → direct array index; BSR/BSF for best bid/ask | Tick range bounded, compile-time known | PIYUSH-KUMAR1809 ~160M orders/s; agentDuality |
| 3 — std::map | Red-black BST, per-node heap alloc | p large, unbounded, sparse | This project Iter 1, measured |
| 4 — Van Emde Boas | O(log log U) successor/predecessor | U ≥ 2^16 (65536 ticks) | bigfatwhale/orderbook |
| 5 — Intrusive list + arena | Pointer-based O(1) cancel; zero heap alloc per order | 90%+ cancel rate; pool allocator in place | Leotaby/MicroExchange |
| 6 — SoA order layout | Contiguous quantity array; SIMD-friendly inner fill loop | q_mean large, fill-loop L3-bandwidth-bound | Iter 5 null result; parallelprogrammer |
| 7 — std::pmr monotonic buffer | Pointer-bump allocation; zero per-order heap cost | Orders individually heap-allocated | PIYUSH-KUMAR1809 |

---

## Affordance Analysis

### Option 2 (Bitmap + fixed array) — strongest affordances

**Affordance A — Bitmap fits in a register**
- Constraint: price band = 100 ticks, fits in 2× uint64 with 28 ticks margin
- Effect: BSR/BSF operates on a register value — 1 cycle, no memory access. Not just fast: essentially free.
- Compounds with: OU reversion keeps active region near centre — deterministic 1-cycle latency, no branch misprediction

**Affordance B — Fixed array is L1-resident**
- Constraint: p_max=82, N_TICKS=100. Array = 100 × sizeof(PriceLevel) ≈ 4KB at current struct size
- Effect: Level lookup is a single cache-hit array dereference. No binary search, no comparison.
- Compounds with: Integer tick arithmetic replaces float comparison — eliminates epsilon fragility latent bug

**Affordance C — Integer representation**
- Constraint: tick granularity = 0.05, fixed, known at compile time
- Effect: price → tick conversion (`(price - BASE) / TICK_SIZE`) done once at addOrder. All downstream ops use int.
- Compounds with: Eliminates OrderLocation.levelPrice float equality fragility (latent bug, noted in Iter 7 plan)

### Option 4 (Van Emde Boas) — affordance works against it
- At U=100, bitmap wins unconditionally. vEB's asymptotic advantage (O(log log U)) requires U ≥ 2^16.
- The problem is too small for vEB to exhibit its advantage. Do not revisit unless tick range changes fundamentally.

### Option 5 (Intrusive list) — marginal at best
- Cancel is already O(1) at 48 cycles (Iter 4 flat array). Intrusive list gives pointer-based cancel (2 writes) vs tombstone (1 write + decrement). Not transformative.
- Permanently forecloses SIMD on inner fill loop — pointer-scattered nodes defeat contiguous access. **Regressive.**

### Option 6 (SoA) — not compounding at current size
- Iter 5 measured: prefix-scan (SIMD prerequisite) gave cross-1L −10%, cross-5L +7% — within noise.
- Fill loop is dependency-limited, not bandwidth-limited. SoA requires breaking the carried dependency first.
- 24→16 byte Order (Iter 7 Change 2) changes stride but does not break the carried dependency.
- SoA is a future structural direction, not an Iter 7 compound.

### Option 7 (std::pmr) — not transformative here
- Order objects already live in contiguous `vector<Order>` per level — not individually heap-allocated.
- Primary allocation overhead (orderIndex `unordered_map` erase) already eliminated in Iter 4.
- Remaining: infrequent `vector` realloc events. Not a hot-path bottleneck at current benchmark scale.

---

## Contradiction Analysis (TRIZ)

### Contradiction 1 — Level lookup speed vs index completeness
- **Type**: Physical (must be both large/complete and small/register-sized)
- **Resolved by**: Option 2. Principle 16 (excess): 128-bit covers 100 ticks, surplus is free. Principle 13 (inversion): tick integer *is* the index. Principle 5 (merging): bitmap + array gives membership test and level access in one structure.
- **Result**: Contradiction dissolved, not traded off.

### Contradiction 2 — Order.price redundancy vs cache density
- **Type**: Technical (self-description improves with price field; cache density degrades)
- **Resolved by**: TRIZ Trimming. `Order.price` function already served by level array index after bitmap change. Remove it: 24→16 bytes, +50% cache line density, working set ~1.88MB→~1.25MB.
- **Dependency**: Cannot trim before bitmap change is in place (level address must be derivable from tick integer alone).

### Contradiction 3 — Float equality fragility vs conversion cost
- **Type**: Technical (integer tick = correct; requires conversion at insert)
- **Resolved by**: Principle 35 (parameter change): price → tick once at addOrder. Principle 10 (prior action): conversion amortised across all subsequent ops.
- **Result**: Latent bug eliminated by representation change, not defensive programming.

### Trimming table

| Component | Current function | Served by after Change 1 | Trim? |
|-----------|-----------------|--------------------------|-------|
| `Order.price` | Price at which order rests | Level array index (tick int) | Yes — Change 2 |
| `OrderLocation.levelPrice` (double) | Level lookup key in cancelOrder | `levelTick` (int) direct index | Yes — Change 3 |
| Binary search in findBid/AskLevel | Find level in sorted vector | `levels[tick]` direct dereference | Yes — function eliminated |
| `drained` flag + `remove_if` pass | Compact empty levels after match | Bitmap bit cleared when level empties | Yes — compaction loop eliminated |

**Four components eliminated by the bitmap change.** The `drained` flag removal was not in the original Iter 7 plan — it is an additional trim that falls out naturally.

---

## Framing Risks for Iteration 7

**Risk 1 — Implementation order: Change 1 must precede Change 2**
Trimming `Order.price` depends on the bitmap being in place. If Change 2 is done first as a "quick win", cancelOrder loses its level lookup key. Do not implement in isolation.

**Risk 2 — Do not conflate outer index change with inner order buffer layout**
Bitmap (Change 1) addresses the outer level index. SoA for inner order buffer is a distinct future change. Implementing both together creates an unattributable benchmark and a harder rollback.

**Risk 3 — Bitmap must be compile-time fixed, not runtime dynamic**
If N_TICKS is a runtime parameter (heap-allocated), BSR operates on memory, not a register — the primary affordance is lost. `uint64_t bid_bits[2]` must be a fixed struct member.

**Risk 4 — Do not switch to intrusive list for inner order queue**
Pointer-scattered nodes permanently foreclose SIMD on the inner fill loop. The current `vector<Order>` tombstone approach (48 cycles/cancel) is already near-optimal and preserves future SIMD options.

---

## Additions to Iteration 7 Scope (from this analysis)

The following was not in the original Iter 7 plan but falls out of the TRIZ trimming analysis:

- **Remove `drained` flag + `remove_if` compaction pass** — with bitmap indexing, an empty level is represented by its bitmap bit being 0. No separate compaction scan required. This simplifies the matching loop.

---

## Sources

| Source | Quality |
|--------|---------|
| This project status.md + measured benchmarks | Rank 1 |
| github.com/PIYUSH-KUMAR1809/order-matching-engine | Rank 1 — C++20, ~160M orders/s, bitmap + pmr |
| github.com/bigfatwhale/orderbook | Rank 1 — vEB tree, fixed-point pricing |
| github.com/Leotaby/MicroExchange | Rank 1 — intrusive list, arena allocator |
| github.com/aspone/OrderBook | Rank 1 — std::map + unordered_map cancel |
| gist.github.com/halfelf (How to Build a Fast Limit Order Book) | Rank 3 — widely cited practitioner reference |
| martinfowler.com/articles/lmax | Rank 3 — LMAX architecture, pre-allocated memory |
| parallelprogrammer.substack.com (ITCH SIMD project) | Rank 3 — SoA/SIMD layout work |
| en.wikipedia.org/wiki/Van_Emde_Boas_tree | Rank 5 — academic reference |
