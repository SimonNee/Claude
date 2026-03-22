# Architecture Specification — ETH/USDT Binance Spot L2 Orderbook

**Produced by**: agentArchitect
**Inputs**: project_eth_orderbook.md (agentContext findings), ES architect-spec.md (reference implementation), ES book.hpp (reference implementation), problem brief
**Date**: 2026-03-22
**Target implementation**: C++ (agentCPP)
**Status**: DRAFT — ready for agentCPP implementation

---

## Scope and Constraints

This specification covers a Binance spot ETH/USDT L2 MBP (Market By Price) orderbook. The orderbook receives aggregate quantity per price level from the Binance WebSocket depth stream. Operations are `upsert(price, qty)` — set level to qty if qty > 0, delete level if qty == 0. There are no individual orders, no arena, no FIFO queues, no matching engine.

Key constraints that differ from the ES reference implementation:

- No compile-time price range bound (Binance price filter is $0.01–$10,000,000 — no fixed array spanning the full range is feasible).
- Prices arrive as ASCII decimal strings, not floating-point doubles.
- Quantities are fractional ETH with 8 decimal places.
- 24/7 operation — no session boundary, no daily arena reset.
- Feed model: snapshot (top 1,000 levels each side) followed by continuous delta updates.

---

## Decision Register

| Decision | Status | Resolution / Condition |
|---|---|---|
| Level index structure | LOCKED | Price-banded sliding window array, one per side. Array of WINDOW_SIZE price_level_t entries indexed by `(absolute_tick - window_base_tick) & WINDOW_MASK`. Rationale: O(1) upsert, O(1) delete (non-best level), O(SUMMARY_WORDS) delete (best level via hierarchical bitmap fallback) — same strategy as ES. Hash map rejected: O(n) scan on best-level delete is unacceptable with 500–2,000 live levels. ART rejected: no C++ production evidence, non-trivial implementation risk. |
| Active window size (WINDOW_SIZE) | LOCKED | 65,536 ticks. Covers ±10% at ETH prices up to $3,276 (best_tick never reaches within REBASE_MARGIN of window edge). If ETH price exceeds $3,276, rebase events become more frequent — this is acceptable as a cold-path operation. The value is a power of two: window index computation uses bitmasking (`& WINDOW_MASK`) with no integer division. WINDOW_MASK = WINDOW_SIZE - 1 = 65,535. |
| Rebase trigger threshold | LOCKED | REBASE_MARGIN = WINDOW_SIZE / 8 = 8,192 ticks from either window edge. A rebase is triggered when best_bid_tick approaches within REBASE_MARGIN of window_base_tick (low edge), or when best_ask_tick approaches within REBASE_MARGIN of the high edge (window_base_tick + WINDOW_SIZE - 1). Rebase is a cold-path operation. |
| Best-tick maintenance on delete | LOCKED | Cached `best_tick` field per side, updated synchronously. On upsert (qty > 0): update best_tick with compare-and-replace if new tick is better. On delete (qty == 0) where deleted tick != best_tick: no change to best_tick (O(1)). On delete (qty == 0) where deleted tick == best_tick: execute hierarchical bitmap fallback scan (O(SUMMARY_WORDS) = O(16) TZCNT/LZCNT operations). Bitmap must be cleared for the deleted level before the fallback scan executes. |
| Price-to-tick conversion | LOCKED | String-to-integer parse, no floating-point. Two-phase parse: integer part and fractional part as separate integer values. Tick = integer_part × 100 + fractional_cents, where fractional_cents is the two-digit centavo value parsed from the first two digits after the decimal point. Absolute tick is then the raw integer tick value; window index = (absolute_tick - window_base_tick) & WINDOW_MASK. One sanctioned arithmetic: integer subtraction and bitmasking only — no float, no cast from float to integer. See API Boundary section for the full parse specification. |
| qty_t representation | LOCKED | uint64_t scaled integer, scale factor 10^8 (i.e., 1 ETH = 100,000,000 units). Rationale: Binance quantities have up to 8 decimal places. Storing as uint64_t eliminates all floating-point error accumulation on qty aggregation. Maximum representable quantity: ~184 billion ETH (uint64_t max / 10^8) — far exceeds the total ETH supply (~120M ETH). String-to-qty parse is exact: integer part × 10^8 + fractional part (up to 8 digits, zero-padded to 8 digits). No float cast anywhere in the qty path. |
| Lifetime / reset strategy | LOCKED | No automatic reset. The book is constructed once at startup. `reset(uint64_t new_base_tick)` is called explicitly by the caller on snapshot re-sync or feed reconnect. Reset clears all level arrays, zeros all bitmaps, resets best_tick fields to TICK_INVALID, and sets window_base_tick to new_base_tick. Since there is no arena, reset cost is O(WINDOW_SIZE) per side (memset of level arrays and bitmap arrays). |
| Snapshot apply strategy | LOCKED | `apply_snapshot(side, array_of_levels, count)` iterates the snapshot array and calls `upsert_impl<IsBid>` for each level. The snapshot is a bulk replace — the caller must call `reset()` before `apply_snapshot()` to clear any prior state. Sequence: `reset()` → `apply_snapshot(BID, ...)` → `apply_snapshot(ASK, ...)` → begin streaming deltas. |
| No matching engine | LOCKED | ETH L2 MBP has no matching engine. There is no `match()` function, no `fill_t`, no `fill_result_t`. The matcher module from ES does not exist in this implementation. |
| No arena, no order_node_t | LOCKED | L2 MBP tracks only aggregate quantity per level. There is no per-order tracking, no FIFO queue within a level, no singly-linked list, no arena. Each price level holds exactly one uint64_t total_qty. |
| Template dispatch for bid/ask | LOCKED | `if constexpr (IsBid)` template pattern from ES is retained. `upsert_impl<true>` handles bids (best_tick = highest tick); `upsert_impl<false>` handles asks (best_tick = lowest tick). The template is private to the Book class. |
| C++ only | LOCKED | Only a C++ implementation is produced for this contract. No C version. The ES C-vs-C++ comparison is complete; this contract focuses on a new data model problem. |
| Namespace | LOCKED | `eth::book` — mirrors `es::book`. No name collision with the ES implementation. |

---

## Data Model

### Fundamental Constants

```
WINDOW_SIZE       = 65536        /* sliding window width in ticks; power of two */
WINDOW_MASK       = 65535        /* WINDOW_SIZE - 1; used for fast modular indexing */
BITMAP_WORDS      = 1024         /* WINDOW_SIZE / 64 = 65536 / 64 */
SUMMARY_WORDS     = 16           /* ceil(BITMAP_WORDS / 64) = ceil(1024 / 64) */
REBASE_MARGIN     = 8192         /* WINDOW_SIZE / 8; rebase trigger distance from edge */
QTY_SCALE         = 100000000ULL /* 10^8; one ETH = 100,000,000 qty units */
TICK_INVALID      = 0xFFFFFFFFU  /* sentinel for "no best level" / invalid tick */
NULL_BASE_TICK    = 0xFFFFFFFFFFFFFFFFULL /* sentinel for "window not initialised" */
```

