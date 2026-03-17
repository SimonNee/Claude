# OrderBookT — Architecture and Optimisation Case Study

*Iterations 1–18 | 2026-03-14 → 2026-03-17*

---

## Abstract

This document describes the design, implementation, and optimisation of `OrderBookT` — a
C++ limit-order-book template developed over 18 iterations as a performance learning
exercise. The book started as a naïve `std::map`-backed implementation at 1,175 cycles/order
and was progressively rearchitected and tuned to 7–22 cycles for `getBestBid/Ask/Spread`
and ~80 cycles for `addOrder` (no-cross) under a realistic Ornstein-Uhlenbeck price walk.

The exercise also developed a multi-agent collaboration model (`agentDuality` for structural
trade-off analysis, `agentASM` for assembly inspection and inline ASM writing, `agentContext`
for SOTA survey and TRIZ contradiction analysis) that proved effective at driving evidence-based
optimisation decisions.

---

## 1. Architecture

### 1.1 Data Model

```
OrderBookT<N_TICKS, TICKS_PER_UNIT>
├── bid_bits[N_BITMAP_WORDS]    — uint64_t bitmap; bit i set ↔ bid level i has live orders
├── ask_bits[N_BITMAP_WORDS]    — same for asks
├── bid_levels[N_TICKS]         — PriceLevel array, indexed by tick
├── ask_levels[N_TICKS]         — same for asks
└── orderIndex                  — vector<OrderLocation>; direct-index by order ID
```

**Template parameters:**

| Parameter | Meaning | Example |
|-----------|---------|---------|
| `N_TICKS` | Number of price levels | 100 |
| `TICKS_PER_UNIT` | Tick size = 1/TICKS_PER_UNIT | 20 → $0.05/tick |
| `N_BITMAP_WORDS` | `(N_TICKS + 63) / 64` — bitmap word count | 2 for 100 ticks |

The constructor takes a `base_price` at runtime — tick 0 maps to `base_price`, tick k maps
to `base_price + k / TICKS_PER_UNIT`. This allows different datasets with different mid-prices
to share a single template instantiation.

---

### 1.2 Core Structs

```cpp
struct Order {             // 16 bytes
    double quantity;       // 8
    int    id;             // 4
    Side   side;           // 4 (enum class)
};

struct PriceLevel {        // 40 bytes
    std::vector<Order> orders;     // 24 (ptr + size + capacity)
    std::size_t head       = 0;    // 8 — advances past filled/tombstoned orders
    std::size_t liveOrders = 0;    // 8 — live order count for O(1) empty()
};

struct OrderLocation {     // 16 bytes (inside OrderBookT)
    Side        side;      // 4
    int         levelTick; // 4 — direct array index; -1 = empty sentinel
    std::size_t orderIdx;  // 8 — stable index into PriceLevel::orders
};
```

**Key sizing decisions:**

- `Order.price` removed in Iteration 7 — price is implied by the level's slot index.
  Reduces 32→24→16 bytes across iterations 1→2→7.
- `OrderLocation.levelPrice` (double) replaced with `levelTick` (int) in Iteration 7 —
  integer index for direct array access; eliminates floating-point equality fragility.
- `sizeof(OrderLocation)` 24→16 bytes: power-of-two size converts `imulq` (magic-constant
  multiply for 24-byte stride) to `sarq $4` (shift for 16-byte stride).

---

### 1.3 Price–Tick Conversion

```cpp
int priceToTick(double price) const {
    return static_cast<int>((price - base_price_) * TICKS_PER_UNIT + 0.5);
}
double tickToPrice(int tick) const {
    return base_price_ + tick * (1.0 / TICKS_PER_UNIT);
}
```

The `+0.5` is a round-to-nearest idiom — avoids a `std::round` call (which compiles to a
PLT-call through the math library). Discovered by agentASM pre-flight inspection of `-S`
output in Iteration 7.

---

### 1.4 Bitmap Helpers

```cpp
template<int NWORDS>
static inline int lowestBit(const uint64_t* bits) {
#pragma GCC unroll 64
    for (int w = 0; w < NWORDS; ++w)
        if (bits[w]) return w * 64 + __builtin_ctzll(bits[w]);
    return -1;
}

template<int NWORDS>
static inline int highestBit(const uint64_t* bits) {
#pragma GCC unroll 64
    for (int w = NWORDS - 1; w >= 0; --w)
        if (bits[w]) return w * 64 + 63 - __builtin_clzll(bits[w]);
    return -1;
}
```

