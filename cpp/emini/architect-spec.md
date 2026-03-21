# Architecture Specification — E-mini S&P 500 Limit Order Book

**Produced by**: agentArchitect
**Inputs**: architect-requirements.md, agentContext-initial-report.md, agentContext-triz-pass.md, agentDuality-initial-report.md, agentDuality-arena-reuse.md
**Date**: 2026-03-21
**Target implementations**: C (agentC), C++ (agentCPP)

---

## Decision Register

| Decision | Status | Resolution / Condition |
|---|---|---|
| Price representation | LOCKED | uint32_t integer tick. `tick = (uint32_t)((price - base) * 4.0 + 0.5)`. One cast at API boundary. No float inside the book. Source: agentDuality A0 |
| Primary price-level structure | LOCKED | Flat array indexed by integer tick, one array per side. Source: agentDuality A1 |
| Bitmap companion | LOCKED | uint64_t bitmap, one per side, synchronous updates only — never lazy. PC-2 from TRIZ pass mandates synchronous updates. Source: agentDuality A4 |
| Arena allocation policy | LOCKED | No-reuse, monotonic. slot_index == order_id by construction. Sized to session total (1,000,000 nodes). Source: agentDuality arena-reuse verdict |
| Node size ceiling | LOCKED | 16 bytes maximum. Keeps 1M-order arena at 16 MB, L3-resident on all target hardware. Source: agentDuality arena-reuse Q1 |
| Order node layout | LOCKED | AoS. Fields: order_id (uint32_t), quantity (uint32_t), next_idx (uint32_t), flags (uint8_t) + pad (uint8_t × 3). 16 bytes. Source: agentDuality A5 |
| Per-level queue discipline | LOCKED | Intrusive singly-linked FIFO as the starting implementation. Source: architect-requirements.md locked decisions |
| Cancel index | LOCKED — dissolved | No cancel index structure. arena[order_id] is the direct dereference. Source: agentDuality arena-reuse implication for A3 |
| Free list | LOCKED — dissolved | No free list. alloc = next_slot++. free = dead-mark write only. Source: agentDuality arena-reuse |
| Cancel latency | LOCKED | First-class requirement. Must be analysed and reported at the same tier as add and match. Source: architect-requirements.md |
| Sliding window vs full array | RESOLVED here | Full array, both sides. One priceLevel array of MAX_TICKS entries per side. Crossover condition: if measured L2 miss rate on price-array access exceeds 5% of total operations, implement the sliding-window variant. Rationale: simpler indexing arithmetic; no rebasing on hot path; 140 KB per side fits L2 on all server CPU targets with L2 >= 256 KB (single-side only; both sides together are 281.6 KB — accept this as borderline L2 or L3-near-L2 on 256 KB targets) |
| Doubly-linked promote condition | RESOLVED here | Promote to doubly-linked (add prev_idx field) if and only if: the cancel benchmark at queue depth q=10 measures median cancel latency above 50 cycles on the target hardware, AND the arena node size after adding prev_idx (uint32_t, +4 bytes) remains at or below 16 bytes. Because the current node is exactly 16 bytes, adding prev_idx would grow the node to 20 bytes and break the node-size ceiling. Therefore: promote to doubly-linked only if the benchmark result at q=10 exceeds 50 cycles median AND a redesign that retains 16-byte nodes is possible (e.g. dropping the explicit order_id field from the node, recovering it from slot_index directly). This is a conditional redesign, not a field addition. The implementation agents must not make this decision unilaterally — they must report the benchmark results to the user for evaluation |
| Matcher component | RESOLVED here | The matcher is an explicit named module (see Module Boundaries and Interface Specification). It holds read access to both sides. It is the only code that reads both sides simultaneously. It is not implicit in add() |
| Order node AoS vs SoA | LOCKED | AoS. Source: agentDuality A5 |

---

## Data Model

### Fundamental Constants

```
MAX_TICKS        = 8800        /* full ±20% CME hard limit, bidirectional */
BITMAP_WORDS     = 138         /* ceil(8800 / 64) */
MAX_ORDERS       = 1000000     /* session upper bound, QuantCup canonical */
NULL_IDX         = 0xFFFFFFFF  /* sentinel for end-of-list and empty level */
DEAD_FLAG        = 0x01        /* order_node_t.flags bit indicating cancelled/filled node */
BASE_TICK        = 0           /* logical base; actual base price is a runtime parameter */
```

These are compile-time constants. In C they are `#define`. In C++ they are `static constexpr uint32_t` members of the Book class or a dedicated constants header.

### Primary Types

| Type name | Representation | Size | Rationale |
|---|---|---|---|
| tick_t | uint32_t | 4 B | Integer price index. Direct array index. No float anywhere inside the book |
| qty_t | uint32_t | 4 B | Whole contracts. No fractional lots on ES |
| order_id_t | uint32_t | 4 B | Dense session integer. Equals arena slot index by construction |
| slot_idx_t | uint32_t | 4 B | Arena slot index. Identical value to order_id_t — distinguished by typedef for clarity |
| side_t | uint8_t enum | 1 B | BID = 0, ASK = 1. Used as index into two-element per-side arrays |
| flags_t | uint8_t | 1 B | Bit 0 = DEAD_FLAG. Remaining bits reserved, must be zero |

In C: implemented as `typedef uint32_t tick_t;` etc.
In C++: implemented as `enum class side_t : uint8_t { BID = 0, ASK = 1 };` and `using tick_t = uint32_t;` etc.

### Struct Layouts

#### order_node_t — the arena element

```
struct order_node_t {
    uint32_t order_id;    /* offset 0,  size 4 — equals slot index by construction */
    uint32_t quantity;    /* offset 4,  size 4 — remaining quantity (decremented on partial fill) */
    uint32_t next_idx;    /* offset 8,  size 4 — next node in FIFO, NULL_IDX if tail */
    uint8_t  flags;       /* offset 12, size 1 — DEAD_FLAG when cancelled or fully filled */
    uint8_t  _pad[3];     /* offset 13, size 3 — explicit pad to 16 bytes */
};                        /* total: 16 bytes */
```