These are `static constexpr` members of the `eth::book` namespace (or a dedicated constants header).

Compile-time static assertions required:
- `WINDOW_SIZE == 65536` (power of two)
- `WINDOW_MASK == WINDOW_SIZE - 1`
- `BITMAP_WORDS == WINDOW_SIZE / 64`
- `SUMMARY_WORDS == (BITMAP_WORDS + 63) / 64`
- `REBASE_MARGIN == WINDOW_SIZE / 8`

### Primary Types

| Type name | Representation | Size | Rationale |
|---|---|---|---|
| tick_t | uint32_t | 4 B | Absolute integer price index. 1 tick = $0.01. Price $2,345.67 → tick 234,567. Window index is derived from tick, not stored separately. |
| window_idx_t | uint32_t | 4 B | Window-relative slot index: `(tick - window_base_tick) & WINDOW_MASK`. Range [0, WINDOW_SIZE). Direct array index into levels[]. |
| qty_t | uint64_t | 8 B | Scaled integer, scale 10^8. 0 means level is absent. uint64_t required: Binance qty can be large (e.g. 10,000 ETH = 10^12 units). |
| abs_tick_t | uint64_t | 8 B | Absolute tick for the window base. uint64_t to avoid overflow at extreme prices (price $100,000 = tick 10,000,000 — fits uint32_t but uint64_t avoids arithmetic overflow during subtraction). Stored as `window_base_tick`. |
| side_t | uint8_t enum | 1 B | BID = 0, ASK = 1. Same as ES. |

Note on tick_t size: At $10,000,000 per ETH (exchange max), absolute tick = 10,000,000 / 0.01 = 1,000,000,000 which fits in uint32_t (max ~4.29 billion). However, `window_base_tick` involves subtraction of two absolute ticks and must not underflow; using uint64_t for `window_base_tick` makes the arithmetic unambiguous. The window_idx_t derived from the subtraction is always masked to [0, WINDOW_SIZE) and fits uint32_t.

In C++: `enum class side_t : uint8_t { BID = 0, ASK = 1 }`.

### Struct Layouts

#### price_level_t — one entry per active window slot per side

```
struct price_level_t {
    uint64_t total_qty;   /* offset 0, size 8 — scaled qty (units of 10^-8 ETH); 0 = empty slot */
};                        /* total: 8 bytes */
```

`static_assert(sizeof(price_level_t) == 8)` — mandatory.

An empty level has `total_qty == 0`. The bitmap bit for this slot must equal `(total_qty > 0)` at all times. These two representations are always synchronised — bitmap is never lazily updated.

Note: the `price_level_t` wrapper is retained for naming consistency with the ES spec and to preserve the module abstraction (the level index module operates on `price_level_t`, not raw `uint64_t`). The single-field struct adds no overhead.

#### book_side_t — one complete side (bid or ask)

```
struct book_side_t {
    price_level_t levels[WINDOW_SIZE];     /* offset      0 — 65,536 × 8 = 524,288 bytes */
    uint64_t      bitmap[BITMAP_WORDS];    /* offset 524288 —  1,024 × 8 =   8,192 bytes */
    tick_t        best_tick;               /* offset 532480 —              4 bytes (fast path) */
    uint32_t      _side_pad;               /* offset 532484 —              4 bytes (align summary[]) */
    uint64_t      summary[SUMMARY_WORDS];  /* offset 532488 —     16 × 8 =  128 bytes (fallback path) */
};                                         /* total:                       532,616 bytes */
```

`static_assert(sizeof(book_side_t) == 532616U)` — mandatory.

Field order rationale:
- `levels[]` at offset 0: largest, most frequently accessed, occupies lowest addresses.
- `bitmap[]` immediately after levels: set/cleared on every upsert/delete; follows levels in address space for predictable prefetch.
- `best_tick` just past bitmap: single hot query field (one 4-byte load per best_bid/best_ask call on the fast path).
- `_side_pad` keeps `summary[]` 8-byte aligned.
- `summary[]` last: fallback only; loaded only on best-level delete.

Cache tier analysis:
- `levels[]`: 524,288 bytes per side — L3-resident. The near-mid working set (levels within ±1,000 ticks of mid, ~16 KB) is L2-warm after the initial snapshot is applied.
- `bitmap[]`: 8,192 bytes per side — L2-resident in normal operation; both sides together (16,384 bytes) fit in L2 on any CPU with L2 >= 256 KB.
- `best_tick` + `summary[]`: 136 bytes per side — L1-resident.

#### Book::Impl — the complete book

```
struct Impl {
    book_side_t  sides[2];           /* sides[0] = BID, sides[1] = ASK */
    uint64_t     window_base_tick;   /* absolute tick at window slot 0; UINT64_MAX if uninitialised */
    uint64_t     _impl_pad;          /* explicit pad to maintain 8-byte alignment after window_base_tick */
};
```

Total size: 2 × 532,616 + 8 + 8 = 1,065,248 bytes (~1.04 MB).

`static_assert(sizeof(Book::Impl) == 1065248U)` — mandatory. Note: this assertion is platform-sensitive (padding depends on ABI). agentCPP must compute the correct value by compiling and verifying; the value above assumes no additional compiler padding between `sides[2]` (last byte at offset 1,065,232) and `window_base_tick` (uint64_t, 8-byte aligned, placed at offset 1,065,232 if that offset is 8-byte aligned, which it is since 1,065,232 = 2 × 532,616 is a multiple of 8). The static_assert value is correct under this alignment; agentCPP must verify.

The complete book is allocated on the heap (~1.04 MB), not on the stack.

### Ownership Model

`Book` owns its `Impl` via a raw heap pointer (`Impl* impl_`). There are no other heap allocations. The constructor allocates; the destructor frees. Move semantics are defined. Copy is deleted.

Callers never receive pointers into `Book::Impl`. Public functions accept and return values only (`tick_t`, `qty_t`, `bool`).

---

## API Boundary

The API boundary is where external string representations (price strings, quantity strings) are converted to internal types. All validation and conversion occurs at the first line of each public upsert call, before any internal state is read or written. The boundary functions are:

### parse_price(const char* s, size_t len) → tick_t

Converts a Binance price string (e.g. "2345.67") to an absolute integer tick.