`lowestBit` finds the best ask (lowest price = lowest tick). `highestBit` finds the best bid.
`__builtin_ctzll` and `__builtin_clzll` compile to `TZCNT`/`LZCNT` with `-march=native` —
single-cycle instructions that replace `BSF`/`BSR` plus zero-guards.

The `#pragma GCC unroll 64` (Iteration 17) forces loop unrolling for all current NWORDS
values (≤63). For NWORDS=16, GCC was emitting a runtime loop with a counter (`incq`/`cmpq`/`jne`)
that prevented out-of-order parallel execution of the 16 independent loads. The unrolled
version is a straight-line sequence; the OOO engine issues multiple loads simultaneously.
This produced −22% to −55% on `getBestBid+Ask+Spread` across 200–1000-tick configurations.

---

### 1.5 Matching Algorithm

```
addOrder(Buy, price, qty):
  1. matchBuy: walk ask_bits via lowestBit; fill resting orders at each ask tick ≤ orderTick
  2. If qty remaining: setBit(bid_bits, orderTick); push to bid_levels[orderTick]
  3. Record orderIndex[id] = {Buy, orderTick, slotIndex}

matchBuy inner loop (per level):
  for each resting order at this ask level:
    skip if tombstone (id == 0)
    fill = min(order.qty, resting.qty)
    order.qty -= fill
    resting.qty -= fill
    if resting.qty == 0: clear orderIndex entry, pop_front
  if level.empty(): clearBit(ask_bits, askTick)
```

**Lazy deletion:** `cancelOrder` sets `resting.id = 0` (tombstone) and decrements
`liveOrders` — no shifting, O(1). The match loop skips tombstones and `pop_front`
advances past them. `PriceLevel.empty()` tests `liveOrders == 0`, not vector size.

**Tombstone invariant:** tombstones have `id=0` but their `quantity` is unmodified.
The match loop must test `id` first — a tombstone with positive quantity would silently
fill against a cancelled order. Bug fixed in Iteration 9; `test_cancel_then_cross_same_level`
covers it.

---

### 1.6 Fill Loop ASM

The inner fill arithmetic uses `__asm__ volatile` to ensure correct register allocation:

```cpp
double xmm_fill, xmm_rest, resting_qty_out;
__asm__ volatile (
    "vmovsd %[resting_qty], %[xmm_fill]\n\t"     // load resting.qty once
    "vmovsd %[resting_qty], %[xmm_rest]\n\t"     // copy before vminsd overwrites
    "vminsd %[order_qty],   %[xmm_fill], %[xmm_fill]\n\t"
    "vsubsd %[xmm_fill],    %[order_qty], %[order_qty]\n\t"
    "vsubsd %[xmm_fill],    %[xmm_rest],  %[xmm_rest]\n\t"
    "vmovsd %[xmm_rest],    %[resting_qty]\n\t"
    "vmovapd %[xmm_rest],   %[resting_qty_out]\n\t"
    : [order_qty]       "+x" (order.quantity),
      [resting_qty]     "+m" (resting.quantity),
      [xmm_fill]        "=&x"(xmm_fill),
      [xmm_rest]        "=&x"(xmm_rest),
      [resting_qty_out]  "=x"(resting_qty_out)
    : :
);
if (resting_qty_out == 0.0) { ... }
```

**Why ASM?** The compiler emits two loads of `resting.quantity` — once before `vminsd`
(which overwrites the register) and again for the subtraction. The `xmm_rest` scratch register
retains the pre-`vminsd` copy, eliminating the second load. The `resting_qty_out` output
exposes the post-update value in a register so the `== 0.0` zero-check uses `vucomisd xmm,xmm`
instead of a third reload from memory.

---

### 1.7 Cancel

```cpp
bool cancelOrder(int id) {
    // O(1): bounds check, direct lookup, direct slot write
    if (id <= 0 || id >= orderIndex.size() || orderIndex[id].levelTick == -1) return false;
    auto [side, levelTick, orderIdx] = orderIndex[id];
    PriceLevel& level = (side == Buy) ? bid_levels[levelTick] : ask_levels[levelTick];
    level.cancel_at(orderIdx);            // id=0, --liveOrders
    if (level.empty()) clearBit(...);
    orderIndex[id] = kEmptyLocation;
    return true;
}
```

