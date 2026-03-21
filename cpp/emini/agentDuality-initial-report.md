# agentDuality Analysis — E-Mini S&P 500 Limit Order Book

---

## Analysis 0 — Cross-Cutting: Fixed-Point Price Representation

This is a prerequisite question, not a trade-off question. The agentContext report correctly identifies it as a structural decision that must precede any struct definition. This analysis confirms that recommendation with layout arithmetic.

**The float path:**

A `double` price field in each order node costs 8 bytes. The index computation `tick = (price - base) / 0.25` involves a floating-point subtraction and division on every insert, cancel, and lookup. The compiler cannot strength-reduce float division by a non-power-of-two-representable constant to a shift. On x86-64 this is a `divsd` instruction: ~20 cycles latency, not pipelined in the same way as integer operations.

**The integer tick path:**

`tick = (int)(price * 4)` at the API boundary. Inside the book, the tick integer is the index. No `price` field is needed on the order node at all — the node's position in the level array encodes the price. Integer comparison, integer arithmetic, bitwise operations throughout. The index computation on the hot path is zero cost because it already happened at the boundary.

**Layout consequence:**

With a float `price` field removed from each order node, the node shrinks. A node containing `{order_id, quantity, next_ptr}` using `{uint32_t, uint32_t, uint32_t}` = 12 bytes, or with a next-index instead of pointer, potentially fits two or more nodes per cache line. Every byte removed from the inner node multiplies by the number of live orders (bounded by ~100,000 for ES) — at 100,000 orders, removing 8 bytes saves 800 KB of working set pressure.

**Verdict:** Confirmed correct. The integer tick representation eliminates a field from every order node, eliminates floating-point arithmetic from the critical path, eliminates comparison ambiguity, and enables the direct array index that is the foundation of the O(1) architecture. This decision must be locked before any struct is defined.

---

## Analysis 1 — Outer Price-Level Structure: Flat Array vs Hash Map vs BST

### Current (Default Naive)
- Data structure: `std::map<double, Limit*>` (BST) or `std::unordered_map<uint64_t, Limit*>`
- Operations: insert O(log M), lookup O(log M) or O(1) avg, delete O(log M) or O(1) avg
- Layout: Pointer-chased — each Limit node is a separate heap allocation; tree nodes non-contiguous
- Working set: For M=200 levels: ~9.6–12.8 KB metadata, scattered across the heap

### Proposed
- Data structure: `pricePoint_t pricePoints[MAX_TICKS]` — statically allocated flat array indexed by integer tick
- Operations: insert O(1) (direct index), lookup O(1), delete O(1)
- Layout: Contiguous — the entire array is one allocation; adjacent ticks are adjacent in memory
- Working set: ±7% range (3080 ticks) at 16 bytes = 49 KB; ±20% hard limit (8800 ticks) = 140 KB; ±2000-tick sliding window = 32 KB

### Trade
- **Time cost**: None on the hot path. Sliding-window variant adds window-shift logic on large price moves — a cold-path event.
- **Time gain**: Insert, lookup, and delete collapse to pure O(1) — single array index computation. No hash, no collision, no pointer dereference to the level node.
- **Space cost**: Array allocates memory for all ticks in range whether or not resting orders exist. At 16 bytes × 8800 slots = 140 KB total.
- **Space gain**: Zero per-level heap allocation overhead. Map's ~48-byte node overhead per active level eliminated.
- **Cache impact**: Significant improvement. Active price levels near best bid/ask are spatially local — consecutive slots in the array. The prefetcher can load them ahead. Map/hash-map levels are scattered; each access is a pointer dereference with potential cache miss.

### Envelope
The critical envelope dimension is the full tick range — fixed by CME contract specifications:
- Working range: 8800 ticks (±20% hard limit, bidirectional)
- Normal operating range: 3080 ticks (±7% overnight)
- Practical sliding window: configurable, e.g. ±2000 ticks = 4000 ticks