Algorithm:
1. Scan forward to find the decimal point '.'. If no decimal point, treat fractional part as 0.
2. Parse integer part before '.': accumulate decimal digits into a uint64_t `int_part`.
3. Parse fractional part after '.': read exactly the first 2 digits (cents). If fewer than 2 digits follow the decimal point, zero-pad on the right. Ignore digits beyond position 2.
4. `absolute_tick = int_part * 100 + fractional_cents` — this is the price in units of $0.01.
5. Return `static_cast<tick_t>(absolute_tick)` — the one sanctioned cast in the price parse path. Precondition: absolute_tick < UINT32_MAX.

Examples:
- "2345.67" → int_part=2345, frac=67 → tick=234567
- "2345.00" → int_part=2345, frac=0 → tick=234500
- "2345.6" → int_part=2345, frac=60 (right-pad) → tick=234560
- "2345" → int_part=2345, frac=0 → tick=234500
- "10000000.00" → int_part=10000000, frac=0 → tick=1000000000 → valid uint32_t (< 4,294,967,295)

Error conditions:
- Empty string: returns TICK_INVALID.
- Non-numeric character before decimal point: returns TICK_INVALID.
- absolute_tick overflows uint32_t: returns TICK_INVALID.

No floating-point arithmetic. No `atof`, `strtod`, or `sscanf`. Pure integer string parse.

`parse_price` is declared `[[nodiscard]] static tick_t parse_price(const char* s, size_t len) noexcept` in the Book class (private static) or as a free function in the `eth::book` namespace. It is the only place a price string is parsed.

### parse_qty(const char* s, size_t len) → qty_t

Converts a Binance quantity string (e.g. "1.23456789") to a scaled integer qty_t.

Algorithm:
1. Find decimal point.
2. Parse integer part before '.': uint64_t `int_part`.
3. Parse fractional part after '.': read up to 8 digits. Accumulate into `uint64_t frac_part`. If fewer than 8 digits follow the decimal point, multiply frac_part by 10^(8 - digit_count) to right-pad to 8 digits. Ignore digits beyond position 8.
4. Return `int_part * QTY_SCALE + frac_part`.

Examples:
- "1.23456789" → int_part=1, frac=23456789 → qty=123456789
- "0.00100000" → int_part=0, frac=100000 → qty=100000
- "1000.0" → int_part=1000, frac=0 → qty=100000000000
- "0.0" → int_part=0, frac=0 → qty=0 (deletion signal in context of upsert)

Error conditions:
- Empty string: returns UINT64_MAX as error sentinel (QTY_INVALID).
- Non-numeric character: returns QTY_INVALID.
- Overflow of uint64_t during parse: returns QTY_INVALID.

`parse_qty` is `[[nodiscard]] static qty_t parse_qty(const char* s, size_t len) noexcept` — private static in Book or free function in `eth::book` namespace.

### window_index(tick_t absolute_tick, uint64_t window_base_tick) → window_idx_t

Converts an absolute tick to a window-relative slot index.

`return static_cast<window_idx_t>((static_cast<uint64_t>(absolute_tick) - window_base_tick) & WINDOW_MASK)`

Preconditions (checked by caller before this call):
- `window_base_tick != NULL_BASE_TICK` (book is initialised)
- The result of the subtraction, after masking, must correspond to a slot that was recently observed as active in the current window — this is not checked here; the caller ensures it by the rebase invariant.

This is the one sanctioned window arithmetic computation. It appears only in `upsert_impl<IsBid>`. It does not appear in any other function.

### Enforcement of no-cast rule

Build flags: `-std=c++17 -O2 -march=native -Wall -Wextra -Wconversion -Wsign-conversion -Werror -fno-exceptions`.

Sanctioned casts (the only ones permitted):
1. `static_cast<tick_t>(absolute_tick)` inside `parse_price()` — uint64_t → uint32_t after bounds check.
2. `static_cast<window_idx_t>((uint64_t - uint64_t) & WINDOW_MASK)` inside `window_index()` — uint64_t → uint32_t; the mask guarantees the value fits.
3. `static_cast<uint8_t>(s)` when indexing `sides[side]` — same pattern as ES.

No other casts are permitted. `-Wconversion -Werror` enforces this at the compilation unit level.

---

## Interface Specification

All public functions are methods on `class Book` in namespace `eth::book`. The book is too large for the stack; it is always heap-allocated inside `Book::Book()`.

### Book::Book(uint64_t initial_base_tick)

Precondition: `initial_base_tick` is a valid absolute tick value (not NULL_BASE_TICK). Typically derived from the mid-price of the first snapshot. If the initial base tick is not yet known, callers may pass `NULL_BASE_TICK` and call `reset(new_base_tick)` before the first upsert.
Postcondition: `impl_` is heap-allocated. All `price_level_t` entries in both sides have `total_qty == 0`. All bitmap words are 0. All summary words are 0. Both `best_tick` fields are `TICK_INVALID`. `window_base_tick == initial_base_tick`.
Error: throws `std::bad_alloc` if heap allocation fails.
Note: `fno-exceptions` build flag requires the caller to handle this: if `-fno-exceptions` is active, `new` calls `std::terminate` on allocation failure rather than throwing. This is acceptable for an in-process LOB — allocation failure is not recoverable at runtime.

### Book::~Book() noexcept

Postcondition: `impl_` is freed. No other resources are held.

### Book::Book(Book&& other) noexcept

Postcondition: `this->impl_` is set to `other.impl_`. `other.impl_` is set to `nullptr`. `other` is left in a valid but empty state (all operations on `other` after move are undefined except destruction).

Copy constructor and copy assignment are deleted.

### Book::upsert(side_t side, const char* price_str, size_t price_len, const char* qty_str, size_t qty_len) → bool

This is the primary hot-path operation. It handles both set (qty > 0) and delete (qty == 0).

Precondition:
- `impl_ != nullptr` (book is constructed and not moved-from)
- `side` is `BID` or `ASK`
- `price_str` is a non-null, null-terminated or length-bounded Binance decimal string
- `qty_str` is a non-null, null-terminated or length-bounded Binance decimal string
- `window_base_tick != NULL_BASE_TICK` (book has been initialised with a valid window base)

Postcondition (qty > 0 — set):
- `absolute_tick = parse_price(price_str, price_len)` is computed; if TICK_INVALID, returns false without mutation.
- `qty = parse_qty(qty_str, qty_len)` is computed; if QTY_INVALID, returns false without mutation.
- `widx = window_index(absolute_tick, window_base_tick)` is computed.
- If `widx` is within [REBASE_MARGIN, WINDOW_SIZE - REBASE_MARGIN) (safety zone): proceed normally.
- If `widx` is outside the safety zone: `needs_rebase()` returns true; the current upsert is applied first, then the caller should invoke `rebase()`. The upsert is not blocked — it applies to the current window even near the edge.
- `levels[side][widx].total_qty = qty` (absolute set — not add).
- Bitmap bit `widx` is set in `bitmap[side]` and corresponding `summary[side]` word is updated.
- `best_tick` is updated: for BID, `best_tick = max(best_tick, absolute_tick)` if qty > 0; for ASK, `best_tick = min(best_tick, absolute_tick)` if qty > 0.
- Returns true.

