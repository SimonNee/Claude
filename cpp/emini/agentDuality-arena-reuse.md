# agentDuality Analysis — Arena Reuse Policy

**Subject**: Analysis 2 (Arena + Intrusive List) — Reuse vs No-Reuse slot policy
**Triggered by**: agentContext TRIZ pass, TC-3 and Trimming Candidates 3 and 5
**Dependency**: Analysis 3 (cancel index structure) is blocked on this verdict

---

## agentDuality Analysis

### Current
Data structure: Static arena (flat array of order nodes) + LIFO free list for slot reclamation + cancel index (flat array, order_id → slot location)
Operations of interest: alloc O(1) — pop free list; free O(1) — push free list; cancel lookup O(1) — cancel_index[order_id]; list splice O(1) with position held
Coupled state changes per cancel: 3 — (1) list splice at price level, (2) free-list push for the vacated slot, (3) cancel index invalidation for order_id
Layout: Arena contiguous; free list intrusive (embedded `next_free` in unused slot or separate uint32 stack); cancel index contiguous flat array
Working set estimate: 100K concurrent × 16–32 bytes/node = 1.6–3.2 MB (arena) + 1M session IDs × 4 bytes = 4 MB (cancel index, flat array) + 100K × 4 bytes = 0.4 MB (free list stack) = 6–7.6 MB total — L3

### Proposed
Data structure: Static arena (flat array of order nodes) with monotonic allocation only — no free list, no cancel index. slot_index == order_id by construction; arena[order_id] is the direct dereference.
Operations of interest: alloc O(1) — `return next_slot++`; free O(1) — no-op (slot marked dead, never reclaimed); cancel lookup O(1) — `arena[order_id]` (direct index, no separate structure)
Coupled state changes per cancel: 2 — (1) list splice at price level, (2) dead-mark write on the node
Layout: Arena contiguous; allocation state is one integer (next_slot); zero ancillary structures
Working set estimate: 1M session orders × 16–32 bytes/node = 16–32 MB — L3

### Trade
Time cost: None per-operation. Arena grows to session size at startup. High-water-mark slots (filled/cancelled orders from earlier in the session) occupy arena space but incur no runtime cost — they are never touched again.
Time gain: One coupled state change per cancel eliminated (free-list push removed). Cancel index lookup eliminated entirely. Cancel operation reduces to: one O(1) direct dereference into arena, list splice, dead-mark write.
Space cost: Arena grows from max_concurrent × node_size to session_total × node_size. At established envelope (1M session / 100K concurrent): delta = 900K × node_size = 14.4 MB at 16 B, 21.6 MB at 24 B, 28.8 MB at 32 B.
Space gain: Cancel index eliminated (−4 MB for flat array over 1M session IDs). Free list eliminated (−0.4 MB). Net space change: −4.4 MB saved in ancillary structures, +14.4 to +28.8 MB in arena growth. Net: +10 to +24.4 MB.
Cache impact: Addressed in full under Questions 2 and 3 below.

### Envelope
Two dimensions govern this decision:

**session_total (total orders submitted in one trading session):** Established from the QuantCup canonical reference: 1,010,000 slots. This is the most-cited production-quality reference for ES-style LOB implementation. It is the defensible upper bound for a single active participant. The TRIZ pass's concern about a 100:1 session/concurrent ratio would require 10M session orders — not defensible for a single ES participant; that scenario is outside the stated scope.

**max_concurrent (peak simultaneous live resting orders):** Estimated at 100,000 from the initial report and agentContext. This has not been confirmed from CME data. Confirmation from CME position limit rules or replay data would sharpen the arena size for the reuse policy, but does not affect the no-reuse arena size — which is bounded by session_total, not max_concurrent.

---

## Question 1 — Memory Cost of No-Reuse at Realistic ES Session Volumes

**Defensible upper bound on session order count:** 1,000,000 orders per session for a single active participant on ES. Source: QuantCup canonical reference (1,010,000 arena slots). Consistent with empirical ES HFT volumes at the high end.

**Arena sizes at 1,000,000 session orders:**

| Node size | No-reuse arena | Cache tier |
|-----------|---------------|------------|
| 16 bytes | 16 MB | L3 — fits all modern server CPUs (Intel Xeon Gold/Platinum, AMD EPYC have 16–64 MB L3) |
| 24 bytes | 24 MB | L3 — fits server CPUs with L3 >= 24 MB; all current-generation Xeon/EPYC qualify |
| 32 bytes | 32 MB | L3 edge — fits servers with L3 >= 32 MB; borderline on older or smaller configurations |