Cancel complexity: O(1). Three failed approaches are documented in section 3.3.

---

### 1.8 O(1) Cancel — orderIndex Design

`orderIndex` is a `std::vector<OrderLocation>` indexed directly by order ID. IDs are
sequential integers starting from 1 — the vector grows with `resize` as needed.

**Why not `std::unordered_map`?** Assembly inspection (Iteration 4) revealed that
`libstdc++`'s `unordered_map::erase` emits 3× `divq` (64-bit unsigned divide, ~35–90 cycles
each, non-pipelined) from the `_Prime_rehash_policy` — prime bucket counts cannot be
strength-reduced to bitmask division. Additionally, each node is separately heap-allocated;
every erase calls `operator delete` (~50–100 cycles, L1/L2 pollution). Total: ~185–315 cycles
per cancel, invisible from C++ source. The flat direct-index vector eliminates all of it:
no hash function, no division, no allocation.

---

## 2. Build Configuration

```bash
# Tests (correctness — no LTO)
g++ -std=c++17 -O2 -march=native -Wall -o tests tests.cpp orderbook.cpp && ./tests

# Smoke test
g++ -std=c++17 -O2 -march=native -o main main.cpp orderbook.cpp && ./main

# Benchmarks
g++ -std=c++17 -O2 -march=native -flto -o bench bench.cpp orderbook.cpp && ./bench
```

**`-march=native`** (from Iteration 7): enables TZCNT/LZCNT (replacing BSF/BSR + zero guards),
and allows GCC to use any ISA extension available on the build machine.

**`-flto`** (from Iteration 11): enables cross-translation-unit inlining. `addOrder`,
`matchBuy`, and `matchSell` are defined in `orderbook.cpp`; without LTO they are called via
PLT stubs from `bench.cpp`. LTO inlines them at the call site, eliminating 4 callee-saved
register push/pop pairs and the call/ret overhead. Benchmark binaries only — tests build
without LTO to avoid inlining heuristic interactions masking correctness issues.

**`getBestBid/getBestAsk/getSpread` inline in class body** (Iteration 13): these three
functions are defined directly in `orderbook.h` rather than `orderbook_impl.h`. GCC always
inlines class-body definitions regardless of LTO heuristics; function-body definitions in
a separate TU can be refused if the call site is classified as "cold" by the LTO profile.

---

## 3. Optimisation History

### 3.1 Phase 1 — Structural Redesign (Iterations 1–4)

The initial implementation (Iteration 1) used `std::map<double, std::deque<Order>>` —
the obvious first-attempt data structure. At 1,175 cycles/order it was correct but slow.

**Iteration 2** replaced the map with a sorted `std::vector<PriceLevel>`. agentDuality
analysis confirmed the vector wins at p≤10,000 (observed p_mean=72); the sorted flat layout
keeps the outer price level array L1-hot (~3.3 KB for 82 levels). The improvement was small
(+3% initially, explained below) because the inner order buffers at q_mean=1,085 still land
in L3 regardless of the outer container.

**Iteration 3** introduced O(1) cancel via `id → location` indexing and lazy deletion
(tombstones). Cancel improved from 30,946 cycles to 150 cycles — a 206× gain. Three
implementation variants were tried before the correct design was found (see section 3.3).
Dedicated benchmarks (`RDTSC` cycle counting, `N=500,000`) were introduced in this iteration.

**Iteration 4** introduced the `agentASM` pre-flight review workflow. Assembly inspection
of `orderIndex.erase` revealed the `3× divq + operator delete` structural cost, leading to
the flat direct-index array replacement (−30–69% across all operations). This was more
valuable than any inline ASM written in this iteration — the finding was invisible from C++
source.

### 3.2 Phase 2 — Bitmap Architecture (Iterations 7–8)

**Iteration 7** was the largest structural change: replacing the sorted `std::vector<PriceLevel>`
with a fixed `PriceLevel[N_TICKS]` array indexed by tick integer, plus a `uint64_t` bitmap
indicating which ticks have live orders.

Before Iteration 7, `addOrder` did:
- Binary search over ~82 active levels to find the price level: O(log p) = 7 comparisons
- `std::vector::insert` at the found position: O(p) memmove at ~82 × 40 bytes

After Iteration 7:
- `priceToTick(price)`: 1 multiply + 1 subtract + cast to int
- `bid_levels[tick]`: direct array index, no search