Postcondition (qty == 0 — delete):
- Parse price to `absolute_tick`; compute `widx`.
- `levels[side][widx].total_qty = 0`.
- Bitmap bit `widx` is cleared in `bitmap[side]`; corresponding `summary[side]` word is updated (clear the summary bit if and only if the entire bitmap word is now zero).
- If `absolute_tick == best_tick[side]`: execute hierarchical bitmap fallback scan to find the new best_tick. Assign result to `best_tick[side]`; if the scan returns -1 (side is now empty), set `best_tick[side] = TICK_INVALID`.
- If `absolute_tick != best_tick[side]`: `best_tick[side]` is unchanged.
- Returns true.

Error:
- `parse_price` returns TICK_INVALID: returns false, no mutation.
- `parse_qty` returns QTY_INVALID: returns false, no mutation.
- `side` is neither BID nor ASK: returns false, no mutation.
- `window_base_tick == NULL_BASE_TICK` (book uninitialised): returns false, no mutation.
- Window overflow check: if `(static_cast<uint64_t>(absolute_tick) - window_base_tick) > WINDOW_MASK`, the tick is outside the current window entirely. Returns false, no mutation. This should not occur in normal operation (Binance PERCENT_PRICE filter keeps ticks within the window) but must be checked as a safety guard.

Implementation note on the best_tick compare for BID upsert: for BID side, best_tick is the highest tick. The compare is:
```
if (absolute_tick > best_tick || best_tick == TICK_INVALID) best_tick = absolute_tick;
```
This must use unsigned comparison correctly. `TICK_INVALID = UINT32_MAX`. Since `absolute_tick` is always a valid tick value (< UINT32_MAX), the condition `absolute_tick > best_tick` is false when `best_tick == TICK_INVALID` (UINT32_MAX > absolute_tick numerically), so the second clause `|| best_tick == TICK_INVALID` handles the empty-side case explicitly.

For ASK side, best_tick is the lowest tick:
```
if (absolute_tick < best_tick || best_tick == TICK_INVALID) best_tick = absolute_tick;
```

### Book::upsert_by_tick(side_t side, tick_t absolute_tick, qty_t qty) → bool

Tick-direct upsert: bypasses `parse_price` / `parse_qty`. Used by the benchmark harness when ticks have been pre-converted. Not part of the public protocol interface — it is a benchmarking hook. Same postconditions as `upsert()` from the point after parsing.

Precondition:
- `absolute_tick != TICK_INVALID`
- `qty != QTY_INVALID`
- All other preconditions of `upsert()` apply.

### Book::best_bid() const noexcept → tick_t

Precondition: none (safe to call on an empty book).
Postcondition: returns `impl_->sides[0].best_tick`. Returns `TICK_INVALID` if the bid side is empty or the book is uninitialised.
Implementation: single field load — no scan, no bitmap access. O(1), one memory read.
`[[nodiscard]]` required.

### Book::best_ask() const noexcept → tick_t

Precondition: none.
Postcondition: returns `impl_->sides[1].best_tick`. Returns `TICK_INVALID` if the ask side is empty.
Implementation: single field load. O(1), one memory read.
`[[nodiscard]]` required.

### Book::needs_rebase() const noexcept → bool

Postcondition: returns true if either `best_bid_tick` or `best_ask_tick` is within REBASE_MARGIN ticks of the window edge. Specifically:
- Let `bid_widx = (best_bid_tick != TICK_INVALID) ? window_index(best_bid_tick, window_base_tick) : WINDOW_SIZE/2`
- Let `ask_widx = (best_ask_tick != TICK_INVALID) ? window_index(best_ask_tick, window_base_tick) : WINDOW_SIZE/2`
- Returns true if `bid_widx < REBASE_MARGIN` or `ask_widx >= WINDOW_SIZE - REBASE_MARGIN`.

This function is called by the caller (feed handler) after each upsert. When it returns true, the caller should invoke `rebase()` at a convenient point (e.g. after processing the current batch of updates).

### Book::rebase(uint64_t new_base_tick) noexcept → void

Cold-path operation. Repositions the sliding window so that `new_base_tick` is the new slot-0 base. New_base_tick should be chosen such that the current best_bid and best_ask fall near the centre of the new window.

Precondition:
- `new_base_tick != NULL_BASE_TICK`
- `new_base_tick` is 64-tick-aligned (i.e., `new_base_tick % 64 == 0`) — required for bitmap word boundary alignment. If the caller supplies a non-aligned value, `rebase()` rounds down to the nearest 64-tick boundary: `new_base_tick = new_base_tick & ~63ULL`.

Algorithm:
1. Allocate a temporary Impl on the heap (or use a static scratch buffer — see Implementation Notes).
2. For each side s in {BID, ASK}:
   a. For each window slot widx in [0, WINDOW_SIZE):
      - If `levels[s][widx].total_qty > 0`:
        - Compute `old_absolute_tick = window_base_tick + widx` (exact, no masking needed here since we are iterating all slots).
        - Compute `new_widx = (old_absolute_tick - new_base_tick) & WINDOW_MASK`.
        - If `new_widx` is within [0, WINDOW_SIZE): copy level to `new_levels[s][new_widx]`.
        - If `new_widx` is out of bounds after rebase (price was outside the new window): the level is dropped silently. This can only happen if the new window does not cover the old level — which is valid if prices have moved significantly.
3. Set `window_base_tick = new_base_tick`.
4. Copy new levels and rebuild bitmaps for both sides from scratch.
5. Recompute `best_tick` for both sides from the rebuilt bitmaps.

Cost: O(WINDOW_SIZE) per side — iterates up to 65,536 slots. At ~10 ns per cache miss and ~512 KB per side, this is ~5–10 ms in the worst case (cold L3). Acceptable for a cold-path reconnect or once-per-minute drift event.

Implementation note: the "temporary Impl" approach requires ~1 MB temporary allocation. An alternative is an in-place shift if the window displacement is small (< WINDOW_SIZE / 2). The spec does not mandate which approach agentCPP uses internally, but the preconditions and postconditions above must hold regardless. The in-place approach is more complex; the temporary-copy approach is simpler and correct. Prefer simplicity (KISS).

