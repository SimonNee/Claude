# Cache Behaviour Reference

The memory hierarchy is the most important performance consideration at small N.
An O(n) algorithm on contiguous data routinely beats an O(log n) algorithm on
pointer-chased data when n < a few thousand.

---

## The Memory Hierarchy (typical x86-64 desktop/server)

| Level | Size | Latency (cycles) | Latency (ns) | Notes |
|-------|------|-----------------|--------------|-------|
| Registers | ~1KB | 0 | 0 | Compiler-managed |
| L1 cache | 32–64KB | 4–5 | ~1ns | Per core |
| L2 cache | 256KB–1MB | 10–15 | ~3–5ns | Per core |
| L3 cache | 4–64MB | 40–60 | ~15–20ns | Shared across cores |
| RAM | GBs | 200–300 | ~60–100ns | DRAM latency |

**A cache miss to RAM costs ~200 cycles. A cache hit costs 4.**
This is why layout beats algorithm for small working sets.

---

## Cache Lines

- The unit of transfer between cache and RAM is the **cache line**: 64 bytes on
  all modern x86-64 CPUs.
- When you read one byte, the CPU fetches the entire 64-byte line containing it.
- If the next byte you need is in the same line: free. If it is in a different
  line and not cached: another 200-cycle miss.

**Implication:** pack related data together. If two fields are always read
together, they should be in the same cache line.

---

## Spatial Locality

Accessing memory sequentially lets the hardware **prefetcher** predict and fetch
the next cache lines before you need them, hiding latency almost entirely.

```
// Good — sequential, prefetcher works
for (auto& level : levels) { process(level); }

// Bad — pointer-chasing, prefetcher cannot predict
for (auto* node = map.begin(); node != map.end(); ++node) { process(*node); }
```

A `std::map` traversal dereferences a pointer at each step. Each node may be
anywhere in the heap — the prefetcher gives up. Each node access is a potential
cache miss.

A `std::vector` traversal is sequential. The prefetcher loads ahead. At small N,
the entire vector may fit in L1.

---

## Temporal Locality

Data used recently is likely to be used again. Keeping a working set small
(fitting in L1 or L2) means repeated accesses hit the cache.

**For the orderbook:** the top 5–10 price levels are accessed on every order.
If those levels are contiguous, they stay hot in L1. If they are spread across
heap nodes, they are cold on every access.

---

## Pointer Chasing

Each level of indirection is a potential cache miss:

```
// One indirection — one potential miss per step
std::map<double, std::deque<Order>> bids;
// Tree node → deque metadata → deque chunk → Order

// Zero indirection — one fetch covers multiple elements
std::vector<PriceLevel> bids;
// PriceLevel array → Order array (contiguous within level)
```

Count the pointer dereferences in the critical path. Each one is a potential
200-cycle stall.

---

## Cache Line Sizing for Structs

```cpp
struct Order {
    int    id;        // 4 bytes
    double price;     // 8 bytes
    double quantity;  // 8 bytes
    Side   side;      // 4 bytes (enum)
    // total: 24 bytes — fits ~2.6 per cache line
};
```

With `alignas(64)`:
```cpp
alignas(64) struct Order { ... };
// 24 bytes of data, 40 bytes of padding — 2.6x memory waste
// Only 1 Order per cache line instead of 2
```

`alignas(64)` on a 24-byte struct is counter-productive — it reduces packing
density. See pitfalls.md Pitfall 4.

The correct alignment here is `alignas(8)` (natural alignment of `double`),
which the compiler applies by default.

---

## Working Set Estimation

For the orderbook with p price levels, each holding q orders:

| Structure | Working set (p=50, q=10) |
|-----------|--------------------------|
| `std::map` nodes | 50 × 48 bytes overhead = 2.4KB + tree pointer traversal |
| `std::vector<PriceLevel>` | 50 × sizeof(PriceLevel) = contiguous, fits in L1 |
| `std::deque` per level | 512-byte chunk minimum per level = 25KB minimum |
| `std::vector<Order>` per level | 10 × 24 bytes = 240 bytes per level |

The flat vector layout for 50 levels × 10 orders each ≈ 12KB — fits entirely
in L1 on most modern CPUs.

---

## False Sharing (Multi-threaded Only)

When two threads write to different variables that share a cache line, the
hardware must synchronise the entire line between cores — the "false sharing"
penalty can be as bad as a RAM access.

**This does not apply to single-threaded code.** Padding structs to 64 bytes
to prevent false sharing in a single-threaded context wastes memory and
hurts density. See pitfalls.md Pitfall 5.
