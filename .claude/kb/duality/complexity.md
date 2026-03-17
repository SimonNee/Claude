# Complexity Reference

Time and space complexity for common C++ containers, shown side by side.
Big-O is necessary but not sufficient — always read alongside cache.md.

---

## Container Comparison Table

| Container | Insert | Lookup | Delete | Space | Layout |
|-----------|--------|--------|--------|-------|--------|
| `std::map` | O(log n) | O(log n) | O(log n) | O(n) nodes | Pointer tree — non-contiguous |
| `std::unordered_map` | O(1) avg | O(1) avg | O(1) avg | O(n) + bucket array | Hash buckets — non-contiguous |
| `std::vector` (unsorted) | O(1) amort end / O(n) middle | O(n) | O(n) | O(n) | Contiguous |
| `std::vector` (sorted) | O(log n) search + O(n) shift | O(log n) binary search | O(n) shift | O(n) | Contiguous |
| `std::deque` | O(1) front/back | O(1) index | O(1) front/back / O(n) middle | O(n) + chunk ptrs | Chunked — near-contiguous |
| `std::list` | O(1) anywhere (with iterator) | O(n) | O(1) with iterator | O(n) nodes | Pointer chain — non-contiguous |
| `std::priority_queue` | O(log n) | O(1) top | O(log n) | O(n) | Contiguous heap |

---

## Worked Example — C++ Orderbook

The orderbook is a concrete exemplar. The principles apply to any C++ codebase
with a sorted, keyed, multi-value container on a hot path.

### Iteration 1 — `std::map<double, std::deque<Order>>`

| Operation | Time | Space | Notes |
|-----------|------|-------|-------|
| `addOrder` (no match) | O(log p) | O(1) per order | p = number of price levels |
| `addOrder` (matching) | O(p * q) | — | p levels crossed, q orders per level |
| `cancelOrder` | O(p * q) | — | Linear scan — no index |
| `getBestBid` / `getBestAsk` | O(1) | — | Map iterator to begin |
| `getSpread` | O(1) | — | Two begin() calls |
| Space per price level | — | ~48 bytes node overhead | Each map node = heap allocation |
| Space per order | — | ~32 bytes (Order struct) | Plus deque chunk overhead |

### Iteration 2 Target (Exemplar) — `std::vector<PriceLevel>` (sorted flat array)

| Operation | Time | Space | Notes |
|-----------|------|-------|-------|
| `addOrder` (no match) | O(log p) search + O(p) shift | O(1) per order | Shift cost is O(p) but p is small and sequential |
| `addOrder` (matching) | O(p * q) | — | Same class, cache-friendly traversal |
| `cancelOrder` | O(p * q) | — | Still linear — id index deferred |
| `getBestBid` / `getBestAsk` | O(1) | — | front() or back() of vector |
| `getSpread` | O(1) | — | Two array accesses |
| Space per price level | — | sizeof(PriceLevel), no overhead | No heap allocation per level |
| Space per order | — | sizeof(Order) | Contiguous within level |

---

## The Key Trade-off: Map vs Sorted Vector

Both are O(log n) for search. The difference is constant factors and allocation:

```
std::map insert:
  1. Allocate new node (heap call)
  2. Traverse tree — each step follows a pointer (cache miss likely)
  3. Rebalance (pointer updates)

sorted vector insert:
  1. Binary search (sequential — prefetcher works)
  2. Shift elements right (memcpy — one cache miss amortised over many elements)
  3. Write new element
```

For a typical orderbook with p < 100 active price levels, the sorted vector
will be faster because all levels fit in L1/L2 and there are no heap allocations
per operation.

The crossover point where the map wins (amortised rebalance beats memcpy shift)
is approximately p > 10,000 for modern CPUs — well beyond any realistic orderbook.

---

## Space Complexity Notes

- `std::map` node: ~48 bytes overhead per node (left, right, parent pointers +
  colour bit + value). For 100 price levels: ~4.8KB in tree metadata alone.
- `std::vector<PriceLevel>`: zero per-element overhead beyond the struct itself.
  100 price levels at 32 bytes each = 3.2KB, fully contiguous.
- `std::deque` block size: typically 512 bytes per chunk. Small queues waste most
  of the first chunk.
- Reordering Order struct members (doubles first) reduces sizeof(Order) from
  32 to 24 bytes — a 25% saving with no functional change.