`static_assert(sizeof(order_node_t) == 16)` — mandatory in both C (as a compile-time check) and C++.

Note on order_id field: the order_id is redundant (it equals the slot index) but is retained for two reasons: (1) verification in cancel — confirm `arena[order_id].order_id == order_id` before acting; (2) protocol output — fill notifications include the order_id. It is not a hot-path field for index computation.

If the promote-to-doubly-linked condition is triggered (see Decision Register), the redesign must recover 4 bytes. The viable option is to remove the order_id field and recover the order_id from the slot index implicitly. The verification check becomes `slot == order_id` by construction (always true unless the arena is corrupt). The protocol output recovers order_id from `slot_index`. This is the only viable path to doubly-linked within the 16-byte ceiling.

#### price_level_t — one entry per tick per side

```
struct price_level_t {
    uint32_t head_idx;    /* offset 0,  size 4 — head of FIFO queue, NULL_IDX if empty */
    uint32_t tail_idx;    /* offset 4,  size 4 — tail of FIFO queue, NULL_IDX if empty */
    uint32_t count;       /* offset 8,  size 4 — live order count at this level */
    uint32_t total_qty;   /* offset 12, size 4 — aggregate quantity at this level */
};                        /* total: 16 bytes */
```

`static_assert(sizeof(price_level_t) == 16)` — mandatory in both languages.

An empty level has `head_idx == NULL_IDX`, `tail_idx == NULL_IDX`, `count == 0`, `total_qty == 0`. These must be jointly consistent at all times — no partially-initialised empty level.

#### book_side_t — one complete side (bid or ask)

```
struct book_side_t {
    price_level_t levels[MAX_TICKS];   /* 8800 × 16 = 140,800 bytes */
    uint64_t      bitmap[BITMAP_WORDS]; /* 138  ×  8 =   1,104 bytes */
    /* total per side: 141,904 bytes (~138.6 KB) */
};
```

`static_assert(sizeof(book_side_t) == 141904)` — mandatory.

Both sides together: 283,808 bytes (~277 KB). This crosses the L2 boundary on CPUs with 256 KB L2. The bitmap (1,104 bytes each side, 2,208 bytes total) is L1-resident on all plausible hardware.

Layout note: levels[] is declared before bitmap[] so that the levels array, being the larger and more frequently accessed structure, occupies the lower address range. On most hardware this is irrelevant to cache behaviour, but it avoids artificial aliasing of the bitmap into the same cache lines as levels.

#### arena_t — session-wide order node pool

```
struct arena_t {
    order_node_t nodes[MAX_ORDERS];   /* 1,000,000 × 16 = 16,000,000 bytes (16 MB) */
    uint32_t     next_slot;           /* allocation high-water mark */
};
```

`static_assert(sizeof(arena_t) == 16000004)` — note: sizeof includes next_slot and compiler padding. The implementation must verify the actual size at compile time; the static_assert value here is approximate. Correct value depends on alignment of next_slot after the array. Implementors must compute and assert the correct value.

The arena fits entirely in L3 on all modern server CPUs (Intel Xeon Gold/Platinum and AMD EPYC have 16–64 MB L3). Cold nodes (slots allocated early in the session, long since cancelled) will have been evicted; accessing them incurs an L3 or RAM miss. This is acceptable for cold cancels.

#### book_t — the complete order book

```
struct book_t {
    book_side_t sides[2];   /* sides[BID] and sides[ASK] */
    arena_t     arena;
    double      base_price; /* reference price for tick conversion; set at init */
};
```

The complete book in memory: 2 × 141,904 + 16,000,004 = 16,283,812 bytes (~15.5 MB).

In C: `book_t` is a flat struct. The book is created on the heap via `book_create()` (see Interface Specification) — it is too large for the stack.

In C++: `book_t` is a class wrapping the same layout. The sides and arena are data members. `base_price` is a `double` member. The constructor enforces invariants. `static_assert` on all struct sizes is in the class body.

#### fill_t — result of a match operation

```
struct fill_t {
    order_id_t maker_order_id;  /* 4 B — the resting order that was matched */
    order_id_t taker_order_id;  /* 4 B — the aggressive order that caused the match */
    tick_t     price_tick;      /* 4 B — price at which the fill occurred */
    qty_t      filled_qty;      /* 4 B — quantity exchanged */
};                              /* 16 bytes */
```

`static_assert(sizeof(fill_t) == 16)` — mandatory.

#### fill_result_t — result of processing one incoming order

```
struct fill_result_t {
    fill_t   fills[64];     /* maximum fills per order (bounded by caller's contract) */
    uint32_t fill_count;    /* number of valid entries in fills[] */
    qty_t    remaining_qty; /* quantity not yet matched; 0 if fully filled */
};
```

`fill_count` is bounded at 64 fills per order for simplicity. A single aggressive order can in theory match across many levels; this bound is a stated implementation constraint. If an incoming order would generate more than 64 fills, the implementation stops matching at 64, sets `remaining_qty` to what was unmatched, and returns — it does not rest the remainder (the caller must decide whether to resubmit or discard). This bound must be documented in the public API.

In C++: `fill_result_t` may use a `std::array<fill_t, 64>` but the struct layout must be ABI-compatible with the C version for benchmark comparison. Prefer the same struct definition in both.

### Ownership Model

The `book_t` struct owns all memory for both sides and the arena. There are no heap allocations after `book_create()` / `Book::Book()`. All `order_node_t` objects are owned by `arena`. The `price_level_t` arrays are owned by `book_side_t`. Callers never hold pointers into the book's internal structures — they hold only `order_id_t` values (which are slot indices).