### Book::reset(uint64_t new_base_tick) noexcept → void

Clears all state and re-initialises the book with a new window base. Called on snapshot re-sync or feed reconnect.

Precondition: `new_base_tick != NULL_BASE_TICK`. Typically the absolute tick derived from the mid-price in the new snapshot.
Postcondition: all `price_level_t` entries have `total_qty == 0`. All bitmap words are 0. All summary words are 0. Both `best_tick` fields are `TICK_INVALID`. `window_base_tick == new_base_tick`.
Implementation: `std::memset(impl_, 0, sizeof(Impl))` followed by `impl_->window_base_tick = new_base_tick`. This is correct because zero is the valid representation of "empty level" (total_qty == 0), "empty bitmap" (0 words), and TICK_INVALID = 0xFFFFFFFF cannot be set via memset. Therefore: after memset, all best_tick fields are 0 (not TICK_INVALID). agentCPP must explicitly set `impl_->sides[0].best_tick = TICK_INVALID` and `impl_->sides[1].best_tick = TICK_INVALID` after the memset.

### Book::apply_snapshot(side_t side, const tick_t* ticks, const qty_t* qtys, uint32_t count) → uint32_t

Applies a snapshot of up to `count` levels to the specified side. Caller must have called `reset()` before `apply_snapshot()`.

Precondition:
- `ticks != nullptr`, `qtys != nullptr`
- `count <= 1000` (Binance snapshot depth limit)
- All `ticks[i]` are valid absolute tick values
- All `qtys[i] > 0` (snapshot levels are all non-zero)
- `window_base_tick != NULL_BASE_TICK`

Postcondition: for each i in [0, count): `upsert_by_tick(side, ticks[i], qtys[i])` is called. Returns the number of levels successfully applied (may be less than count if any tick falls outside the current window).

### Book::level_qty(side_t side, tick_t absolute_tick) const noexcept → qty_t

Precondition: `side` is BID or ASK; `absolute_tick != TICK_INVALID`.
Postcondition: returns `levels[side][widx].total_qty` where `widx = window_index(absolute_tick, window_base_tick)`. Returns 0 if the tick is outside the current window.
Not on the hot path. Used for testing and inspection only.

### Book::window_base() const noexcept → uint64_t

Returns `impl_->window_base_tick`. Used by the test harness. Not on the hot path.

---

## Performance Contract

| Operation | Complexity | Cache tier (hot path data) | Notes |
|---|---|---|---|
| upsert (non-best-level, qty > 0) | O(1) | levels[]: L3 (near-mid: L2-warm). bitmap[]: L2. summary[]: L1. best_tick: L1. | 1 level write, 1 bitmap write, 1 summary write, 1 best_tick compare. |
| upsert (new best level, qty > 0) | O(1) | Same as above. | Same path + best_tick update. One additional write to best_tick field. |
| upsert (non-best-level, qty == 0 — delete) | O(1) | Same as above. | 1 level zero, 1 bitmap clear, conditional summary clear. No fallback scan. |
| upsert (best-level delete, qty == 0) | O(SUMMARY_WORDS) | summary[]: L1. bitmap[]: L2. | Hierarchical fallback scan: 2 TZCNT/LZCNT + 3 loads. SUMMARY_WORDS = 16. |
| best_bid() / best_ask() | O(1) | best_tick: L1. | Single field load. No scan. |
| needs_rebase() | O(1) | best_tick: L1. window_base_tick: L1 (adjacent to sides). | Two comparisons. |
| rebase() | O(WINDOW_SIZE) | levels[]: L3. | Cold path. ~65K slot iteration. Not on the message-processing hot path. |
| reset() | O(WINDOW_SIZE) | levels[]: L3. | memset of ~1 MB. Cold path. |
| apply_snapshot() | O(count) | levels[]: L3. bitmap[]: L2. | count <= 1000. Cold path. |
| parse_price() | O(len) | Registers. | String scan, no memory reads beyond input. Typically len <= 12 characters. |
| parse_qty() | O(len) | Registers. | String scan. Typically len <= 12 characters. |

**Target cycle counts (hot path, warm cache):**
- `upsert` (non-best-level delete or set): target < 30 cycles median.
- `upsert` (best-level delete with fallback): target < 60 cycles median (2 TZCNT + bitmap loads).
- `best_bid()` / `best_ask()`: target < 5 cycles (single load, L1-resident).

**Cache tier key:**
- L1: < 32 KB; always warm. `best_tick` (4 B), `summary[]` (128 B × 2 sides = 256 B total) — comfortably L1.
- L2: 256 KB – 1 MB typical. `bitmap[]` (8,192 B × 2 sides = 16,384 B) — L2-resident on any CPU with L2 >= 256 KB.
- L3: `levels[]` (524,288 B × 2 sides = 1,048,576 B) — L3-resident. Near-mid levels (within ±1,000 ticks of mid = ~16 KB) will be L2-warm after snapshot application.

---

## Module Boundaries

Each module hides exactly one design decision.

### Module 1: Price/Qty Parser (parser.hpp)

**Hides**: the choice of string-to-integer parse algorithm for Binance decimal format — specifically that prices are parsed as two-part integer arithmetic (integer_part × 100 + frac_cents) and quantities are parsed as 8-digit scaled integers, with no floating-point involved.

Public interface:
- `[[nodiscard]] tick_t parse_price(const char* s, size_t len) noexcept`
- `[[nodiscard]] qty_t  parse_qty(const char* s, size_t len) noexcept`

The caller knows that a valid ASCII decimal string becomes a `tick_t` or `qty_t`. The caller does not know how the decimal point is located or how digits are accumulated. Changing the parse implementation (e.g. to SIMD digit scanning) changes only this module.

### Module 2: Level Index (level_index.hpp)

**Hides**: the choice of price-banded sliding window array as the level index structure — specifically that the index is a flat array with modular addressing over a power-of-two window, maintained alongside a two-level hierarchical bitmap.

Public interface (all in namespace `eth::book`, operating on `book_side_t&`):
- `void level_set(book_side_t& side, window_idx_t widx, qty_t qty) noexcept`
- `void level_clear(book_side_t& side, window_idx_t widx) noexcept`
- `qty_t level_get(const book_side_t& side, window_idx_t widx) noexcept`
- `bool bitmap_test(const book_side_t& side, window_idx_t widx) noexcept`
- `tick_t best_bid_fallback(const book_side_t& side, uint64_t window_base_tick) noexcept`
- `tick_t best_ask_fallback(const book_side_t& side, uint64_t window_base_tick) noexcept`

The caller knows that levels can be set, cleared, and queried, and that a best-level fallback scan is available. The caller does not know about the flat array, the bitmap structure, or the two-level summary. Changing to a hash map or ART would change only this module (plus the `best_tick` fallback calls in `upsert_impl`).