### Verdict
**Recommend** — the flat array indexed by integer tick.

The ES tick range is fixed by exchange rule. The array fits in L2 (140 KB for the full range) and L1 for a sliding window (32 KB at ±2000 ticks). The hash map's O(1) average includes constant overhead (hash computation, collision resolution, pointer dereference). The flat array's O(1) is an integer index plus base address — no hash, no collision, no pointer. The BST is strictly dominated: O(log M) at every operation with pointer-chasing on each traversal step.

The sliding-window variant is worth considering for L1 residency but introduces window-shift logic. Start with the full-range static array; add the sliding window only if L2 residency is confirmed as a bottleneck by measurement.

### Pitfalls Checked
- Pitfall 2 (O(log n) vs slow): O(log M) pointer-chased vs O(1) array index — both the constant and layout favour the array
- Pitfall 7 (hash maps for small N): hash map constant (hash + collision + pointer) is measurably worse than array even at small M
- Pitfall 13 (envelope unknown): tick range is established by CME contract specs — not estimated, fixed by exchange rule

---

## Analysis 2 — Inner Order Queue: Singly-Linked List (Arena) vs std::deque vs std::vector + Head Index

### Current (Default Naive)
- Data structure: `std::deque<Order>` or doubly-linked list of heap-allocated Order nodes
- Operations: push_back O(1), pop_front O(1), cancel-by-id O(q) scan
- Layout: Chunked (deque) or pointer-chased (linked list). Deque: minimum 512-byte chunk per level even for a single order.
- Working set: deque minimum 512 bytes × M=200 active levels = 100 KB in chunk metadata alone

### Proposed
- Data structure: Singly-linked intrusive list with nodes drawn from a pre-allocated arena (static array, free list for O(1) alloc/free)
- Operations: push_back (tail append) O(1), pop_front O(1), alloc/free O(1)
- Layout: Arena is one contiguous allocation. Nodes within arena's contiguous region — no heap fragmentation, no allocator calls on the hot path.
- Working set: 100,000 nodes × 16 bytes = 1.6 MB (L3)

### Trade
- **Time cost**: Arena requires upfront sizing. Free list adds one store on free and one load on alloc — negligible.
- **Time gain**: Eliminates `malloc`/`free` from the hot path entirely. No per-operation allocator interaction.
- **Space cost**: Full arena allocated at startup regardless of current occupancy.
- **Space gain**: Deque 512-byte chunk overhead per level eliminated. Heap fragmentation eliminated. Per-node allocator metadata (~16–32 bytes per node in typical allocator) eliminated.
- **Cache impact**: Neutral to slight improvement. No fragmentation-induced scatter — all nodes within arena's address range. Temporal locality (recently freed nodes reused immediately) keeps active nodes warm.

### Envelope
- Total concurrent live orders: bounded by exchange position limits. QuantCup reference uses 1,010,000 slots. Estimate of 100,000 for ES has not been confirmed from workload data. **Must be confirmed before arena sizing.**
- q (orders per level): **unknown and unconfirmed.** Distribution not stated. Affects cancel-by-id scan performance (O(q)) but not arena sizing directly.

### Verdict
**Conditional** — arena + singly-linked intrusive list is the correct architecture, but arena size cannot be finalised without a confirmed concurrent order count envelope.

Condition: confirm peak concurrent live orders from target workload (replay data or stated per-session maximum). Size arena to that bound × 1.5 safety margin. If confirmed count yields arena under ~2 MB it fits in L3 with room for the price array alongside it.

### Pitfalls Checked
- Pitfall 14 (inner envelope must be measured): q per level is not stated — affects cancel O(q) performance, not arena sizing
- Pitfall 15 (container growth cost): arena + free list has no growth cost — statically allocated
- Pitfall 16 (bimodal pre-allocation): does not apply — free-list arena is a shared pool, not per-level pre-allocation
- Pitfall 10 (allocation cost vs algorithm cost): arena exists precisely to eliminate allocation cost from the hot path