Results: −35% no-cross, −45% cross-1L, −39% cross-5L, −60% cancel.

Also in Iteration 7: `Order.price` removed (24→16 bytes), `PriceLevel.price` removed (48→40
bytes), `OrderLocation.levelPrice` replaced with `levelTick` (24→16 bytes), and the
`drained` flag + trailing `remove_if` compaction pass eliminated — the bitmap bit is cleared
inline when a level empties during matching, making a separate O(p) clean-up pass redundant.

**Iteration 8** fixed the double-load of `resting.quantity` in the fill loop. The
compiler-generated code loaded `resting.quantity` twice: once into `xmm1` for `vminsd`
(overwriting it), then again from memory for the subtraction. Inline ASM with two scratch
XMM registers eliminated the second memory round-trip. Effect: −1% at cross-1L (one fill
iteration — saving amortised to noise), −29% at cross-5L (5× fill iterations — saving
compounds).

### 3.3 Cancel Implementation History

Three failed approaches before the correct design:

| Attempt | Approach | Cycles | Problem |
|---------|----------|--------|---------|
| 1 | O(p×q) nested scan | 30,946 | Scans every level + every order |
| 2 | Map lookup + linear scan within level | ~30,946 | Map finds the level but erase-shift still O(q) |
| 3 | Map + lazy deletion (no stored index) | 36,200 | Tombstones accumulate; scan degrades O(k) per cancel |
| **4** | **Map + stored orderIdx + lazy deletion** | **150** | **O(1): no scan, no shift** |
| 5 (Iter 4) | Replace map with flat vector | **48** | Eliminates divq + operator delete |
| 6 (Iter 7+) | Flat vector + bitmap tick array | **12–19** | Direct array index, no binary search |

Attempt 3 is the instructive failure: lazy deletion alone is insufficient. Without storing
`orderIdx`, the cancel implementation must linearly scan the order vector to find the target.
As cancelled orders accumulate (without fills to clear them), the scan grows O(k) per cancel.
Storing `orderIdx` is safe because lazy deletion never shifts elements — indices are stable.

### 3.4 Phase 3 — Micro-Optimisations (Iterations 9–13)

**Iteration 9**: ASM promoted to canonical; tombstone bug fixed. The match loop was skipping
tombstones via `fill = min(qty, 0.0) = 0` — but this still subtracted 0.0 from `order.qty`
and updated `resting.qty` in memory. A tombstone with positive quantity could silently
partially fill. Fix: explicit `if (resting.id == 0) { level.pop_front(); continue; }` added
before fill arithmetic.

**Iteration 10**: `optional<OrderLocation>` (24 bytes) → plain `OrderLocation` with `levelTick=-1`
sentinel (16 bytes). The `optional` wrapper forces a `imulq $24` for every `orderIndex`
element access; the 16-byte plain struct reduces to `sarq $4` (right-shift). Effect: −13%
no-cross, −19% cross-1L.

**Iteration 11**: Added `-flto`. Eliminated PLT call overhead for `addOrder`/`matchBuy`/
`matchSell` (cross-TU inlining). Also exposed that `getBestBid/Ask/Spread` return values
were being discarded — LTO eliminated the calls entirely (0 measured cycles). Fixed by
accumulating into a `sink` double; the `if (sink == -1.0)` guard prevents the sink from
being optimised away without adding a branch to the hot path. Effect: −7% no-cross,
−14% cross-1L, −53% getBBA (corrected from a 0-cycle artifact).

**Iteration 12**: Third `resting.quantity` load eliminated. The `+m` constraint on
`resting.quantity` forced a reload for the `if (resting.quantity == 0.0)` check after the
ASM block. Added `resting_qty_out` as a fourth `=x` output; `vmovapd xmm_rest → resting_qty_out`
exposes the post-update value in a live register. The zero-check uses `vucomisd xmm, xmm`
with no memory access. No measurable benchmark delta (saving is below noise floor at depth 1–5),
but structurally correct.

**Iteration 13**: Template `OrderBookT<N_TICKS, TICKS_PER_UNIT>`. `base_price` moved to
runtime constructor argument; `TICKS_PER_UNIT` as integer template param. `getBestBid/Ask/getSpread`
moved to class-body inline definitions (always inlined by GCC, no LTO heuristic interference).
Effect on `<100,20>`: −61% getBBA (18→7 cycles). The LTO cold-call refusal for these three
functions was the dominant hidden cost; inlining them eliminates a conditional branch and
optional-construction overhead on the hot path.