### Module 3: Book (book.hpp / book.cpp)

**Hides**: the composition of the parser, level index, best_tick maintenance, and sliding window rebase into a single stateful object accessible via a narrow public API.

Public interface: the full set of `Book::` methods listed in the Interface Specification above.

The caller knows: prices and quantities as strings, upsert/delete semantics, best_bid/best_ask queries, reset and snapshot-apply operations. The caller does not know about the sliding window, the bitmap, the two-level summary, the parse implementation, or the rebase trigger.

---

## Implementation Notes

### Invariants maintained at all times

The following invariants must hold after every call to `upsert()`, `upsert_by_tick()`, `reset()`, `apply_snapshot()`, and `rebase()`:

1. **Bitmap-level consistency**: for every window slot widx on every side, `bitmap_test(side, widx)` must equal `(levels[side][widx].total_qty > 0)`. If the bitmap and level disagree, it is a correctness bug.

2. **Summary-bitmap consistency**: for every summary word index `sw` and every bit `b` within that word, `(summary[sw] >> b) & 1` must equal `(bitmap[sw * 64 + b] != 0)`. The summary bit must be set if and only if the corresponding bitmap word is non-zero.

3. **best_tick validity**: `best_tick[BID]` must equal the highest absolute tick t for which `levels[BID][window_index(t)]` is non-zero. If no such tick exists, `best_tick[BID] == TICK_INVALID`. Same requirement inverted for ASK (lowest non-zero tick).

4. **Window bounds**: `window_base_tick` is 64-tick-aligned at all times (required for bitmap word alignment). If `rebase()` is called with a non-aligned value, it rounds down before applying.

5. **Zero is the empty sentinel**: a level with `total_qty == 0` is treated as absent. A level write of 0 is equivalent to a delete. The bitmap bit for that slot must be 0.

### Ordering constraints within upsert_impl

On a **set** (qty > 0):
1. Write `levels[side][widx].total_qty = qty`.
2. Set bitmap bit: `bitmap[widx / 64] |= (1ULL << (widx % 64))`.
3. Set summary bit: `summary[(widx / 64) / 64] |= (1ULL << ((widx / 64) % 64))`.
4. Update `best_tick` (compare-and-replace, see Interface Specification).

On a **delete** (qty == 0):
1. Write `levels[side][widx].total_qty = 0`.
2. Clear bitmap bit: `bitmap[widx / 64] &= ~(1ULL << (widx % 64))`.
3. If the bitmap word is now zero: clear the corresponding summary bit.
4. If `absolute_tick == best_tick[side]`: run hierarchical fallback scan; assign result to `best_tick[side]` (or TICK_INVALID if side is empty).

The bitmap must be updated (step 2–3) **before** the fallback scan (step 4). The fallback scan reads the bitmap; if the bitmap is not yet updated, the scan will return the now-deleted level as the new best. This is a correctness ordering requirement.

### Hierarchical bitmap fallback scan

This mirrors the ES `bitmap_lowest_h` / `bitmap_highest_h` pattern. The scan operates over the window-relative indices and must convert the result back to an absolute tick before storing in `best_tick`.

For ASK side (lowest tick wins — uses `bitmap_lowest_h`):
1. Scan `summary[]` from word 0 upward for the first non-zero word `sw`.
2. Within `summary[sw]`, TZCNT to find the lowest set bit `sb` → `bmap_word = sw * 64 + sb`.
3. TZCNT within `bitmap[bmap_word]` to find bit `bb` → `window_idx = bmap_word * 64 + bb`.
4. `best_tick = window_base_tick + window_idx`.

For BID side (highest tick wins — uses `bitmap_highest_h`):
1. Scan `summary[]` from word SUMMARY_WORDS-1 downward for first non-zero word `sw`.
2. Within `summary[sw]`, LZCNT to find highest set bit `sb` → `bmap_word = sw * 64 + sb`.
3. LZCNT within `bitmap[bmap_word]` to find bit `bb` → `window_idx = bmap_word * 64 + (63 - clz)`.
4. `best_tick = window_base_tick + window_idx`.

Both functions return TICK_INVALID (as `tick_t`) if no set bit is found. The caller (`upsert_impl`) assigns the result directly to `best_tick[side]`.

### Summary word update rule

The summary bit at position `(widx / 64)` within summary word `(widx / 64) / 64` must be:
- Set when a bitmap bit is set (the set operation must also set the summary bit unconditionally — the summary bit may already be set, which is fine).
- Cleared only when the entire bitmap word `bitmap[widx / 64]` becomes zero after the bitmap clear. The clear check is: `if (bitmap[bmap_word] == 0) { summary[sw] &= ~(1ULL << sb); }`.

agentCPP must use helper functions `bitmap_set_bit(book_side_t&, window_idx_t)` and `bitmap_clear_bit(book_side_t&, window_idx_t)` to encapsulate this logic — these are the functions that maintain the two-level invariant atomically. These helpers are inline and must not be called from outside the level_index module.

### Rebase temporary allocation

The rebase algorithm requires a scratch buffer to hold the new state before committing. Two implementation options:

Option A (simple): allocate a second `Impl*` on the heap during rebase, populate it from the old state, then swap `impl_` pointers. Free the old Impl. This is ~1 MB temporary allocation during rebase — acceptable for a cold-path operation.

Option B (in-place shift): if `new_base_tick > window_base_tick` and the displacement `D = new_base_tick - window_base_tick < WINDOW_SIZE`, shift levels and bitmap in-place using `memmove` of the appropriate range. Rebuild summary from the shifted bitmap. This avoids temporary allocation but is more complex.

agentCPP must use Option A. The complexity risk of Option B is not justified given that rebase is rare (triggered by price drift, not per-message). KISS applies here.

### Binance snapshot-to-tick preprocessing

The `apply_snapshot` function accepts pre-parsed `tick_t` and `qty_t` arrays. The caller (feed handler) is responsible for parsing the Binance JSON snapshot and building these arrays using `parse_price` and `parse_qty`. The Book does not parse JSON.

`apply_snapshot` chooses the `window_base_tick` assumption: after `reset(new_base_tick)`, the window must be centred appropriately. The recommended pattern is:

```
tick_t mid_tick = (snapshot_best_bid_tick + snapshot_best_ask_tick) / 2;
uint64_t base_tick = (mid_tick - WINDOW_SIZE / 2) & ~63ULL;  // 64-tick-aligned
book.reset(base_tick);
book.apply_snapshot(BID, bid_ticks, bid_qtys, bid_count);
book.apply_snapshot(ASK, ask_ticks, ask_qtys, ask_count);
```

