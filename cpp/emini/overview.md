# E-mini S&P 500 Limit Order Book — Technical Overview

## Glossary

| Term | Meaning |
|---|---|
| **Bid** | An order to buy at a specified price or lower |
| **Ask** | An order to sell at a specified price or higher |
| **Resting order** | An order sitting in the book waiting to be matched — it has not yet found a counterparty |
| **Aggressor** | An incoming order that is willing to trade immediately at the current market price — it crosses the spread and triggers a match |
| **Spread** | The gap between the lowest resting ask price and the highest resting bid price |
| **Tick** | The minimum price increment ($0.25 for ES). All prices are exact multiples of one tick |
| **Price level** | All resting orders at the same price, held as a FIFO queue |
| **Price-time priority** | The matching rule: lower ask prices (or higher bid prices) fill first; within the same price, earlier orders fill first |
| **Session** | One trading day. The arena is sized for up to 1,000,000 orders per session |
| **Arena** | A pre-allocated block of memory from which order nodes are dispensed one at a time — no heap allocation on the hot path |
| **Dead node** | An order node whose order has been cancelled or filled. It stays in the arena but is skipped during matching |
| **Bitmap** | A compact bitset (one bit per price level) used to locate the best bid or ask in a single CPU instruction |
| **TZCNT / LZCNT** | x86 instructions that count trailing / leading zero bits — used to find the first set bit in a bitmap word in one cycle |

---

## What It Is

A limit order book for the CME E-mini S&P 500 futures contract (ES), implemented in C++. The book maintains resting bid and ask orders organised by price level with time priority (FIFO) within each level. The three operations are **add** (place a resting order), **cancel** (remove a resting order by its ID), and **match** (execute an incoming aggressor order against resting orders on the opposite side).

A parallel C implementation was built and benchmarked head-to-head to quantify the cost of language boundary constraints on register allocation. That work is complete; the C code is preserved at tag `version-0.3-C-remove`. Active development is C++ only.

The design target is O(1) for all three operations with latencies in the 40–75 cycle range on modern hardware.

---

## The Central Idea

The E-mini contract has two properties that make a fast book possible:

1. **Prices are discrete.** The tick size is $0.25. Every valid price is an exact multiple of $0.25, so any price maps to an integer tick index: `tick = (uint32_t)((price - base) * 4.0 + 0.5)`. There are at most 8,800 valid ticks across the ±20% CME hard limit.

2. **The active range is narrow.** In a live session, orders cluster within ±$50 (~200 ticks) of the current price. The book is almost always sparse.

These two facts allow the entire price space to be represented as a **flat array indexed directly by tick**. There is no hash map, no tree, no search — a price level is a direct array dereference.

---

## Data Structures

### Order Node — 16 bytes

```
order_node_t / order_node (C)
┌─────────────┬─────────────┬─────────────┬───────┬──────────┐
│  order_id   │  quantity   │  next_idx   │ flags │  pad[3]  │
│  uint32_t   │  uint32_t   │  uint32_t   │ uint8 │          │
└─────────────┴─────────────┴─────────────┴───────┴──────────┘
```

`order_id` equals the slot index in the arena — this identity is load-bearing (see Cancel below). `next_idx` is an index (not a pointer) into the same arena, forming an intrusive singly-linked FIFO. `flags` carries a single `DEAD_FLAG` bit for cancelled/filled nodes. The 16-byte size keeps four nodes per cache line.

### Price Level — 16 bytes

```
price_level_t
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  head_idx   │  tail_idx   │    count    │  total_qty  │
│  uint32_t   │  uint32_t   │  uint32_t   │  uint32_t   │
└─────────────┴─────────────┴─────────────┴─────────────┘
```

`head_idx` and `tail_idx` are arena indices forming the FIFO queue for this price level. `count` and `total_qty` are maintained synchronously on every add/cancel/fill.

### Book Side — 141,904 bytes

```
book_side_t
┌──────────────────────────────────────────┐
│  levels[8800]   price_level_t × 8800     │  140,800 bytes
│  bitmap[138]    uint64_t × 138           │   1,104 bytes
└──────────────────────────────────────────┘
```

`levels` is the flat array — `levels[tick]` is the price level for that tick, O(1) by construction. `bitmap` is a companion 8,800-bit bitset: bit `t` is set if and only if `levels[t]` has resting orders. The bitmap is 1,104 bytes and is L1-resident at all times.

### Arena — 16,000,004 bytes (~16 MB)

```
arena_t
┌──────────────────────────────────────────┐
│  nodes[1,000,000]   order_node_t × 1M   │  16,000,000 bytes
│  next_slot          uint32_t            │          4 bytes
└──────────────────────────────────────────┘
```

A monotonic bump allocator. `alloc` is `slot = next_slot++`. There is no free list and no deallocation — cancelled nodes are dead-marked in place. The arena fits in L3 (16 MB); hot nodes (recently added, not yet cancelled) will be in L1/L2.