In C: `book_create()` returns a heap-allocated `book_t*`. The caller owns the pointer and must call `book_destroy()`. All internal pointers are indices into `arena.nodes[]` — no interior pointer is ever exposed.

In C++: `Book` is a class with RAII semantics. The destructor releases the heap-allocated book. Move semantics are defined; copy is deleted.

---

## API Boundary

The API boundary is the surface at which external types (floating-point prices, unvalidated IDs) are converted to internal types. The boundary consists of the public functions in the interface specification below. All validation and conversion occurs at the first line of each public function, before any internal state is read or written.

### Price conversion (external double → internal tick_t)

```
tick_t price_to_tick(double price, double base_price)
```

Conversion formula: `tick = (uint32_t)((price - base_price) * 4.0 + 0.5)`

This is the one sanctioned cast in the entire system. It appears in `price_to_tick()` only. No other function performs a float-to-integer cast.

Preconditions checked at boundary:
- `price >= base_price` (otherwise tick computation underflows)
- `(price - base_price) * 4.0 < (double)MAX_TICKS` (otherwise tick overflows array bounds)
- `price` is a finite double (no NaN, no inf — checked with `isfinite()`)
- The result tick satisfies `tick < MAX_TICKS`

If any precondition fails, `price_to_tick()` returns a sentinel value `TICK_INVALID = UINT32_MAX`. Every caller must check for `TICK_INVALID` before proceeding.

In C: `price_to_tick()` is a static inline function in the header. The cast is the only non-`uint32_t` expression visible in the function body.

In C++: `price_to_tick()` is a free function in the `es::book` namespace, marked `[[nodiscard]]`. The return type is `tick_t`. The compiler enforces no further implicit conversions because all internal arithmetic uses `tick_t` consistently.

### Order ID origin

Order IDs are assigned by the book on insert. Callers do not supply order IDs. `book_add()` returns the `order_id_t` for the newly placed order. The caller stores this value and supplies it to `book_cancel()` later. No external order ID mapping is required.

This design avoids the order ID density question (sparse/dense CME IDs) by making the session-internal ID the canonical identifier. If the caller also tracks a CME-native order ID, that mapping is the caller's responsibility and is outside the book's scope.

### Enforcement of no-cast rule

In C: the file is compiled with `-Wconversion -Werror`. Any implicit narrowing or promotion in any hot-path function will fail to compile. The only sanctioned cast is in `price_to_tick()`.

In C++: the type system enforces the rule. All internal functions accept `tick_t`, `qty_t`, `order_id_t`, `slot_idx_t` — distinct typedefs. In the C++ implementation, `enum class side_t` prevents int/side_t confusion. `[[nodiscard]]` on all functions returning `order_id_t` or `tick_t` prevents silent discard of a returned ID.

---

## Interface Specification

All functions operate on a `book_t*` (C) or `Book&` (C++). The C++ version wraps each C-style function as a method or free function in namespace `es::book`. The underlying operations are identical; where the languages differ, the difference is noted explicitly.

### book_create / Book::Book

**C**: `book_t* book_create(double base_price)`
**C++**: `Book::Book(double base_price)`

Precondition: `isfinite(base_price)` is true. `base_price > 0.0`.
Postcondition: returns a fully initialised book. All `price_level_t` entries have `head_idx == NULL_IDX`, `tail_idx == NULL_IDX`, `count == 0`, `total_qty == 0`. All bitmap words are 0. `arena.next_slot == 0`. `book.base_price == base_price`.
Error (C): returns NULL if heap allocation fails. Caller must check.
Error (C++): throws `std::bad_alloc` if allocation fails (standard constructor semantics).
Note: the book is allocated on the heap because its size (~15.5 MB) exceeds typical stack limits.

### book_destroy (C only)

**C**: `void book_destroy(book_t* book)`

Precondition: `book != NULL`.
Postcondition: the heap memory for `*book` is freed. `book` is a dangling pointer after return.
Error: if `book == NULL`, no-op (matches `free(NULL)` semantics).
Note: C++ version uses RAII destructor; no explicit destroy function.

### book_add

**C**: `order_id_t book_add(book_t* book, side_t side, double price, qty_t quantity)`
**C++**: `order_id_t Book::add(side_t side, double price, qty_t quantity)`

Precondition:
- `book != NULL` (C) / object is valid (C++)
- `side` is `BID` or `ASK`
- `price` is a valid ES price: finite, `>= base_price`, maps to `tick < MAX_TICKS`
- `quantity > 0`
- `arena.next_slot < MAX_ORDERS` (session capacity not exhausted)

Postcondition:
- A new `order_node_t` is allocated: `slot = arena.next_slot++`
- The node is initialised: `order_id = slot`, `quantity = quantity`, `next_idx = NULL_IDX`, `flags = 0`
- The node is appended to the FIFO tail of `sides[side].levels[tick]`
- `price_level_t.count` is incremented by 1; `total_qty` is incremented by `quantity`
- If the level was previously empty (`count` was 0 before this add): bitmap bit `tick` is set in `sides[side].bitmap`
- Returns `slot` as the `order_id_t` for this order

Error:
- Invalid `price` (fails `price_to_tick` preconditions): returns `NULL_IDX` (`0xFFFFFFFF`), no mutation
- `quantity == 0`: returns `NULL_IDX`, no mutation
- `side` is neither `BID` nor `ASK`: returns `NULL_IDX`, no mutation
- Arena exhausted (`next_slot >= MAX_ORDERS`): returns `NULL_IDX`, no mutation

The returned `order_id_t` of `NULL_IDX` is the error sentinel. Callers must check.

Important: `book_add` does NOT check for crossing prices. It places the order into the book as a resting order unconditionally. Crossing detection is the caller's responsibility — call `book_match` before `book_add` if the incoming order may be aggressive. This design separates the placement and matching concerns cleanly (see Module Boundaries).

### book_cancel