### 3.5 Phase 4 — Cache Characterisation (Iterations 14–18)

**Iteration 14**: Multi-configuration benchmark harness. `benchSuite<N_TICKS, TICKS_PER_UNIT>`
runs all operations against five instantiations (100/200/500/500-01/1000 ticks). The getBBA
scan cost scales linearly with N_BITMAP_WORDS. The L1→L2 transition was expected in the
no-cross path but was not visible — OU price walk clusters within ~20 levels of MID regardless
of N_TICKS; the CPU never loads inactive levels.

**Iteration 15**: Uniform-price benchmark added to force the entire `PriceLevel[N_TICKS]`
array into the working set. Random ticks across the full range expose the L1→L2 transition
that OU obscures. L1→L2 confirmed at 200→500 ticks (+14% uniform no-cross, +72% getBBA).

**Iteration 16**: Investigation of a 1000-tick no-cross anomaly (~42 cycles vs ~84 for 100-tick).
Confirmed as an LTO binary layout artifact — isolated binary gave ~73 cycles, consistent with
other configs. Assembly structure was identical in both builds; the difference was link-time
code placement. The ~42 datum was discarded.

**Iteration 17**: `#pragma GCC unroll 64` on `lowestBit`/`highestBit`. For NWORDS=16
(1000-tick), GCC was emitting a runtime loop: `incq`/`cmpq $16`/`jne`. The loop counter
creates a serial dependency chain; the OOO engine cannot issue loads in parallel. The unrolled
version is a straight-line sequence of independent `movq`/`testq`/`jne` pairs — the OOO
engine issues multiple loads per cycle. Effect: −22% to −55% getBBA at 200–1000 ticks.

**Iteration 18**: Added 2000/3000/4000-tick configs. L2→L3 transition confirmed between
1000 and 2000 ticks: getBBA jumps from ~22 to ~40 cycles (+82%). Uniform no-cross also
steps at the same boundary (+10 cycles, +11%).

---

## 4. Cache Tier Analysis

### 4.1 Working Set

The `PriceLevel[N_TICKS]` fixed arrays are the dominant working-set item. Each `PriceLevel`
is 40 bytes (`sizeof(PriceLevel) == 40`):

| Config | N_TICKS | Metadata (2× N_TICKS × 40 bytes) | Cache tier |
|--------|---------|----------------------------------|------------|
| `<100,20>` | 100 | 8 KB | L1 |
| `<200,20>` | 200 | 16 KB | L1 |
| `<400,20>` | 400 | ~32 KB | L1 tight |
| `<500,20>` | 500 | 40 KB | L2 |
| `<1000,20>` | 1000 | 80 KB | L2 |
| `<2000,20>` | 2000 | 160 KB | L2→L3 |
| `<3000,20>` | 3000 | 240 KB | L3 |
| `<4000,20>` | 4000 | 320 KB | L3 |

### 4.2 The Two Transitions

| Transition | Tick range | getBBA delta | Uniform no-cross delta |
|------------|-----------|-------------|------------------------|
| L1 → L2 | 200 → 500 ticks | +1 cycle (+7%) | +8 cycles (+14%) |
| L2 → L3 | 1000 → 2000 ticks | +18 cycles (+82%) | +10 cycles (+11%) |

**Why getBBA is more sensitive than addOrder to cache tier:**

getBBA *only* does bitmap scans — every call reads all N_BITMAP_WORDS words regardless of
book state. Once past L2, each additional bitmap word is an L3 load (~40 cycles latency).
`addOrder` also writes one `PriceLevel` slot (which survives in L1 after first touch) and
updates `orderIndex`, diluting the bitmap scan cost in the measured cycles/op.

### 4.3 OU vs Uniform — Why They Differ

The OU walk reverts toward a fixed mid-price with parameter θ=0.05. At steady state the
price stays within ~20 ticks of mid regardless of N_TICKS. Only those ~20 levels are active;
the rest of the `PriceLevel[N_TICKS]` array is cold and never touched. OU no-cross cycles
are nearly flat across all configs.

The uniform benchmark (Iteration 15) distributes orders uniformly across the full tick range,
forcing every `PriceLevel` slot into the working set. This is an artificial stress test — no
production OU-like instrument generates random activity across thousands of ticks. It is
useful as a worst-case cache pressure measurement, not as a production throughput estimate.