This pattern is documented in the interface but enforced by the caller, not the Book.

### Static assertions

The following `static_assert` statements are mandatory in `book.hpp`, immediately after each struct definition or constant declaration:

```cpp
static_assert(WINDOW_SIZE == 65536U);
static_assert(WINDOW_MASK == WINDOW_SIZE - 1U);
static_assert(BITMAP_WORDS == WINDOW_SIZE / 64U);
static_assert(SUMMARY_WORDS == (BITMAP_WORDS + 63U) / 64U);
static_assert(REBASE_MARGIN == WINDOW_SIZE / 8U);
static_assert(sizeof(price_level_t) == 8U);
static_assert(sizeof(book_side_t) == 532616U);
static_assert(alignof(price_level_t) == 8U);
static_assert(alignof(book_side_t) == 8U);
```

The `sizeof(Book::Impl)` assertion is placed in `book.cpp` after the `Book::Impl` struct is fully defined, since the compiler needs to see all fields. agentCPP must compute the actual sizeof value by compiling and print it in the first test run before asserting. If the value differs from 1,065,248, update the assert to match the compiler's result and flag the discrepancy.

### Build file structure

```
cpp/eth/
  book.hpp         — Book class declaration, all structs, constants, parse_price/parse_qty
  book.cpp         — Book method definitions, upsert_impl<>, rebase, reset
  test_book.cpp    — Test harness (agentTest)
  bench_book.cpp   — Benchmark harness (agentTest)
```

No separate `parser.hpp`, `level_index.hpp` source files are required — the module boundaries are logical (justified by hidden design decisions) but the implementation may be in a single `book.hpp` / `book.cpp` pair given the small total line count expected (~250–350 lines). KISS: do not split files unless size or independent compile-time testing requires it.

---

## Test Suite Contract

### Invariants the checker must verify after every operation

1. **Bitmap-level consistency**: for every window slot widx on both sides, `bitmap[widx/64] & (1ULL << (widx%64))` must equal `(levels[side][widx].total_qty > 0 ? 1 : 0)`.

2. **Summary-bitmap consistency**: for every summary word `sw` and bit `b`, `(summary[sw] >> b) & 1` must equal `(bitmap[sw * 64 + b] != 0 ? 1 : 0)`.

3. **best_tick — BID**: `best_tick[BID]` is the maximum absolute tick t such that `levels[BID][window_index(t)].total_qty > 0`. If no such tick exists, `best_tick[BID] == TICK_INVALID`.

4. **best_tick — ASK**: `best_tick[ASK]` is the minimum absolute tick t such that `levels[ASK][window_index(t)].total_qty > 0`. If no such tick exists, `best_tick[ASK] == TICK_INVALID`.

5. **Window alignment**: `window_base_tick % 64 == 0` at all times (or `window_base_tick == NULL_BASE_TICK`).

### Test case catalogue

**Category A — Upsert set (qty > 0)**

A1. Upsert one bid level. Verify: level qty correct, bitmap bit set, summary bit set, best_bid == that tick.
A2. Upsert two bid levels at different prices. Verify: best_bid == the higher tick.
A3. Upsert bid and ask at the same absolute tick. Verify: each side is independent; no interaction.
A4. Upsert at window slot 0 (lowest valid slot). Verify boundary.
A5. Upsert at window slot WINDOW_SIZE - 1 (highest valid slot). Verify boundary.
A6. Upsert with qty == 0 string ("0.00000000"). Verify: treated as delete; if level was previously empty, no change; if level was live, it is now absent.
A7. Upsert with price string that parses to TICK_INVALID. Verify: returns false, no mutation.
A8. Upsert with qty string that parses to QTY_INVALID. Verify: returns false, no mutation.
A9. Upsert 1,000 bid levels (snapshot simulation). Verify: all 1,000 levels set, best_bid == highest of the 1,000.
A10. Upsert same price twice (update). Verify: qty is replaced (not added); level qty == second value; bitmap still set.

**Category B — Upsert delete (qty == 0)**

B1. Set one bid level, then delete it. Verify: total_qty == 0, bitmap bit cleared, summary bit cleared if appropriate, best_bid == TICK_INVALID.
B2. Set two bid levels, delete the best one. Verify: best_bid falls back to the second level. Bitmap and summary still consistent.
B3. Set two bid levels, delete the non-best one. Verify: best_bid unchanged. O(1) path taken (no fallback scan needed).
B4. Delete a level that was never set (total_qty already 0). Verify: no state mutation, returns true (idempotent; Binance deltas may send a zero for a level that was already zero).
B5. Set N bid levels, delete all of them in reverse price order. Verify: after each delete, best_bid falls back correctly; after last delete, best_bid == TICK_INVALID, all bitmap words zero, all summary words zero.
B6. Set bid and ask levels, delete all bid levels. Verify: ask side unaffected throughout.

**Category C — Reset and snapshot apply**

C1. Apply a 1,000-level snapshot for each side. Reset. Verify: all levels zero, bitmaps zero, best ticks TICK_INVALID.
C2. Apply snapshot, process 100 delta upserts, reset, apply new snapshot. Verify: state matches second snapshot only.
C3. Apply snapshot with a level at window slot 0 and another at WINDOW_SIZE - 1. Verify both are set correctly.

**Category D — Parser unit tests**

D1. parse_price("2345.67", 7) == 234567.
D2. parse_price("10000000.00", 11) == 1000000000.
D3. parse_price("0.01", 4) == 1.
D4. parse_price("0.10", 4) == 10.
D5. parse_price("", 0) == TICK_INVALID.
D6. parse_price("abc.def", 7) == TICK_INVALID.
D7. parse_qty("1.23456789", 10) == 123456789.
D8. parse_qty("0.00000001", 10) == 1.
D9. parse_qty("1000.0", 6) == 100000000000ULL.
D10. parse_qty("0.0", 3) == 0.

**Category E — needs_rebase and rebase**

E1. Set best_bid within REBASE_MARGIN ticks of window low edge. Verify: needs_rebase() == true.
E2. Set best_ask within REBASE_MARGIN ticks of window high edge. Verify: needs_rebase() == true.
E3. Book with mid near centre. Verify: needs_rebase() == false.
E4. Rebase with a level that falls inside the new window. Verify: level is present in new window; old window slot is absent.
E5. Rebase with a level that falls outside the new window. Verify: level is dropped; no corruption.
E6. After rebase, best_bid and best_ask are correct absolute tick values.

---

## Benchmark Contract

### General rules

Same rules as ES: RDTSC with LFENCE serialisation, median + 99th + max, CPU pinning to core 2+, `-O2 -march=native`. Report CPU model, L1/L2/L3 sizes, compiler version.