**Comparison: total working set across both policies**

| Policy | Arena | Cancel index | Free list | Total | Cache tier |
|--------|-------|-------------|-----------|-------|------------|
| Reuse (100K concurrent) | 1.6–3.2 MB | 4.0 MB | 0.4 MB | 6.0–7.6 MB | L3 (comfortably) |
| No-reuse (1M session) | 16–32 MB | 0 | 0 | 16–32 MB | L3 (at 16B: fits all; at 32B: requires 32 MB L3) |

No-reuse uses 2–4x more total memory. Both policies are L3-resident structures — neither policy lands in L2 (256 KB–1 MB), which is reserved for the bitmap (1.1 KB) and price-level array (32–140 KB) from Analyses 1 and 4. The meaningful distinction is not L2 vs L3 but L3-resident vs RAM-resident.

**Conclusion:** At 16-byte nodes and 1M session orders, the no-reuse arena is 16 MB — L3-resident on all current server CPU targets. Node size is therefore the binding constraint, not the session volume. The initial report's Analysis 5 established that a minimal node `{order_id: uint32, quantity: uint32, next_idx: uint32}` is 12 bytes, padded to 16. Holding node size at 16 bytes keeps the no-reuse policy safe within L3.

---

## Question 2 — Cancel Access Pattern

**Common case (recent cancel):** In ES futures, the cancel-to-add ratio exceeds 90% in active trading; the interval between order submission and cancellation is predominantly sub-second to seconds. These cancels access slots near the current allocation high-water mark — slots written recently and likely warm in L3.

Under no-reuse: recent order → high slot index → warm L3 slot. One access.
Under reuse + cancel index: recent order → cancel_index entry is warm, arena slot is warm. One or two accesses depending on cache line occupancy.

Both policies handle the common case well. Temporal locality is equivalent.

**Cold cancel (order placed early in session, cancelled much later):** This is the exception case — a resting order deep in the book that sat untouched for an extended period. In liquid ES futures, orders near best bid/ask turn over rapidly; deep-book orders may rest for minutes to hours. The fraction of total cancels that are cold is small but non-zero.

Under no-reuse: cold cancel → arena[order_id] — one cache line fetch (the node itself, at a low index, cold). Cost: one L3 miss (15–20 ns) or RAM miss (60–100 ns) depending on whether the slot has been evicted.

Under reuse + cancel index: cold cancel → cancel_index[order_id] — one potentially cold cache line fetch for the location. Then arena[slot] — a second potentially cold cache line fetch for the node. These are two different cache lines with a data dependency: the second fetch cannot begin until the first completes. Cost: one or two L3/RAM misses in series.

**No-reuse is strictly better for cold cancels:** one access instead of two, with no dependency chain. For cancel latency as a first-class requirement, the cold-cancel path under no-reuse is never worse than under reuse + cancel index, and is structurally faster (by one cache-miss-latency-worth of dependency) in the worst case.

**Cancel access pattern summary:**

| Cancel type | No-reuse | Reuse + cancel index |
|-------------|----------|---------------------|
| Recent (< seconds) | 1 warm L3 access | 1–2 warm accesses (equivalent) |
| Medium (minutes) | 1 cool L3 access | 1–2 accesses, potentially independent |
| Cold (hours) | 1 cold L3/RAM access | 2 serial cold accesses with dependency chain |

---

## Question 3 — Crossover Condition

**When does no-reuse become unacceptable?**

No-reuse becomes unacceptable when `session_count × node_size > L3_size`, causing the arena to spill to RAM. Once a cold cancel requires a RAM fetch, the latency is 60–100 ns rather than 15–20 ns for an L3 hit.

Note: when the no-reuse arena exceeds L3, the cancel index under the reuse policy also exceeds its effective warm range (the cancel index is 4 MB; warm-slot access under reuse degrades too as the session progresses). The crossover is not "no-reuse is bad, reuse is fine" — it is "both policies degrade, but reuse degrades less severely because its total working set is smaller."

**Crossover thresholds by node size:**