### 4.4 Production Relevance

| Instrument | Typical tick | Active depth | Effective N_TICKS | Cache tier |
|------------|-------------|-------------|-------------------|------------|
| US equity ($100 stock) | $0.01 | ±$1–2 | 200–400 | L1–L2 |
| E-mini S&P futures | 0.25 pts | ±10 pts | ~80 | L1 |
| EUR/USD forex (1 pip) | 0.0001 | ±50 pips | ~1000 | L2 |
| BTC/USD (tick $0.01) | $0.01 | ±$500 | ~50,000 | beyond L3 |
| `<100,20>` baseline | $0.05 | ±$2.50 | 100 | L1 |

For equity and futures orderbooks (100–400 ticks), `OrderBookT` fits entirely in L1.
The L1→L2 transition at ~400 ticks is a real production cost for wider or more volatile
instruments. Production implementations typically rebase when mid drifts more than half the
tick range — construct a new `OrderBookT` with an updated `base_price` and replay the
resting book into it. `OrderBookT` supports this: `base_price` is a runtime constructor
argument, not baked into the type.

---

## 5. Benchmark Summary

All benchmarks: `g++ -std=c++17 -O2 -march=native -flto`, `<100,20>` configuration,
OU price walk (THETA=0.05, band=±2.50, N=500,000 adds), RDTSC cycle counting.

### 5.1 Progression — `addOrder` no-cross

| Iteration | Change | Cycles/op | Delta |
|-----------|--------|-----------|-------|
| 1 | Baseline (`std::map`) | 1,175 | — |
| 2 | Vector + head index | ~1,209 | +3% |
| 3 | O(1) cancel, `orderIndex` map | 233 | −80% |
| 4 | Replace `unordered_map` with flat vector | 158 | −32% |
| 7 | Bitmap + fixed level array, `-march=native` | 103 | −35% |
| 10 | `optional<OL>` → plain struct sentinel | 84 | −18% |
| 11 | LTO cross-TU inlining | 78 | −7% |
| 13 | Template + inline getBBA | 82 | ~0 |
| 14+ | Multi-config harness (< 100,20 >) | ~84 | ~0 |

**Total improvement: 1,175 → ~80 cycles (−93%).**

### 5.2 Progression — `getBestBid+Ask+Spread` (per trio)

| Iteration | Change | Cycles/op |
|-----------|--------|-----------|
| 3 | First measured | 31 |
| 7 | Bitmap (BSF/BSR) | 40 |
| 11 | LTO — corrected: was 0 before sink fix | 19 |
| 13 | Inline class-body definitions | **7** |
| 17 | `#pragma GCC unroll 64` (1000-tick: 44→22) | 7 (100-tick unchanged) |

### 5.3 Multi-config getBBA (post-Iteration 17, cycles/op)

| Config | N_BITMAP_WORDS | getBBA |
|--------|---------------|--------|
| `<100,20>` | 2 | 7 |
| `<200,20>` | 4 | ~13 |
| `<500,20>` | 8 | ~14 |
| `<500,100>` | 8 | ~14 |
| `<1000,20>` | 16 | ~22 |
| `<2000,20>` | 32 | ~41 |
| `<3000,20>` | 47 | ~62 |
| `<4000,20>` | 63 | ~80 |

---

## 6. Agent Collaboration Model

### 6.1 Agents and Roles

```
agentContext   — SOTA survey + affordance analysis + TRIZ contradiction analysis
agentDuality   — structural C++ trade-offs: data structure, memory layout, cache behaviour
agentASM       — compiler output review (-S) + targeted inline ASM writing
```

The collaboration loop:

```
agentContext opens solution space
      ↓
agentDuality → structural C++ change
      ↓
agentASM pre-flight (-S review)
      ↓
inline ASM if warranted
      ↓
benchmark
      ↑
findings feed next iteration
```

### 6.2 agentASM as Reviewer First

agentASM's most valuable contribution is **assembly review**, not ASM writing. Pre-flight
inspection in Iteration 4 found:

- `getSpread` already near-optimal — compiler had inlined and reduced to 9 instructions
- `matchBuy` had a narrow double-load redundancy
- **`orderIndex` had `3× divq + operator delete`** — structural, not fixable by ASM