**C**: `bool book_cancel(book_t* book, order_id_t order_id, side_t side, tick_t tick)`
**C++**: `bool Book::cancel(order_id_t order_id, side_t side, tick_t tick)`

The caller must supply `side` and `tick` along with `order_id`. This is not a lookup operation — the caller already knows which side and price level the order rests at (it recorded them when `book_add` returned). This is a deliberate design decision: it avoids any secondary lookup and makes the cancel path consist of exactly two memory accesses (arena node + price_level FIFO manipulation).

Precondition:
- `book != NULL` (C) / object is valid (C++)
- `order_id < arena.next_slot` (the ID was ever issued)
- `side` is `BID` or `ASK`
- `tick < MAX_TICKS`

Postcondition:
- If `arena.nodes[order_id].flags & DEAD_FLAG`: order is already dead; returns false, no mutation
- Otherwise:
  - The node is unlinked from the FIFO at `sides[side].levels[tick]`
  - `price_level_t.count` is decremented by 1; `total_qty` is decremented by the node's `quantity`
  - If `count` reaches 0: bitmap bit `tick` is cleared in `sides[side].bitmap` — synchronous, before function returns
  - `arena.nodes[order_id].flags |= DEAD_FLAG`
  - Returns true

Error:
- `order_id >= MAX_ORDERS` or `order_id >= arena.next_slot`: returns false, no mutation
- `tick >= MAX_TICKS`: returns false, no mutation
- `side` invalid: returns false, no mutation

Implementation note on singly-linked cancel: `book_cancel` must scan the FIFO at `levels[tick]` from head to find the predecessor of `order_id`'s node before unlinking. This is O(q) where q is the queue depth. The function iterates `next_idx` links starting at `head_idx` until it finds a node whose `next_idx == order_id`. This is the known cost of singly-linked cancel. The benchmark contract below requires measuring this cost at q = 1, 5, 10, 50.

Special case — cancelling the head node: if `arena.nodes[order_id]` is the head of the queue (`head_idx == order_id`), no predecessor scan is needed. The head is updated to `next_idx` of the cancelled node. This is O(1). The benchmark must exercise both head-cancel and mid-queue cancel separately.

### book_match

**C**: `fill_result_t book_match(book_t* book, side_t aggressor_side, double price, qty_t quantity, order_id_t taker_id)`
**C++**: `fill_result_t Book::match(side_t aggressor_side, double price, qty_t quantity, order_id_t taker_id)`

`aggressor_side` is the side of the incoming order. To buy aggressively (bid crosses ask), `aggressor_side = BID`. The matcher looks at the opposite side (`ASK`) for resting orders to match against.

Precondition:
- `book != NULL` (C) / object is valid (C++)
- `aggressor_side` is `BID` or `ASK`
- `price` is a valid ES price mapping to `tick < MAX_TICKS`
- `quantity > 0`
- `taker_id` is a valid order_id issued by the caller (used in fill records only; not verified inside the book)

Postcondition:
- Let `maker_side = 1 - aggressor_side` (the opposite side)
- Let `aggressor_tick = price_to_tick(price, base_price)`
- The matcher iterates the maker side from its best price toward the aggressor price:
  - For BID aggressor: iterates ask side from best_ask upward, while `best_ask_tick <= aggressor_tick` and `remaining_qty > 0`
  - For ASK aggressor: iterates bid side from best_bid downward, while `best_bid_tick >= aggressor_tick` and `remaining_qty > 0`
- For each matching level tick `t`:
  - Consumes orders from `maker_side.levels[t]` head-first (FIFO)
  - For each consumed maker order: records a `fill_t` (maker_order_id, taker_id, t, min(maker_qty, remaining_qty))
  - If the maker order is partially filled: `order_node_t.quantity` is decremented; the node remains at the head
  - If the maker order is fully filled: the node is dequeued from the head, `flags |= DEAD_FLAG`, `count--`, `total_qty -= filled_qty`
  - If a level empties: bitmap bit `t` is cleared synchronously
- Stops when `remaining_qty == 0` or no more crossing prices exist or `fill_count == 64`
- `fill_result_t.remaining_qty` = quantity not matched
- `fill_result_t.fill_count` = number of fills recorded

Error:
- Invalid price: `fill_result_t.fill_count = 0`, `remaining_qty = quantity`, no mutation
- No crossing orders: `fill_result_t.fill_count = 0`, `remaining_qty = quantity`, no mutation

### book_best_bid / book_best_ask

**C**: `tick_t book_best_bid(const book_t* book)` / `tick_t book_best_ask(const book_t* book)`
**C++**: `tick_t Book::best_bid() const` / `tick_t Book::best_ask() const`

Precondition: `book != NULL` (C) / object is valid (C++).
Postcondition: returns the tick index of the best (highest) bid, or `NULL_IDX` if the bid side is empty. Similarly for ask (lowest ask).

Implementation: scans the bid bitmap from word `BITMAP_WORDS-1` downward using `__builtin_clzll` to find the highest set bit; scans the ask bitmap from word 0 upward using `__builtin_ctzll` to find the lowest set bit.

Performance: O(BITMAP_WORDS) = O(138) = effectively O(1). The bitmap (1,104 bytes per side) is L1-resident. This is one to a few cycles per 64-tick word. Degenerate worst case (empty book): 138 words scanned, approximately 138 hardware bit-count instructions.

### book_level_count / book_level_qty

**C**: `uint32_t book_level_count(const book_t* book, side_t side, tick_t tick)`
**C++**: `uint32_t Book::level_count(side_t side, tick_t tick) const`

**C**: `qty_t book_level_qty(const book_t* book, side_t side, tick_t tick)`
**C++**: `qty_t Book::level_qty(side_t side, tick_t tick) const`

Precondition: `book != NULL`, `side` valid, `tick < MAX_TICKS`.
Postcondition: returns `levels[tick].count` (or `total_qty`) without modifying state.
Error: `tick >= MAX_TICKS` or invalid side: returns 0.