| Node size | No-reuse safe when | Crossover (L3 = 16 MB) | Crossover (L3 = 32 MB) |
|-----------|-------------------|------------------------|------------------------|
| 16 bytes | session_count <= L3 / 16 | 1,000,000 — at boundary | 2,000,000 — above ES envelope |
| 24 bytes | session_count <= L3 / 24 | 666,000 — below ES upper bound | 1,333,000 — above ES upper bound |
| 32 bytes | session_count <= L3 / 32 | 500,000 — inside ES envelope | 1,000,000 — at boundary |

**Stated crossover:** No-reuse is acceptable when `session_count × node_size <= L3_size`. For ES at 1M session orders:
- 16-byte nodes: requires L3 >= 16 MB. Met unconditionally on target hardware.
- 24-byte nodes: requires L3 >= 24 MB. Met on current-generation server CPUs.
- 32-byte nodes: requires L3 >= 32 MB. Met on mid-to-high end server CPUs; not guaranteed on all targets.

**The L2 boundary does not apply here.** Neither the no-reuse arena (16–32 MB) nor the reuse arena + cancel index (6–7.6 MB) fits in L2. The L2 tier is reserved for the price-level array and bitmap. The correct framing is L3-resident vs RAM-resident, not L2/L3 warm vs L3-cold.

---

## Question 4 — Verdict

**RECOMMEND — no-reuse policy**, conditional on node size held at or below 16 bytes and hardware target with L3 >= 16 MB.

**Primary justification:**

1. **Cancel latency is first-class.** No-reuse eliminates the cancel index lookup, reducing the cancel operation's memory access count from two serial fetches (cancel_index[order_id] then arena[slot]) to one (arena[order_id] direct). For cold cancels, this eliminates a serialised dependency chain. The benefit is structural and does not require profiling to establish.

2. **Structural simplification is concrete.** Eliminating two data structures (cancel index, free list) and reducing per-cancel coupled writes from three to two has direct correctness consequences. TRIZ TC-2 identified the three-way cancel coupling as a consistency risk. Reducing to two coupled operations removes one class of partial-execution bugs.

3. **The QuantCup canonical reference uses no-reuse.** `arenaBookEntries[1,010,000]` is a monotonic-allocation arena sized to session total, with no free list for intra-session reclamation. The most-cited production-quality reference for this exact problem already made this choice.

4. **Memory cost is contained within L3 at 16-byte nodes.** 16 MB at the 1M-order upper bound fits in L3 on all modern server CPUs targeted for ES matching. The cost is real (+10–24 MB over reuse + cancel index) but acceptable given the hardware envelope.

**The condition under which reuse policy is correct:**

Reuse (arena + free list + cancel index) is the correct choice if:
- Node size cannot be held at or below 16 bytes (Architect requires additional fields, growing nodes to 24–32 bytes), AND
- Hardware target has L3 < 24 MB.

Or: if multi-session operation is required and order IDs increment monotonically across sessions, making session_total unbounded. A single-session LOB does not face this condition.

---

## Implication for Analysis 3

Analysis 3 in the initial report asked: flat array vs hash map for the cancel index. With no-reuse policy recommended, **Analysis 3 is dissolved** — the cancel index does not exist.

Analysis 3 is replaced by **Analysis 3-revised**: given that arena[order_id] provides the order node directly, does the node contain a `prev_idx` field (doubly-linked intrusive list, O(1) list splice) or only `next_idx` (singly-linked, O(q) predecessor scan before splice)? This is the remaining structural decision for cancel latency, and it is independent of the reuse policy decision. The Architect should receive this as an open conditional, not a resolved question.

---

### Pitfalls Checked

- **Pitfall 1 (optimise before measuring):** The analysis is structural — it prices memory cost and access count of two specific policies. The cold-cancel access-count argument (1 vs 2 dependent accesses) is deterministic from the data structure definitions and does not require a benchmark to establish.
- **Pitfall 6 (optimising cold path):** Cold cancels are a cold sub-path within cancel. Cancel latency is a declared first-class requirement. Analysing the cold-cancel sub-path is within scope.
- **Pitfall 13 (envelope unknown):** Session order count is established from the QuantCup canonical reference (1,010,000). Concurrent order count (100K) is an estimate; the verdict is robust to concurrent counts between 10K and 200K.
- **Pitfall 16 (bimodal distributions break pre-allocation):** No-reuse arena is a flat session-wide pool, not per-level pre-allocation. Not applicable.

---

*Sources: agentContext-initial-report.md, agentDuality-initial-report.md, agentContext-triz-pass.md, .claude/kb/duality/ KB files, .claude/kb/process/lessons.md. No external sources consulted.*
