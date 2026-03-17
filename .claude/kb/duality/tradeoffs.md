# Trade-off Phrasebook

Indexed by pattern. For each: what you have, what to consider, what the trade is.
Read pitfalls.md before applying any of these.

---

## Pattern 1 — Sorted Map → Sorted Flat Vector

**You have:** `std::map<K, V>` used for ordered iteration and O(log n) lookup.

**Consider:** `std::vector<std::pair<K,V>>` kept sorted, binary search for lookup.

| Dimension | std::map | sorted vector |
|-----------|----------|---------------|
| Lookup | O(log n) | O(log n) |
| Insert (no resize) | O(log n) + alloc | O(log n) search + O(n) shift |
| Insert (resize) | O(log n) + alloc | O(n) copy + O(n) shift |
| Delete | O(log n) | O(n) shift |
| Ordered iteration | O(n) — pointer chase | O(n) — sequential |
| Space | O(n) nodes, ~48 bytes overhead each | O(n) elements, zero overhead |
| Cache on iteration | Poor — each node is a pointer dereference | Excellent — sequential |
| Cache on lookup | Poor — tree traversal | Good — binary search is sequential within each half |

**When the trade is worth it:** n < ~10,000 and reads/iterations dominate over inserts.
For an orderbook with < 200 price levels, the sorted vector wins clearly.

**When it is not:** insert-heavy workloads at large n where O(n) shift becomes the bottleneck.

---

## Pattern 2 — std::deque → std::vector (per price level queue)

**You have:** `std::deque<Order>` as the FIFO queue at each price level.

**Consider:** `std::vector<Order>` with a head index (logical pop_front without shifting).

| Dimension | std::deque | vector + head index |
|-----------|------------|---------------------|
| push_back | O(1) amort | O(1) amort |
| pop_front | O(1) | O(1) — increment head index |
| Random access | O(1) | O(1) |
| Space | Chunked (512-byte blocks minimum) | Contiguous, exact size |
| Cache | Near-contiguous (chunk boundaries cause misses) | Fully contiguous |
| Memory reclaim on pop | Not immediate | Not immediate (index advances) |

**Trade:** a vector with a head index wastes space at the front after pops
(consumed orders leave holes). For a price level that fills completely and
is then removed, this is fine — the vector is discarded. For levels with
many partial fills, periodic compaction may be needed.

**When the trade is worth it:** price levels that either fill completely or
have small queues (< ~20 orders). This is the common case in a liquid market.

---

## Pattern 3 — Pointer-Based Node → Index-Based Reference

**You have:** pointers to orders stored in a map (implicit, via iterator or node).

**Consider:** a flat order pool (`std::vector<Order>`) with orders referenced by
index (int, 4 bytes) rather than pointer (8 bytes).

| Dimension | Pointer | Index |
|-----------|---------|-------|
| Size | 8 bytes | 4 bytes |
| Dereference cost | Potential cache miss (heap pointer) | Array lookup (likely cached) |
| Invalidation | On vector resize | On pool resize (safer with reserved capacity) |
| Cancel by id | O(n) scan | O(1) with id→index map |

**Trade:** index references require a stable backing store (no reallocation after
the index is handed out). Use `reserve()` to pre-allocate, or accept that
outstanding indices are invalidated on resize.

**When the trade is worth it:** cancel-heavy workloads where O(1) lookup by id
is needed. The flat pool also enables direct SIMD iteration over all live orders.

---

## Pattern 4 — Dynamic Allocation → Pre-allocated Pool

**You have:** per-order `new` allocation (implicit in map node or deque chunk creation).

**Consider:** a fixed pool of Order objects allocated once at startup.

| Dimension | Dynamic alloc | Pool |
|-----------|---------------|------|
| Allocation cost | O(1) amort, but system call path | O(1), array index |
| Fragmentation | Grows over time | None |
| Cache | Orders scattered across heap | Orders contiguous |
| Max capacity | Unbounded | Fixed at pool size |

**Trade:** pools require a known upper bound on simultaneous live orders. For a
limit orderbook this is a reasonable constraint (max open orders per session).

**When the trade is worth it:** Iteration 3+ when benchmarks confirm allocation
is on the hot path. Do not pre-optimise.

---

## Pattern 5 — Separate Bid/Ask Containers → Unified Level Array

**You have:** two separate containers — one for bids, one for asks.

**Consider:** one unified array of price levels, with a split point (best bid /
best ask boundary).

**Trade:** simpler iteration for SIMD (one contiguous array), but more complex
boundary logic. The split point must be maintained correctly.

**When the trade is worth it:** Iteration 4 SIMD scan, where a single `cmpps`
sweep over a unified price array is more efficient than two separate scans.
Not worth it until that iteration.

---

## Pattern 6 — AoS Order → SoA Price Extraction

**You have:** `Order` structs with price, qty, id, side together (AoS).

**Consider:** extracting the price field into a separate contiguous array for
SIMD scanning, keeping the full Order array for detail access.

```cpp
// SoA split for SIMD price scan
std::vector<double> levelPrices;   // for SIMD comparison
std::vector<PriceLevel> levels;    // for full detail
```

**Trade:** two arrays to keep in sync. Every insert/delete must update both.
Read pattern improves for price-only scans; write pattern gets more complex.

**When the trade is worth it:** Iteration 4, when `cmppd` sweep over a price
array is the target. Not before.

---

## The Envelope Question

Before giving any verdict on a pattern that is O(n) on a dimension, ask:

> **"What is n bounded by in production, and where does that bound come from?"**

If the answer is "I don't know" or "the test data says so" — the verdict must be
"Measure first" and the measurement must be taken against production-representative
data. Synthetic data that does not respect the production envelope is not a ruler;
it is a guess. See pitfalls.md Pitfall 13.

The envelope is a domain contract. It belongs in the problem specification, not
in the code. If it is not written down, it does not exist yet.

---

## Quick Reference — Common Questions

| Question | Answer |
|----------|--------|
| My map is slow. Should I use unordered_map? | Only if lookup is the bottleneck AND n > ~100. Measure first. |
| Should I align my structs to 64 bytes? | No, unless SIMD or false sharing is the confirmed issue. |
| Is SoA always better for SIMD? | Only if you scan one field independently. Mixed access = AoS. |
| My deque is slow at small n (< ~20 inner items). | Replace with vector. Deque chunk overhead dominates at small n. |
| My vector is slow at large n with frequent push_back. | Deque may win — it avoids O(n) reallocation-copy events. Measure the dominant operation first. |
| Should I use a pool allocator? | Not until allocation is confirmed on the hot path by a profiler. Pool eliminates malloc overhead but NOT reallocation-copy cost. |
| Can I reserve(N) to avoid reallocation? | Only if the inner count distribution is not bimodal. Uniform reserve on a bimodal distribution wastes capacity on shallow instances. See Pitfall 16. |
| Both outer and inner counts must be known before a working-set estimate is valid. | Pitfall 14 — instrument both dimensions. An unvalidated inner count invalidates the entire cache-level analysis. |
| How many price levels before sorted vector insert is slow? | ~10,000. But what is p in your domain? Know before you assume. |