These functions exist for testing and inspection. They are not on the hot path.

### book_reset (C) / Book::reset() (C++)

**C**: `void book_reset(book_t* book)`
**C++**: `void Book::reset()`

Precondition: `book != NULL`.
Postcondition: all price levels cleared, all bitmap words zeroed, `arena.next_slot = 0`. `base_price` is retained.
Note: this is a session boundary operation — not on the hot path. May use `memset`.

---

## Performance Contract

| Operation | Complexity | Cache tier (hot path data) | Notes |
|---|---|---|---|
| book_add | O(1) | Bitmap: L1. price_level_t: L2/L3. arena slot: L3 | Three writes: arena node, level tail pointer, bitmap bit. All O(1) arithmetic |
| book_cancel (head) | O(1) | arena node: L3 (warm if recent cancel). price_level_t: L2/L3 | Head cancel: no scan. Two writes: level head pointer, DEAD_FLAG |
| book_cancel (mid-queue, depth q) | O(q) | Up to q arena nodes traversed | Worst case: q predecessor nodes, each 16 bytes, ~5.3 nodes per cache line. At q=10: ~2 cache lines |
| book_match (single level) | O(1) | Bitmap: L1. price_level_t: L2/L3. arena node(s): L3 | Drain one level: loop over head-dequeue until filled or level empty |
| book_match (k levels crossed) | O(k × q_mean) | As above, k levels | k bounded by the price move; q_mean is the empirical mean queue depth |
| book_best_bid / book_best_ask | O(BITMAP_WORDS) = O(138) | Bitmap: L1 | Hardware TZCNT/LZCNT; at most 138 words; degenerate case only when book is empty |
| book_level_count / book_level_qty | O(1) | price_level_t: L2/L3 | Single field read |
| book_reset | O(MAX_TICKS + BITMAP_WORDS + MAX_ORDERS) | Bulk memset; not cache-sensitive | Cold path: session boundary only |
| price_to_tick | O(1) | Register | One multiply, one add, one cast. Called once per public operation at boundary |

**Cache tier key**:
- L1: under 32 KB, always warm in normal operation (bitmap: 1.1 KB per side)
- L2/L3: 140 KB per side for price-level array; warm for levels near best bid/ask due to spatial locality
- L3: arena (16 MB); warm for recent orders, cold for early-session orders

**Cancel latency target**: under 20 cycles median for head cancel (single L3 access at warm arena slot). Mid-queue cancel at q=10: target under 50 cycles median. These are the benchmark thresholds that trigger the doubly-linked promote decision.

---

## Module Boundaries

Each module hides exactly one design decision.

### Module 1: Arena (arena.h / Arena.hpp)

**Hides**: the allocation policy — specifically, that order nodes are never reclaimed within a session and that slot_index equals order_id by construction.

Public interface: `arena_alloc(arena, order_id, quantity)` → `order_node_t*`; `arena_get(arena, order_id)` → `order_node_t*`; `arena_reset(arena)`.

The caller knows that `order_id_t` values are valid until `arena_reset`. The caller does not know that there is no free list. Changing to a reuse policy with a free list would change only this module.

### Module 2: Level Queue (queue.h / Queue.hpp)

**Hides**: the choice of singly-linked vs doubly-linked FIFO within a price level.

Public interface: `queue_enqueue(level, node_idx)`; `queue_dequeue_head(level, arena)` → `order_node_t*`; `queue_remove(level, arena, order_id)` → `bool`; `queue_is_empty(level)` → `bool`.

The caller knows that the queue is a FIFO with time-priority. The caller does not know whether the list is singly or doubly linked, or how the predecessor scan is performed. Changing from singly to doubly linked changes only this module.

`queue_remove` is the function that performs the predecessor scan for mid-queue cancel. It is the only function that needs to change if prev_idx is added.

### Module 3: Bitmap (bitmap.h / Bitmap.hpp)

**Hides**: the representation of the active-level indicator — specifically that it is a uint64_t word array and that `__builtin_ctzll` / `__builtin_clzll` are used for best-level scan.

Public interface: `bitmap_set(bitmap, tick)`; `bitmap_clear(bitmap, tick)`; `bitmap_best_bid(bitmap)` → `tick_t`; `bitmap_best_ask(bitmap)` → `tick_t`; `bitmap_is_set(bitmap, tick)` → `bool`.

The caller knows the bitmap answers "which levels are active." The caller does not know the internal word size or the bit-scan intrinsic used. Changing to a SIMD bitmap or a different intrinsic changes only this module.

All bitmap updates are synchronous — the caller (level queue module) must call `bitmap_clear` immediately when a level empties, before the function returns. The bitmap module does not enforce this; it is a precondition of the module boundary contract.

### Module 4: Matcher (matcher.h / Matcher.hpp)

**Hides**: the cross-side matching algorithm — specifically the loop structure that consumes the maker side until the taker quantity is satisfied or no crossing price remains.

Public interface: `matcher_execute(book, aggressor_side, tick, quantity, taker_id)` → `fill_result_t`.

The matcher is the only code in the system with simultaneous read/write access to both sides. All other modules operate on a single side. Changing the matching algorithm (e.g. pro-rata fill, or maximum-fill-first) changes only this module.

The matcher calls `bitmap_best_ask` / `bitmap_best_bid` and `queue_dequeue_head` — it is above both the bitmap and queue modules in the dependency graph. It does not call `book_cancel` or `book_add`.

### Module 5: Book (book.h / Book.hpp)

**Hides**: the composition of the above four modules into a single stateful object and the routing of public API calls to the appropriate module.

Public interface: the full set of `book_*` functions (C) or `Book::` methods (C++).

The book module assembles Arena, BookSide (containing LevelQueue × MAX_TICKS and Bitmap), and Matcher into one object. The caller knows nothing about the internal module decomposition.