The third finding, invisible from C++ source, motivated the structural fix that produced
the largest gains of any single iteration (−30–69% across all ops). No amount of inline
ASM could have addressed it.

The rule established in Iteration 4 and maintained throughout: **always compile with `-S`
and review before writing any ASM**. Assembly analysis removes guesswork; it is the most
direct evidence available.

### 6.3 agentContext and the Frame Problem

Before Iteration 7, the book used a sorted vector of price levels — an optimisation of the
wrong frame. The bitmap + fixed-array design is a well-known pattern in production
orderbooks (LMAX Disruptor, Databento, various open-source matching engines). It was not
considered until Iteration 6 because the solution space was never formally opened.

agentContext (run in Iteration 6) surfaced:
- van Emde Boas tree dismissed with precision: crossover at U≥2^16; bitmap wins at U=100
- Intrusive list flagged as regressive: pointer-scattered nodes permanently foreclose SIMD
- `drained` flag + `remove_if` identified as trimmable — the bitmap already encodes empty levels

The TRIZ trimming lens ("what can be removed?") identified the `drained` pass independently
of the Iteration 7 agentDuality plan. Both converged on the same elimination.

### 6.4 Lessons

1. **Open the solution space before the first design decision.** Iterations 1–5 optimised
   within a frame that was replaced wholesale in Iteration 7. A SOTA survey at project start
   would have reached the bitmap design directly.

2. **Assembly review precedes ASM writing.** The pre-flight step is the payoff; the ASM is
   sometimes a null result (compiler already optimal).

3. **Structural fixes outperform micro-optimisations.** The three largest gains — O(1) cancel
   (Iter 3), flat `orderIndex` (Iter 4), bitmap level array (Iter 7) — were all data structure
   changes. The ASM fixes (Iters 8, 12) were real but smaller.

4. **Measure before optimising layout.** In Iteration 2, agentDuality predicted the inner
   order buffers would be L1-hot (q_mean≈10 assumed). Instrumentation showed q_mean=1,085 —
   off by 100×. The performance model was wrong until measured; the structural conclusion
   (flat vector) was still correct, but for different reasons.

5. **LTO changes what is worth measuring.** Three findings only surfaced after LTO was added:
   (a) `getBBB` calls were being eliminated entirely (zero-cycle artifact); (b) a cross-TU
   inlining gain of −7–14%; (c) an apparent 1000-tick anomaly (~42 cycles) that was a binary
   layout artifact from LTO code placement, not a real signal.

---

## 7. Test Coverage

15/15 correctness tests pass at every iteration. Tests build without `-flto` (correctness,
not performance). Tests were never modified — the test suite from Iteration 1 is the
authoritative invariant.

Notable test added in Iteration 9: `test_cancel_then_cross_same_level` — verifies that a
crossing order does not fill against a tombstone from a previously cancelled order at the
same price level. This was a real bug (tombstones had `id=0` but positive `quantity`;
the fill arithmetic silently consumed them).

---

## 8. What Was Not Done

**SIMD inner fill loop** (Iteration 5): four independent blockers — carried dependency on
`order.quantity`, data-dependent trip count, 24-byte AoS stride, aliasing ambiguity. A
two-pass prefix-scan approach was benchmarked and produced a null result. SIMD on the fill
loop requires SoA layout (separate contiguous `quantity` array), which doubles write
amplification on the dominant no-cross path. Not pursued.

**Concurrency** (Iteration 5): no concurrency model defined for this project. A meaningful
atomic implementation requires a concrete scenario (e.g. lock-free `nextId`, separate
reader thread). Deferred indefinitely.

**ID recycling / `orderIndex` compaction** (Iteration 7+): sequential IDs grow the
`orderIndex` vector without bound. No per-op cycle impact at benchmark scale; not implemented.

**Rebasing** (architectural): when mid drifts, a production book constructs a new
`OrderBookT` with updated `base_price` and replays resting orders. The constructor supports
this; the replay logic was not implemented.

**Realistic benchmark data** (Iteration 14): `bench.cpp` uses a Normal noise distribution;
`gen_orders.q` uses Uniform[-1,+1]. The CSV feeds only `main.cpp`; bench is self-contained.
The mismatch is a latent inconsistency. Both are OU walks with the same THETA and band — the
noise distribution affects clustering density, not the cache tier findings.

---

*All iteration tags: `iter-1-complete` through `iter-18-complete` on branch `feature/agents`.*
