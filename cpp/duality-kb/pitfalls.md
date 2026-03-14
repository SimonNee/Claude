# agentDuality — Pitfalls

Read this before every analysis. These are optimisations that look correct but
measure worse, or improvements that are real but applied in the wrong place.

---

## Pitfall 1 — Optimising Before Measuring

Identifying a hot path by inspection is guessing. The path that looks expensive
is rarely the one the profiler finds. Apply no optimisation — structural or
algorithmic — until a benchmark confirms the target.

**The rule:** Measure first. Always. The iteration plan enforces this: agentDuality
analyses structure, benchmarks run in the next iteration, asm follows after.

---

## Pitfall 2 — Confusing O(log n) with "Slow"

`std::map` is O(log n). A sorted `std::vector` with binary search is also O(log n).
On paper they are equivalent. In practice, for small N, the map is slower because
each node is a separate heap allocation — pointer-chasing, not the algorithm,
is the cost. For large N, the map's tree rebalancing adds overhead the flat array
does not have.

**Do not compare Big-O classes in isolation. Always ask: what is N, and what does
the memory access pattern look like?**

---

## Pitfall 3 — Assuming Contiguous Always Wins

A flat `std::vector` is cache-friendly for sequential access. It is not
cache-friendly if you are inserting in the middle frequently — every insert
shifts elements, blowing the cache line. A structure is only as cache-friendly
as its dominant access pattern.

**Ask: what operation dominates at runtime — read, insert, or delete?**

---

## Pitfall 4 — Over-Aligning

`alignas(64)` aligns a struct to a cache line. If the struct is 12 bytes, the
remaining 52 bytes per instance are wasted padding. For an array of N such
structs, you have just made your working set 5x larger and made cache
utilisation 5x worse.

**Align to cache lines only when false sharing is the confirmed problem, or when
SIMD requires it. Otherwise align to the natural alignment of the largest member.**

---

## Pitfall 5 — False Cache Sharing

Two threads writing to different fields of the same struct, where the struct
fits in one cache line, will ping-pong that cache line between cores. The fix
— padding each field to 64 bytes — wastes memory and hurts single-threaded
density.

**False sharing is a multi-threaded problem. Do not apply the fix to
single-threaded code.**

---

## Pitfall 6 — Optimising the Cold Path

The function called once at startup is not the hot path. The function called
in the inner loop of the matching engine is. Structural changes have a cost
(complexity, code size, maintenance burden) that is only justified on a
confirmed hot path.

**If it is not in the inner loop, leave it alone.**

---

## Pitfall 7 — Hash Maps for Small N

`std::unordered_map` has O(1) average lookup but a large constant: hash
computation, bucket lookup, potential collision chain. For N < ~100, a linear
scan of a `std::vector` is faster because the entire vector fits in L1 cache
and there is no hash overhead.

**For small N, flat and dumb beats clever and indirect.**

---

## Pitfall 8 — AoS vs SoA Is Not Always SoA

Array of Structs (AoS) keeps all fields of one object together. Struct of
Arrays (SoA) keeps all instances of one field together. SoA wins when you
process one field across many objects (SIMD-friendly). AoS wins when you
process all fields of one object at a time.

**For an orderbook matching loop that reads price, qty, and side together per
order, AoS is correct. SoA wins only if you scan prices independently of other
fields.**

---

## Pitfall 9 — Measuring the Wrong Build

Benchmarking a debug build (`-O0`) tells you nothing about production
performance. Optimisations change which operations are hot. Always benchmark
with `-O2` or `-O3` and the same flags used in production.

---

## Pitfall 10 — Mistaking Allocation Cost for Algorithm Cost

A function that calls `new` in a loop is slow because of allocation, not
because of its algorithm. Replacing `std::map` with `std::vector` removes
per-node allocation. The Big-O may be the same; the constant is not.

**When a structure is slower than expected, ask: how many heap allocations does
one operation cause?**

---

## Pitfall 11 — Premature Inlining

Forcing inlining of large functions with `__attribute__((always_inline))` or
`__forceinline` can increase instruction cache pressure and hurt performance.
The compiler's inlining decisions at `-O2` are usually correct. Override only
when profiling shows a specific call overhead.

---

## Pitfall 12 — Ignoring Branch Prediction

A data-dependent branch inside an inner loop (e.g. `if (side == Buy)`) can
stall the pipeline if the branch is unpredictable. Restructuring data so the
branch is eliminated (separate bid/ask arrays, CMOV) can be more valuable than
any algorithmic change.

**Unpredictable branches in inner loops are a layout problem, not an algorithm
problem.**

---

## Pitfall 13 — Analysing Without Knowing the Envelope

A recommendation that is correct for n=50 may be wrong for n=5000. Before
recommending any structure whose cost is O(n) on a dimension, you must establish
what that n is in production — not in test data, not in theory, not by assumption.

**You cannot measure unless you know how long your ruler is.** A benchmark run
against synthetic data that does not match the production distribution is not a
measurement — it is a guess with false precision.

This applies to every domain differently. The dimension and its envelope are
always domain-specific — they come from the problem, not the code:

| Domain | The dimension to bound | Typical envelope |
|--------|-----------------------|-----------------|
| Game entity system | Live entities per frame | Engine budget |
| Network packet queue | Concurrent in-flight packets | Protocol spec |
| Database index | Rows per page / branching factor | Schema and hardware |
| Physics simulation | Active collision pairs | Scene complexity |
| Financial orderbook | Active price levels (p) | Market microstructure |
| Event queue | Pending events per tick | System throughput |

The financial orderbook is one exemplar. The principle is universal.

The envelope is a domain contract, not a code contract. It comes from the
problem owner, not from the code. If you do not have it, ask for it before
giving a verdict.

**If the envelope is unknown, the verdict must be "Measure first" — and the
measurement must be taken against production-representative data, not synthetic
data generated without envelope constraints.**

Synthetic data that does not respect the production envelope (e.g. a free random
walk that creates unbounded price levels when a real book has 20) will produce
benchmarks that are misleading in both directions — making good structures look
bad and bad structures look good.

**The fix is not to distrust benchmarks. The fix is to validate the data before
trusting the benchmark.**