### Book — ~16.3 MB total

Two `book_side_t` (bid + ask) plus one `arena_t` shared across both sides, plus a `double base_price`. The whole structure lives on the heap.

---

## Why It Is Fast

**O(1) add.** `levels[tick]` is a direct array dereference. The new node is written to `arena.nodes[next_slot++]` (one store), appended to the level's FIFO via `tail->next_idx = slot; level.tail_idx = slot` (two stores), and the bitmap bit is set with a single `OR` into one word. No allocation, no search.

**O(1) cancel.** The caller tracks `(side, tick, order_id)` at add time. Cancel is: `arena.nodes[order_id]` (one load — the identity `slot == order_id` makes this a direct dereference), then a linear scan forward from `level.head_idx` to find and unlink the node. The bitmap is cleared if the level becomes empty. The scan is O(q) in queue depth q, but in practice q is small (the cancel rate exceeds the add rate by 10:1, keeping queues short).

**O(1) match.** The bitmap locates the best ask or bid price (the cheapest ask or highest bid) in one `TZCNT`/`LZCNT` instruction on the first non-zero word — typically the very first word checked in a live book. The match loop then drains the FIFO at that level, consuming one resting order per fill until the aggressor quantity is satisfied.

**Cache layout.** The bitmap (1,104 bytes) stays in L1. The active price levels cluster in 4–8 cache lines around the current price. The arena nodes for recent orders are in L1/L2. A cancel on a recently-added order touches L1 throughout.

**No dynamic allocation on the hot path.** Arena bump allocation is a single increment. There are no `malloc`/`free` calls after session initialisation.

**Integer arithmetic throughout.** The one float-to-integer conversion happens at the API boundary (`price_to_tick`). Every subsequent operation is on `uint32_t` tick indices and quantities. No floating-point in any hot-path function.

---

## The Three Operations

### Add
1. Convert price to tick (API boundary — one cast)
2. Allocate node: `slot = arena.next_slot++`
3. Write node fields: `order_id = slot`, `quantity`, `next_idx = NULL_IDX`, `flags = 0`
4. Append to level FIFO: update `tail->next_idx` and `level.tail_idx`
5. If level was empty: set bitmap bit, update `level.head_idx`
6. Update `level.count` and `level.total_qty`
7. Return `order_id` to caller

### Cancel
1. Validate: `order_id < MAX_ORDERS`, `tick < MAX_TICKS`, node not already dead
2. Load node: `node = &arena.nodes[order_id]`
3. Scan FIFO from `level.head_idx` to find and unlink `node`
4. Mark node dead: `node.flags |= DEAD_FLAG`
5. Update `level.count` and `level.total_qty`
6. If level now empty: clear bitmap bit, set `head_idx = tail_idx = NULL_IDX`

### Match
The caller supplies an aggressor order (side, price, quantity). The matcher finds resting orders on the opposite side that are willing to trade at that price.

1. Find the best resting price on the opposite side via bitmap scan (`TZCNT`/`LZCNT` on first non-zero word)
2. Check crossing condition: does the aggressor's price reach the best resting price? (For a bid aggressor: bid price ≥ best ask; for an ask aggressor: ask price ≤ best bid)
3. If prices cross: drain the FIFO at that price level, filling resting orders oldest-first. Record each fill as a `fill_t` (maker order ID, quantity traded). Mark filled nodes dead.
4. If the level empties: clear its bitmap bit, advance to the next best price
5. Repeat until the aggressor's quantity is fully consumed or no more crossing prices exist
6. Return all fill records and any remaining unfilled aggressor quantity

The matcher is a named, separate module (`matcher.c` / `matcher.cpp`). It is the only component with simultaneous read/write access to both sides of the book. It does not get called by `add` — a resting order never triggers a match; only an aggressor does.

---

## Module Map

```
cpp/emini/cpp/
├── book.hpp        Types, constants, Book class, bitmap_lowest/bitmap_highest templates
├── internal.hpp    Package-internal helpers: bitmap set/clear, arena alloc, queue ops (inline)
├── book.cpp        Book constructor, add, cancel, match, reset
├── matcher.hpp     Matcher class declaration
└── matcher.cpp     Matcher::execute
```

Tests: `test_book.cpp` — 33 cases covering add, cancel, match, boundaries, invalid inputs, and invariant checking after every operation (ASAN + UBSAN clean).

Benchmarks: `bench_book.cpp` — RDTSC-timed, data-driven (1M OU-generated events), pinned to core 2.

---

## Performance Reference (warm cache, i9-10980HK)

| Operation | Cycles (i9-10980HK, core 2, `isolcpus=2`) |
|---|---|
| Add | 34 cy |
| Cancel (realistic 10:1 workload) | 22 cy |
| Match (data-driven) | 137 cy |
| Best-bid scan | 84 cy |

Full results, methodology, and C vs C++ head-to-head history: `benchmark-results.md`.