### Benchmark B1 — Upsert latency (non-best-level set)

Operation: `upsert_by_tick(ASK, tick, qty)` where tick is not the current best_ask and level is new.
Measurement: median cycles per upsert over 100,000 calls at a single tick.
Cache regime: warm.
Expected result: < 30 cycles median.

### Benchmark B2 — Upsert latency (best-level delete with fallback)

Operation: repeatedly set and delete the best_ask level, forcing the hierarchical fallback scan each delete.
Setup: pre-fill 1,000 ask levels. Delete the current best_ask. Measure the delete cycle cost.
Measurement: median cycles per best-level delete.
Cache regime: warm (bitmap and summary in cache).
Expected result: < 60 cycles median.

### Benchmark B3 — best_bid / best_ask fast path

Operation: `book.best_ask()` with a live book.
Measurement: median cycles per call over 1,000,000 calls.
Cache regime: warm.
Expected result: < 5 cycles median (single field load).

### Benchmark B4 — Snapshot apply

Operation: `apply_snapshot(BID, ticks, qtys, 1000)` after `reset()`.
Measurement: total cycles for the full snapshot apply (1,000 levels per side).
Cache regime: cold (levels array freshly evicted before each measurement).
Purpose: characterise snapshot ingestion cost; baseline for feed reconnect overhead.

### Benchmark B5 — parse_price throughput

Operation: `parse_price(s, len)` on a fixed price string "2345.67".
Measurement: median cycles per call over 1,000,000 calls.
Expected result: < 15 cycles median.
Purpose: confirm that string parsing is not the bottleneck vs the level update.

---

## Pre-Handoff Checklist

- [x] Data model section complete: all types, sizes, and rationale stated (price_level_t 8 B, book_side_t 532,616 B, Book::Impl ~1.04 MB; all struct layouts with offset tables; ownership model).
- [x] Decision register complete: 12 decisions, all LOCKED, with rationale.
- [x] Every public function has precondition, postcondition, and error behaviour (Book constructor, destructor, move, upsert, upsert_by_tick, best_bid, best_ask, needs_rebase, rebase, reset, apply_snapshot, level_qty, window_base).
- [x] API boundary explicitly named and documented (parse_price and parse_qty, window_index; sanctioned casts enumerated; no-cast rule enforced by build flags).
- [x] Struct layout table present with sizeof values and static_assert requirements.
- [x] Performance contract stated (operation, complexity, cache tier, cycle targets).
- [x] No design decisions left for agentCPP to make (level index structure locked; window size locked; qty representation locked; rebase algorithm specified to Option A; build file structure specified; two-level bitmap hierarchy specified).
- [x] No open questions that block the implementation (all seven design decisions from the brief are locked).

---

## Notes for agentCPP

1. **Namespace is `eth::book`, not `es::book`.** All types, constants, and functions in this implementation live in `eth::book`. There is no name collision with the ES implementation if both are compiled together, but they are separate translation units.

2. **best_tick is an absolute tick, not a window index.** `book_side_t::best_tick` stores the absolute tick value (e.g. 234567 for $2,345.67), not the window_idx_t. The fallback scan converts the window_idx_t result back to an absolute tick by adding `window_base_tick`. The query functions `best_bid()` and `best_ask()` return absolute tick values. The caller converts absolute ticks back to prices by multiplying by 0.01 if needed.

3. **best_tick compare for BID must handle TICK_INVALID correctly.** `TICK_INVALID = UINT32_MAX`. When the bid side is empty, `best_tick = TICK_INVALID`. A new bid at any valid absolute tick `t` (which is always < UINT32_MAX) should set `best_tick = t`. The naive comparison `if (t > best_tick)` evaluates to `if (t > UINT32_MAX)` = false, which is wrong. The correct code: `if (best_tick == TICK_INVALID || t > best_tick) best_tick = t;`. For ASK: `if (best_tick == TICK_INVALID || t < best_tick) best_tick = t;`.

4. **Bitmap and summary must be updated atomically (in the sense of ordering, not atomics).** There is no concurrent access — this is single-threaded. But within a single `upsert_impl` call, the bitmap must be updated before the best_tick fallback scan. Do not reorder these operations.

5. **The `levels[]` array is indexed by window_idx_t, not by tick_t.** The levels array in `book_side_t` is `price_level_t levels[WINDOW_SIZE]` — indexed 0 to 65,535. Indexing by absolute tick directly would be wrong. All access to `levels[]` goes through `window_index()`.

6. **WINDOW_SIZE is a power of two.** `window_index()` uses `& WINDOW_MASK` not `% WINDOW_SIZE`. Do not replace the bitmask with a modulo.

7. **The rebase function uses Option A (temporary allocation).** Allocate a new `Impl*` via `new`, populate it from the old state, swap, free the old. This is ~1 MB temporary during rebase — acceptable. Do not attempt in-place shifts.

8. **Summary word clearing requires checking the full bitmap word.** When clearing a bitmap bit, check if the entire bitmap word (`bitmap[widx / 64]`) is now zero before clearing the summary bit. If other bits in the same bitmap word are still set, the summary bit must remain set.

9. **parse_price ignores digits beyond position 2 after the decimal point.** Binance prices have exactly 2 decimal places for ETH/USDT (tick size $0.01), but the parser must handle strings with more digits gracefully (by ignoring them) rather than returning an error. This makes the parser robust to format changes.

10. **Static assertions are mandatory.** Every struct with a stated sizeof in this specification must have a `static_assert` immediately after its definition. If any assertion fails at compile time, the build must not proceed.

11. **`[[nodiscard]]` on every function returning `tick_t` or `bool`.** This enforces that callers check error returns from `upsert()` and `parse_price()`. Apply `[[nodiscard]]` to `upsert()`, `upsert_by_tick()`, `best_bid()`, `best_ask()`, `needs_rebase()`, `parse_price()`, `parse_qty()`, `window_index()`.

12. **No STL containers on any path.** No `std::unordered_map`, `std::map`, `std::vector`, `std::list`. The standard library is permitted for test harnesses only. The hot path is exclusively flat arrays, bitmap words, and scalar comparisons.

13. **`fno-exceptions` build flag.** `new` in the constructor will call `std::terminate` on failure rather than throw. Document this in the constructor's comment. Callers in production must ensure sufficient process memory before constructing `Book`.

14. **`upsert_impl<IsBid>` is a private template method.** The public `upsert()` dispatches to `upsert_impl<true>` (BID) or `upsert_impl<false>` (ASK). The template parameter removes a branch on the hot path. The compiler must instantiate both. agentCPP should verify with `-S` output that the dispatch is a direct call (not a function pointer indirect) after inlining at `-O2`.