### C vs C++ module differences

In C: modules are implemented as `.c` files with accompanying `.h` headers. Module state is passed as explicit pointer parameters. There is no object encapsulation — the struct definitions are visible in the headers. The no-cast rule is enforced by `-Wconversion -Werror` at the compilation unit level.

In C++: modules are implemented as classes or namespaces in `.cpp` / `.hpp` files. State is encapsulated in class members. The no-cast rule is enforced by the type system: `enum class side_t` prevents int/side comparisons; `[[nodiscard]]` prevents silent ID discard; `static_assert` on struct sizes prevents layout drift; `std::span` or bounded array types prevent out-of-bounds access in non-hot-path code.

The struct layouts (order_node_t, price_level_t, book_side_t, arena_t, fill_t) must be byte-for-byte identical between C and C++ versions. This is required for the benchmark comparison to be valid — the same data layout must be used in both languages so that cache behaviour is controlled.

---

## Test Suite Contract

### Invariants the checker must verify after every operation

These invariants must hold after every call to `book_add`, `book_cancel`, and `book_match`. The test harness must check all of them, not just the return value.

1. **Bitmap-level consistency**: for every tick t on every side, `bitmap_is_set(t)` must equal `(levels[t].count > 0)`. If the bitmap and level count disagree, it is a correctness bug — the bitmap was updated lazily or not at all.

2. **Level count consistency**: `levels[t].count` must equal the number of live (non-DEAD_FLAG) nodes reachable by following `next_idx` from `head_idx`. These must agree exactly.

3. **Level qty consistency**: `levels[t].total_qty` must equal the sum of `quantity` fields of all live nodes at tick t.

4. **FIFO order**: within any price level, nodes reachable from `head_idx` by following `next_idx` must have monotonically increasing `order_id` values (because IDs are assigned in arrival order and no reordering occurs).

5. **Null sentinel at tail**: the last node in any FIFO chain must have `next_idx == NULL_IDX`. No valid slot index may equal `NULL_IDX`.

6. **DEAD_FLAG nodes unreachable**: no live node (reachable via `head_idx` chain) may have `DEAD_FLAG` set. Dead nodes must not appear in any FIFO chain.

7. **Arena high-water mark**: `arena.next_slot` must equal the total number of `book_add` calls that returned non-NULL_IDX since the last reset. It never decreases.

8. **Tail pointer validity**: if `count > 0`, `levels[t].tail_idx` must be the last node in the FIFO chain (the node with `next_idx == NULL_IDX`).

9. **Head pointer validity**: if `count > 0`, `levels[t].head_idx` must point to the node with the lowest `order_id` among live nodes at that level. If `count == 0`, `head_idx == NULL_IDX` and `tail_idx == NULL_IDX`.

### Test case catalogue

**Category A — Add only**

A1. Add one bid order. Verify: level count = 1, total_qty correct, bitmap bit set, returned order_id is 0.
A2. Add two bid orders at the same price. Verify: count = 2, FIFO order (order_id 0 before order_id 1), both invariants hold.
A3. Add bid and ask at the same price. Verify: each side independent; no crossing check in add.
A4. Add order at tick 0 (lowest valid tick). Verify boundary.
A5. Add order at tick MAX_TICKS-1 (highest valid tick). Verify boundary.
A6. Add order with invalid price (below base). Verify: returns NULL_IDX, no state mutation.
A7. Add order with price mapping to tick >= MAX_TICKS. Verify: returns NULL_IDX, no state mutation.
A8. Add order with quantity = 0. Verify: returns NULL_IDX, no state mutation.
A9. Add MAX_ORDERS orders. Verify: last order succeeds, returns order_id = MAX_ORDERS-1.
A10. Add MAX_ORDERS+1 orders. Verify: the (MAX_ORDERS+1)th add returns NULL_IDX.

**Category B — Cancel only (after add)**

B1. Add one order, cancel it. Verify: level count = 0, bitmap bit cleared, DEAD_FLAG set on node.
B2. Add two orders at same level, cancel head. Verify: head is now the second order; count = 1; bitmap still set.
B3. Add two orders at same level, cancel tail (non-head, mid-queue at q=2). Verify: head is still first; count = 1.
B4. Add five orders at same level, cancel the third (mid-queue, q=5). Verify: chain is still ordered, count = 4.
B5. Cancel with order_id that was never issued (>= next_slot). Verify: returns false, no mutation.
B6. Cancel the same order_id twice. Verify: second cancel returns false (DEAD_FLAG already set), no mutation.
B7. Add at two different price levels, cancel one. Verify: the other level is unaffected; all invariants hold.
B8. Add N orders at one level, cancel all N. Verify: level count = 0, bitmap bit cleared, head_idx = NULL_IDX, tail_idx = NULL_IDX.

**Category C — Match only (after add)**

C1. Add one ask at tick 100. Call match(BID, price=tick_100, qty=10). If ask qty=10: verify fill_count=1, remaining_qty=0, ask level empty, bitmap cleared.
C2. Add one ask at tick 100, qty=20. Match(BID, tick=100, qty=10). Verify: partial fill, ask node quantity decremented to 10, level still live.
C3. Add two asks at tick 100, each qty=5. Match(BID, tick=100, qty=8). Verify: first ask fully filled (5), second ask partially filled (3), fill_count=2.
C4. Match with no crossing orders. Verify: fill_count=0, remaining_qty = input qty, no state mutation.
C5. Match crossing multiple levels. Add asks at ticks 100, 101, 102. Match(BID, tick=102, qty=large). Verify: fills at 100 first (price priority), then 101, then 102 (time-priority within each level preserved).
C6. Match generates exactly 64 fills. Verify: remaining_qty > 0 (match stopped at 64 fill limit), fill_count = 64.
C7. Match asks with a BID at ask price -1 tick (does not cross). Verify: fill_count=0, no mutation.