---

## Analysis 3 — Cancel-by-ID: O(q) Scan vs O(1) Index

### Current (Default)
- Data structure: No order-ID index. Cancel requires scanning the order queue at the relevant price level.
- Operations: cancel O(q) — linear scan of q orders at the price level
- Layout: Dependent on inner container choice

### Proposed
- Data structure: Flat array or hash map `order_id → (tick_index, position_in_queue)` maintained alongside the order arena
- Operations: cancel O(1) — look up order's location, splice from list or mark deleted
- Layout: Flat array `uint32_t location[MAX_ORDER_ID]` if IDs are dense integers; open-addressing hash map if IDs are sparse/large

### Trade
- **Time cost**: Every insert must write to both price-level queue and order-ID index. One extra write per insert, one extra read per cancel.
- **Time gain**: Cancel drops from O(q) to O(1). For cancel-heavy workloads (most limit orders are cancelled before fill), this is the dominant gain.
- **Space cost**: Order-ID index adds memory proportional to live order count or ID space, depending on implementation.
- **Cache impact**: Index lookup adds one memory access. Savings from avoiding O(q) scan are larger than this cost in any realistic scenario.

### Envelope
- Order ID density: **unknown.** CME assigns order IDs per session. Whether dense sequential integers or sparse 64-bit values determines flat array vs hash map. **This must be established from the CME protocol spec before choosing the index structure.**
- If dense 32-bit integers with maximum N per session, and N × 4 bytes fits in budget (N=100,000 → 400 KB, in L3): use flat array.
- If 64-bit or sparse: use open-addressing hash map (not `std::unordered_map`).

### Verdict
**Conditional** — O(1) order-ID index is unambiguously correct. Conditional is on index structure (flat array vs hash map), which depends on order ID type.

### Pitfalls Checked
- Pitfall 3 (contiguous always wins): flat array only correct if IDs are dense; hash map correct for sparse IDs
- Pitfall 13 (envelope unknown): order ID type/density is a protocol property — must be established from CME spec

---

## Analysis 4 — Best Bid/Ask Tracking: Maintained Pointer vs Bitset Scan

### Current (Default)
- Data structure: Maintained `int` pointer (best_bid_tick, best_ask_tick) updated on every structural change
- Operations: get_best O(1), update after drain — O(scan_width) without bitset
- Layout: Two integers, trivially in registers

### Proposed
- Data structure: Companion bitset — one bit per tick. `uint64_t bitmap[138]` for 8800 ticks = 1.1 KB
- Operations: update bit O(1), find best bid via `__builtin_clzll` O(1) hardware, find best ask via `__builtin_ctzll` O(1) hardware
- Layout: 138 × 8 = 1.1 KB contiguous, L1 resident at all times

### Trade
- **Time cost**: Every insert and cancel must update the bitset (one bit set/clear) in addition to price array. Two writes per operation instead of one.
- **Time gain**: After a price level drains, finding the new best via `__builtin_ctzll`/`__builtin_clzll` is one instruction per 64 ticks, entirely within L1. Without bitset: scan of up to 140 KB of price array — L2/L3 accesses.
- **Space cost**: 1.1 KB for the bitmap. Negligible.
- **Cache impact**: Significant improvement for the drain case. Bitmap stays in L1. Worst-case scan is 138 × 8 = 1.1 KB sequential reads — prefetcher-friendly, entirely within L1.

### Envelope
The bitmap size is fixed by the tick range: 8800 ticks / 64 = 138 words = 1.1 KB. Established by CME contract specs. No further measurement needed.

The benefit magnitude depends on drain frequency. In a liquid market during active matching, draining the top of book is a hot-path event — every aggressive order consuming the top of book drains a level.

### Verdict
**Recommend** — bitset companion is the correct structure.

The 1.1 KB bitmap fits entirely in L1 and dissolves Contradiction 2 from the agentContext report (maintained pointer vs scan). `__builtin_ctzll` compiles to `TZCNT`/`BSF` — 1–3 cycles on modern x86-64, indistinguishable from O(1). Cost: one bit set/clear per insert/cancel — two instructions, dominated by the surrounding array access and list manipulation.

### Pitfalls Checked
- Pitfall 6 (cold path): drain event is hot-path during matching — bitmap is justified
- Pitfall 12 (branch prediction): bitset scan avoids data-dependent branching; `ctzll`/`clzll` is branchless
- Pitfall 13 (envelope): bitmap size fixed by contract specs

---

## Analysis 5 — AoS vs SoA for Order Nodes

### Current (Implicit AoS)
- Layout: AoS — all fields of one order adjacent. Candidate: `{order_id: uint32_t, quantity: uint32_t, next_idx: uint32_t}` = 12 bytes per node
- Working set: 100,000 nodes × 16 bytes = 1.6 MB (L3)

### Proposed (SoA alternative)
- Layout: SoA — separate arrays per field: `uint32_t order_ids[MAX]`, `uint32_t quantities[MAX]`, `uint32_t next_indices[MAX]`
- Working set: Same total bytes, different access pattern

### Trade
- **Time cost**: SoA requires three non-adjacent loads when processing one order (reading order_id + quantity + next_idx together). For full-node operations (matching, cancel), SoA hurts locality.
- **Time gain**: SoA wins only for single-field scans across all orders (e.g. aggregate volume scan). Not the dominant LOB operation.
- **Cache impact**: AoS keeps all fields of one order in the same cache line. SoA would require three separate cache-line loads per node. At 12 bytes per node, AoS gives ~5.3 nodes per 64-byte cache line.

### Verdict
**Retain** — AoS is the correct layout. Do not introduce SoA.

The dominant matching-engine operation is sequential full-node traversal of a price level's order queue (read order_id, quantity, next_idx per node). AoS is correct for this access pattern. SoA would only benefit single-field scans, which are not the primary operation. The fixed-point representation (no price field) already minimises node size to 12 bytes — good packing density without SoA complexity.

Use `static_assert(sizeof(order_node_t) == 12)` to enforce the layout. Consider whether to pad to 16 bytes for alignment simplicity vs retaining 12-byte density.

### Pitfalls Checked
- Pitfall 8 (AoS vs SoA): AoS correct for full-node sequential traversal; SoA only beneficial for single-field scans across all orders
- Pitfall 4 (over-aligning): at 12 bytes, natural `uint32_t` alignment (4 bytes) is correct; do not force 16-byte alignment without profiling evidence

---

## Summary Table for the Architect

| Decision | Verdict | Key Condition |
|---|---|---|
| Price representation (float vs integer tick) | **Adopt integer tick** — precondition for everything else | Fixed by CME contract specs; no measurement needed |
| Outer price-level structure (array vs map/BST) | **Recommend flat array indexed by tick** | Tick range fixed by CME spec; fits L2 unconditionally |
| Sliding window vs full-range array | **Conditional** | Measure if L2 latency is a confirmed bottleneck; start with full array |
| Inner order container (arena + list vs deque/heap) | **Conditional** — arena is correct; size TBD | Confirm peak concurrent order count from workload or exchange rules |
| Cancel-by-ID index (flat array vs hash map) | **Conditional** | Establish CME order ID type and density first |
| Best bid/ask tracking (bitmap vs maintained pointer) | **Recommend bitmap companion** | Tick range known; 1.1 KB fits L1; drain is a hot-path event |
| Order node layout (AoS vs SoA) | **Retain AoS** | Dominant operation is full-node sequential traversal |

## Single Highest-Priority Action Before Architecture

Establish the CME order ID format (type, density, session scope). This resolves the conditional on Analysis 3 (cancel-by-ID index structure) — the one structural decision that cannot be resolved from contract specifications alone. Everything else is either resolved by the tick-range envelope (from CME specs) or is a conditional on the workload's peak concurrent order count (which can be conservatively bounded and adjusted later).