**Category D — Combined operations**

D1. Add, cancel, add again at the same level. Verify: second add uses a new slot, FIFO order correct.
D2. Interleaved add and match: add 100 asks, match 50, verify remaining 50 are in FIFO order.
D3. Add N orders across M price levels, cancel all odd-order_id orders, verify all invariants.
D4. Full session simulation: add/cancel/match sequence from a pre-recorded order stream. Verify fill results against independently computed expected fills.
D5. Reset: after a full session, call book_reset, verify all levels empty, bitmap all zero, next_slot = 0.

### Oracle rules

The test oracle must derive expected results independently of the implementation. Oracles must not call the implementation to compute the expected answer.

**Oracle rule 1 (add)**: expected order_id for the k-th valid add is k-1 (zero-indexed). The oracle maintains its own add counter.

**Oracle rule 2 (cancel)**: a cancel is expected to succeed if and only if the order_id was previously issued by a valid add and has not been previously cancelled or fully matched. The oracle maintains a set of live order_ids.

**Oracle rule 3 (match)**: the oracle independently simulates the matching loop: for each call to match, iterate the opposite side from its best price toward the aggressor price, consuming orders in FIFO order. The oracle computes the expected sequence of fills and expected remaining_qty. The oracle's state is maintained as a separate data structure (e.g. a Python dictionary or a simple list-based simulation) that does not share code with the C or C++ implementation.

**Oracle rule 4 (best bid/ask)**: the expected best bid tick is the maximum tick t for which the oracle's bid side has at least one live order. The oracle computes this from its independent order tracking, not from calling book_best_bid.

**Oracle rule 5 (invariant check)**: the invariant checker traverses the actual book's data structures by pointer (it has full read access to book internals). It does not use the book's public API to check invariants — it reads raw struct fields and verifies them against the oracle's state.

---

## Benchmark Contract

### General rules

All benchmarks must be run in two cache regimes:
- **Warm**: operation immediately follows a cache-warming pass over the relevant data structures
- **Cold**: operation follows a cache-eviction step (flush using `clflush` or a large memset that evicts the book from L1/L2/L3)

Timing must use `RDTSC` with serialising instructions (`CPUID` or `LFENCE` before, `LFENCE` after). The benchmark harness must report median, 99th percentile, and maximum latency in cycles. Do not report averages only — tail latency matters.

**CPU pinning**: All benchmark runs must be pinned to a core other than core 0 (and core 1 on hyperthreaded CPUs) to avoid OS interrupt affinity noise. Use `taskset -c 2 ./benchmark` or equivalent. For maximum isolation, the target core should be removed from the kernel scheduler via `isolcpus=2` in the kernel boot parameters. The benchmark report must state which core was used and whether `isolcpus` was active.

Compiler optimisation: compile at `-O2`. Do not use `-O3` unless measuring the effect of additional optimisation is a stated goal. Both C and C++ benchmarks use the same `-O2 -Wconversion -Werror` flags (C) and `-O2 -Wconversion -Werror` flags (C++).

### Benchmark B1 — Add latency

**Operation**: `book_add(book, BID, price, qty)`
**Measurement**: median cycles per add over 100,000 adds at a single price level, then over 100,000 adds spread across 100 price levels.
**Cache regime**: warm (levels array and bitmap are in cache from prior adds).
**Expected result**: 10–30 cycles. Less than 50 cycles median is the target.

### Benchmark B2 — Cancel latency by queue depth

**Operation**: `book_cancel(book, order_id, side, tick)` for orders at a specific queue position.
**Setup**: fill a level to queue depth q before each cancel measurement. Cancel the order at position p (0-indexed from head). Repeat for q = 1, 5, 10, 50 and for p = 0 (head), p = q/2 (middle), p = q-1 (tail).
**Measurement**: median cycles per cancel for each (q, p) combination.
**Cache regime**: both warm and cold.
**Primary purpose**: inform the singly-linked promote decision. Report in a table of (q, p, warm_median_cycles, cold_median_cycles).
**Threshold**: if median cancel cycles at (q=10, p=middle) exceeds 50 cycles warm, report this explicitly — it is the doubly-linked promote trigger.

### Benchmark B3 — Match latency (single level)

**Operation**: `book_match(book, BID, price, qty, taker_id)` against a single ask level with one resting order.
**Measurement**: median cycles per match over 100,000 match operations.
**Cache regime**: warm.
**Expected result**: 15–40 cycles.

### Benchmark B4 — Match latency (multi-level)

**Operation**: `book_match` consuming k = 1, 5, 10 price levels.
**Setup**: pre-fill k ask levels, each with one order. Match a BID of sufficient qty to consume all k levels.
**Measurement**: median cycles per operation for each k.
**Cache regime**: warm.
**Purpose**: characterise the cost of level-crossing matches; confirm O(k) behaviour.

### Benchmark B5 — Best bid/ask scan

**Operation**: `book_best_bid(book)` with varying numbers of active bid levels.
**Setup**: vary the number of active bid levels N from 1 to 138 (the number of bitmap words), distributed uniformly across the bitmap range.
**Measurement**: median cycles per call for each N.
**Cache regime**: warm (bitmap in L1).
**Purpose**: confirm O(BITMAP_WORDS) in the degenerate case; confirm the bitmap is L1-resident.

### Benchmark B6 — C vs C++ head-to-head

Run benchmarks B1, B2 (all q and p), B3, B4 simultaneously on C and C++ implementations under identical conditions (same hardware, same compiler version, same flags, same input sequence).

Report: cycles per operation for each benchmark, both implementations, both cache regimes. State the hardware (CPU model, L1/L2/L3 sizes) and software (compiler version, flags) explicitly in the benchmark report.

The head-to-head is only valid if:
- Both implementations use the identical struct layouts (verified by static_assert on identical sizeof values)
- Both implementations use identical input sequences (identical order_id assignment, identical prices and quantities)
- The benchmark harness is not part of either implementation — it is external to both

---

## Pre-Handoff Checklist

- [x] Data model section complete: all types, sizes, and rationale stated (order_node_t 16 B, price_level_t 16 B, book_side_t 141904 B, arena_t ~16 MB, fill_t 16 B, fill_result_t with 64-fill bound)
- [x] Decision register complete: locked/conditional/open clearly labelled (9 locked, 3 resolved here — sliding window, promote condition, matcher)
- [x] Every public function has precondition, postcondition, and error behaviour (book_create, book_destroy, book_add, book_cancel, book_match, book_best_bid, book_best_ask, book_level_count, book_level_qty, book_reset)
- [x] API boundary explicitly named and documented (price_to_tick function, one sanctioned cast, validation checklist)
- [x] Struct layout table present with sizeof values and static_assert requirements
- [x] Performance contract stated (operation, complexity, cache tier, cycle targets)
- [x] No design decisions left for the implementation agent to make (all three open decisions from the brief are resolved; promote condition is a user decision triggered by benchmark results, not an implementation agent decision)
- [x] No open questions that block the implementation (the promote-to-doubly-linked path is conditional on a benchmark result; the initial implementation is singly-linked throughout — no blocker)

---

## Notes for agentC / agentCPP

### Notes for both

1. **Node size is a hard ceiling.** `sizeof(order_node_t)` must be exactly 16 bytes. Add a `static_assert` immediately after the struct definition. If any field is added that would grow the node beyond 16 bytes, the implementation is wrong — do not proceed without flagging this to the user.

2. **Bitmap updates are synchronous.** Every function that drains a level (`queue_remove` when count reaches 0, `queue_dequeue_head` when count reaches 0, partial fills in the matcher when the level empties) must call `bitmap_clear` before returning. There is no deferred or lazy bitmap update. This is a correctness requirement, not a performance hint.

3. **NULL_IDX is 0xFFFFFFFF.** It must never be a valid slot index. Because `MAX_ORDERS = 1,000,000` and `next_slot` never exceeds this value, slot index 0xFFFFFFFF is never issued. Do not add a runtime check for this — it is guaranteed by the session bound.

4. **The matcher is a named module, not a loop in main.** The matcher has its own file (matcher.c / matcher.cpp) and its own header. The `book_match` public function delegates entirely to `matcher_execute`. There is no matching logic outside the matcher module.

5. **book_add does not match.** `book_add` is a pure placement function. If the caller wants to check for crossing, it must call `book_match` first. `book_add` does not check `best_ask` or `best_bid` and does not call the matcher.

6. **Caller supplies side and tick on cancel.** `book_cancel` takes `side_t side` and `tick_t tick` as parameters. The caller is responsible for tracking which side and price level each order_id rests at. This information was available at the time of `book_add` — the caller stores it. The book does not maintain a reverse lookup from order_id to (side, tick).

7. **fill_result_t cap is 64.** If a match operation would produce more than 64 fills, stop at 64 and return the remaining quantity in `remaining_qty`. Do not allocate dynamically. This cap is a stated implementation constraint visible in the API documentation.

8. **base_price is a runtime parameter.** The book is not compiled for a specific ES reference price. `base_price` is passed to `book_create`. All tick arithmetic uses `base_price` at runtime. This allows the same binary to handle different session reference prices.

9. **Invariant checker is separate from the implementation.** The test harness's invariant checker (walking the FIFO chains, checking bitmap consistency) is a separate function with read access to book internals. It must not call `book_add`, `book_cancel`, or `book_match` to check its state. It reads raw struct fields.

10. **Static assertions are not optional.** Every struct that has a stated sizeof in this specification must have a `static_assert` (C11: `_Static_assert`; C++11: `static_assert`) immediately following the struct definition. If any assertion fails, the build must not proceed.

### Notes specific to agentC

- Use C11 (`-std=c11`). `_Static_assert` is available. `_Bool` is available.
- Use `<stdbool.h>` for `bool`, `true`, `false`.
- All public functions that can fail return either a sentinel value (`NULL_IDX` for order_id returns) or `bool`. There are no output parameters for error codes.
- The `-Wconversion -Werror` flag is the primary cast enforcement mechanism. Any implicit narrowing conversion between `uint32_t` and `uint64_t` or between any two integer types of different width must produce a compile error. Fix the types, not the cast.
- Module state is passed as explicit pointer arguments. There is no global state.
- Use `<string.h>` `memset` for `book_reset` only. Do not use `memset` inside hot-path functions.

### Notes specific to agentCPP

- Use C++17 (`-std=c++17`). `if constexpr`, structured bindings, and `[[nodiscard]]` are available.
- Struct layouts must be `standard_layout` types to ensure ABI compatibility with the C version. Do not add virtual functions, inheritance, or non-trivial constructors to `order_node_t`, `price_level_t`, or `fill_t`. These structs must be layout-compatible with their C counterparts.
- Use `enum class side_t : uint8_t { BID = 0, ASK = 1 }`. Use explicit `static_cast<size_t>(side)` when indexing `sides[side]`. Do not allow implicit conversion from `side_t` to integer.
- All functions that return `order_id_t` must be marked `[[nodiscard]]`.
- The C++ implementation wraps the same flat-array / bitmap / arena architecture. Do not introduce `std::unordered_map`, `std::map`, `std::list`, or any STL container on the hot path. The standard library is permitted for test harnesses and non-hot-path code only.
- `std::bit_width`, `std::countr_zero`, `std::countl_zero` (C++20 `<bit>`) are preferred over `__builtin_ctzll` / `__builtin_clzll` if targeting C++20. If C++17 only, use the GCC/Clang built-ins directly and document them.
- The C++ `Book` class provides the same interface as the C functions. The class is move-constructible and move-assignable. Copy constructor and copy assignment are deleted (the book is too large to copy accidentally).
