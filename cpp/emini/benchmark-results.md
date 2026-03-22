# E-mini Orderbook — Benchmark Results

## Hardware & Build

| Field | Value |
|---|---|
| CPU | Intel Core i9-10980HK @ 2.40 GHz |
| L1d / L1i | 32 KB / 32 KB |
| L2 | 256 KB (unified) |
| L3 | 16 MB |
| Compiler | GCC 12.2.0 (Debian) |
| C flags | `-std=c11 -O2 -march=native -flto` |
| C++ flags | `-std=c++17 -O2 -march=native -flto -fno-exceptions` |
| Core pinned | Core 2 (`taskset -c 2`) |
| `isolcpus` | **Not set** — results may include minor OS noise |
| Sanitizers | None (bench build) |
| Cache regime | Warm |
| Iterations | 500,000 per benchmark |

---

## Run 1 — 2026-03-21

### sizeof(Book::Impl) = 16,283,824 bytes

Derivation: 2 × 141,904 (sides) + 16,000,004 (arena) + 4 (pad) + 8 (base_price) = 16,283,824

---

### B1 — Add Latency

| Variant | C median | C p99 | C max | C++ median | C++ p99 | C++ max |
|---|---|---|---|---|---|---|
| Single level (tick=100) | 42 cy | 50 cy | 109,818 cy | 44 cy | 60 cy | 115,144 cy |
| Multi-level (100 ticks) | 48 cy | 54 cy | 1,330 cy | 42 cy | 53 cy | 124,283 cy |

Spec target: <50 cy median. Both implementations at or within target.

---

### B2 — Cancel Latency by Queue Depth

| q | position | C median | C p99 | C++ median | C++ p99 |
|---|---|---|---|---|---|
| 1 | head | 29 cy | 32 cy | 28 cy | 35 cy |
| 1 | mid | 30 cy | 33 cy | 28 cy | 34 cy |
| 1 | tail | 29 cy | 33 cy | 27 cy | 31 cy |
| 5 | head | 28 cy | 31 cy | 26 cy | 29 cy |
| 5 | mid | 38 cy | 45 cy | 34 cy | 36 cy |
| 5 | tail | 48 cy | 52 cy | 42 cy | 45 cy |
| **10** | **head** | **28 cy** | **31 cy** | **27 cy** | **30 cy** |
| **10** | **mid** | **52 cy** | **55 cy** | **46 cy** | **49 cy** |
| **10** | **tail** | **70 cy** | **77 cy** | **64 cy** | **69 cy** |
| 50 | head | 28 cy | 31 cy | 26 cy | 29 cy |
| 50 | mid | 138 cy | 145 cy | 133 cy | 137 cy |
| 50 | tail | 244 cy | 260 cy | 238 cy | 250 cy |

#### Doubly-Linked Promotion Decision (threshold: 50 cy at q=10 mid, warm)

| Implementation | q=10 mid median | vs. threshold | Verdict |
|---|---|---|---|
| C | 52 cy | +2 cy OVER | Promotion review warranted — DEFERRED (`isolcpus` retest first) |
| C++ | 46 cy | -4 cy UNDER | Singly-linked adequate |

**Note**: `isolcpus` was not active during this run. The 2-cycle margin in C is within OS noise range. Retest with `isolcpus=2` before acting on the promotion path.

---

### B3 — Match Latency (Single Level)

| Implementation | median | p99 |
|---|---|---|
| C | 100 cy | 105 cy |
| C++ | 40 cy | 45 cy |

Note: C result includes full `matcher_execute` → bitmap scan → level drain path on every call. C++ result more closely matches spec target of 15–40 cy.

---

### B4 — Match Latency (Multi-Level, k resting orders consumed)

| k | C median | C p99 | C++ median | C++ p99 |
|---|---|---|---|---|
| 1 | 105 cy | 111 cy | 47 cy | 52 cy |
| 5 | 160 cy | 193 cy | 105 cy | 145 cy |
| 10 | 230 cy | 268 cy | 175 cy | 211 cy |

O(k) scaling confirmed in both implementations.

---

### B5 — best_bid Scan (N active levels)

| N | C median | C p99 | C++ median | C++ p99 |
|---|---|---|---|---|
| 1 | 204 cy | 393 cy | 107 cy | 109 cy |
| 10 | 22 cy | 26 cy | 22 cy | 24 cy |
| 50 | 22 cy | 25 cy | 22 cy | 25 cy |
| 138 | 22 cy | 25 cy | 22 cy | 24 cy |

Bitmap is L1-resident as expected (22 cy flat at N≥10). N=1 outlier: single bit at high tick forces scan through all 138 words. C N=1 higher than C++ (204 cy vs 107 cy) — different codegen for bitmap scan direction.

---

## Correctness (Run 1)

| | C | C++ |
|---|---|---|
| Tests | 32/32 PASS | 33/33 PASS |
| ASAN | Clean | Clean |
| UBSAN | Clean | Clean |

---

---

## Run 2 — 2026-03-21 (C only — after bitmap inlining + fill_result_t fix)

### Changes from Run 1

- **Fix 1**: `bitmap_best_ask` / `bitmap_best_bid` moved to `bitmap.h` as `static inline` — now inlined into `matcher_execute` instead of PLT calls
- **Fix 2**: `matcher_execute` changed to `void` with `fill_result_t *out` output parameter — eliminates 1032-byte `memcpy` on every return

C++ implementation unchanged.

---

### B1 — Add Latency (C, unchanged)

| Variant | C median | C p99 |
|---|---|---|
| Single level (tick=100) | 42 cy | 47 cy |
| Multi-level (100 ticks) | 48 cy | 52 cy |

No change expected or observed.

---

### B2 — Cancel Latency (C, unchanged)

| q | position | C median | C p99 |
|---|---|---|---|
| 1 | head | 30 cy | 34 cy |
| 1 | mid | 30 cy | 34 cy |
| 1 | tail | 30 cy | 33 cy |
| 5 | head | 28 cy | 31 cy |
| 5 | mid | 38 cy | 44 cy |
| 5 | tail | 48 cy | 52 cy |
| **10** | **head** | **29 cy** | **32 cy** |
| **10** | **mid** | **53 cy** | **57 cy** |
| **10** | **tail** | **70 cy** | **75 cy** |
| 50 | head | 29 cy | 32 cy |
| 50 | mid | 139 cy | 151 cy |
| 50 | tail | 244 cy | 266 cy |

Doubly-linked decision unchanged — still deferred pending `isolcpus` retest.

---

### B3 — Match Latency (Single Level)

| Implementation | Run 1 median | Run 2 median | Delta | p99 |
|---|---|---|---|---|
| C | 100 cy | **73 cy** | **−27 cy** | 81 cy |
| C++ | 40 cy | 40 cy | — | 45 cy |

Gap closed from 60 cy to 33 cy.

---

### B4 — Match Latency (Multi-Level)

| k | C Run 1 | C Run 2 | Delta | C++ (Run 1) |
|---|---|---|---|---|
| 1 | 105 cy | **74 cy** | **−31 cy** | 47 cy |
| 5 | 160 cy | **141 cy** | **−19 cy** | 105 cy |
| 10 | 230 cy | **209 cy** | **−21 cy** | 175 cy |

O(k) scaling confirmed. Remaining gap (~33 cy) attributed to C++ fully-unrolled bitmap scan winning on sparse books.

---

### B5 — best_bid Scan (unchanged)

| N | C median | C p99 |
|---|---|---|
| 1 | 205 cy | 250 cy |
| 10 | 24 cy | 26 cy |
| 50 | 22 cy | 26 cy |
| 138 | 22 cy | 26 cy |

---

---

## Run 3 — 2026-03-21 (C++ only — bitmap unroll pragma removed)

### Changes from Run 1

- `#pragma GCC unroll 64` removed from `bitmap_lowest<138>` and `bitmap_highest<138>` in `book.hpp`
- Plain counted loops replacing 400+ instruction fully-unrolled bodies
- C implementation unchanged from Run 2

**Rationale**: agentDuality verdict — E-mini trades in ±200-tick window (4 bitmap words). Early-exit dominates; full 138-word scan only on empty side. Full unroll imposed 20× I-cache footprint for zero common-case benefit.

---

### B1 — Add Latency (C++ unchanged)

| Variant | C++ median | C++ p99 |
|---|---|---|
| Single level (tick=100) | 42 cy | 48 cy |
| Multi-level (100 ticks) | 42 cy | 48 cy |

---

### B2 — Cancel Latency (C++ only)

| q | position | C++ Run 1 | C++ Run 3 | Delta |
|---|---|---|---|---|
| 1 | head | 28 cy | 28 cy | — |
| 1 | mid | 28 cy | 28 cy | — |
| 1 | tail | 27 cy | 28 cy | — |
| 5 | head | 26 cy | 26 cy | — |
| 5 | mid | 34 cy | 34 cy | — |
| 5 | tail | 42 cy | 44 cy | — |
| **10** | **head** | **27 cy** | **28 cy** | — |
| **10** | **mid** | **46 cy** | **47 cy** | — |
| **10** | **tail** | **64 cy** | **65 cy** | — |
| 50 | head | 26 cy | 28 cy | — |
| 50 | mid | 133 cy | 133 cy | — |
| 50 | tail | 238 cy | 238 cy | — |

C++ singly-linked verdict unchanged: 47 cy at q=10 mid — below 50 cy threshold.

---

### B3 — Match Latency (Single Level)

| Implementation | Run 1 | Run 3 | Delta |
|---|---|---|---|
| C++ | 40 cy | 42 cy | +2 cy (noise) |
| C (Run 2) | 73 cy | — | — |

---

### B4 — Match Latency (Multi-Level)

| k | C++ Run 1 | C++ Run 3 | Delta |
|---|---|---|---|
| 1 | 47 cy | 49 cy | +2 cy (noise) |
| 5 | 105 cy | 102 cy | −3 cy (noise) |
| 10 | 175 cy | 174 cy | −1 cy (noise) |

Common path unaffected. agentDuality verdict confirmed.

---

### B5 — best_bid Scan

| N | C++ Run 1 | C++ Run 3 | Delta |
|---|---|---|---|
| 1 (full scan) | 107 cy | 123 cy | +16 cy — expected, rare path |
| 10 | 22 cy | 24 cy | noise |
| 50 | 22 cy | 24 cy | noise |
| 138 | 22 cy | 24 cy | noise |

N=1 worst case (single bit at tick 8799, full 138-word scan) slower by 16 cy — unrolled path loses advantage on a complete scan. This is the rare/empty-side case; no action.

---

## Current Best Numbers (post all fixes)

| Benchmark | C | C++ |
|---|---|---|
| Add single level | 42 cy | 42 cy |
| Add multi-level | 48 cy | 42 cy |
| Cancel q=1 | 29 cy | 28 cy |
| Cancel q=10 mid | 53 cy | 47 cy |
| Match single level | 73 cy | 42 cy |
| Match k=5 | 141 cy | 102 cy |
| Match k=10 | 209 cy | 174 cy |
| best_bid (live book) | 22 cy | 24 cy |

---

---

## Run 4 — 2026-03-21 (both — data-driven, integer tick CSV)

### Changes from Run 3

- CSV column `price` (float) replaced with `tick` (plain integer, multiply by 4 in q before write)
- Loaders rewritten: tick read directly as `uint32_t` — no `price_to_tick`, no float in loader or benchmark loop
- C: `book_add_tick` / `book_match_tick` added — benchmark calls internal tick path directly
- C++: `add_by_tick` / `match_by_tick` added — same pattern
- The single sanctioned `price_to_tick` cast remains in the public API for real incoming orders only
- `BASE_PRICE = 4400.0` (tick floor) used only at `book_create` time — not in any timed path
- Benchmark now replays 1M OU-generated events: 83,333 ADD / 833,330 CANCEL / 83,337 MATCH
- Tick range in CSV: 4191–4586 (OU mid 5500 maps to tick 4400; active range ≈ ±200 ticks)

**This is the first benchmark run on realistic domain data.**

---

### B1 — Add Latency

| Implementation | median | p99 |
|---|---|---|
| C | 43 cy | 91 cy |
| C++ | 34 cy | 77 cy |

---

### B2 — Cancel Latency (realistic 10:1 distribution)

| Implementation | median | p99 |
|---|---|---|
| C | 39 cy | 64 cy |
| C++ | 22 cy | 46 cy |

Note: no queue-depth breakdown — cancel distribution comes from the OU event stream. Realistic mix of head/mid/tail cancels across the active price band.

---

### B3/B4 — Match Latency (data-driven)

| Implementation | median | p99 |
|---|---|---|
| C | 176 cy | 520 cy |
| C++ | 161 cy | 248 cy |

Gap has narrowed to 15 cy (vs 27–31 cy in synthetic runs). Realistic multi-level sweeps across OU-generated book state. High p99 on C reflects occasional deep sweeps.

---

### B5 — best_bid Scan (realistic bitmap occupancy)

| Implementation | median | p99 |
|---|---|---|
| C | 119 cy | 155 cy |
| C++ | 87 cy | 153 cy |

Significantly higher than synthetic runs (22–24 cy) — the OU book has realistic bitmap occupancy across the active price band, not a single isolated level. This is the correct number to track going forward.

---

## Current Best Numbers (Run 4 — data-driven, integer ticks)

| Benchmark | C | C++ |
|---|---|---|
| Add | 43 cy | 34 cy |
| Cancel (realistic) | 39 cy | 22 cy |
| Match (data-driven) | 176 cy | 161 cy |
| best_bid (realistic) | 119 cy | 87 cy |

---

---

## Run 7 — 2026-03-22 (C only — bitmap scan algorithm correction)

### Changes from Run 4

Two defensive boundary checks removed from `bitmap.h` to make the C algorithm
identical to the C++ `bitmap_lowest` / `bitmap_highest` templates:

- `bitmap_best_ask`: removed `if (tick < MAX_TICKS)` guard on the non-zero word path.
  Previously returned `TICK_INVALID` if the tick straddled the boundary; now returns
  the tick directly, matching C++ which also performs no in-loop bounds check.
- `bitmap_best_bid`: same change — removed `if (tick < MAX_TICKS)` guard and the
  "keep scanning" continuation path.
- Loop variable changed to `int w` (from `uint32_t w`) to enable `subq/jb` borrow-flag
  termination, matching the C++ template form.

**Why the guards were safe to remove**: the invariant that no bitmap bit above
`MAX_TICKS` can ever be set is enforced at every write path into the bitmap.
`queue_enqueue` (the only function that sets a bit) is `static` and reachable only
through `book_add` and `book_add_tick`, both of which reject `tick >= MAX_TICKS`
before calling `queue_enqueue`. The in-loop guards were defensive checks against a
condition the book's own API boundary already makes impossible.

**Root cause of the original gap** (identified by agentDuality + agentASM):
The `if (tick < MAX_TICKS)` check inside `bitmap_best_bid` created a second back-edge
in the loop body (the "keep scanning" path), forcing GCC into a two-path loop structure
with an explicit pointer walk (`subq $8, %rdi`) and a running tick-ceiling sentinel
register (`edx`), adding 2 extra instructions per word vs. the C++ form. The C++
template had no such check and compiled to a clean 5-instruction-per-word loop using
scaled-index addressing (`(%rdi,%rax,8)`) and borrow-flag termination (`subq/jb`).

---

### B5 — best_bid Scan (C, data-driven)

| Implementation | Run 4 median | Run 7 median | Delta | p99 |
|---|---|---|---|---|
| C | 119 cy | **91 cy** | **−28 cy** | 152 cy |
| C++ (Run 6) | 87 cy | — | — | 151 cy |

Residual gap: 5 cy (noise). The algorithms are now equivalent.

---

### All benchmarks — Run 7 (C only, C++ unchanged from Run 6)

| Benchmark | C Run 4 | C Run 7 | C++ Run 6 |
|---|---|---|---|
| Add | 43 cy | 32 cy | 34 cy |
| Cancel (realistic) | 39 cy | 37 cy | 22 cy |
| Match (data-driven) | 176 cy | 164 cy | 140 cy |
| best_bid (realistic) | 119 cy | **91 cy** | 86 cy |

---

## Run 8 — 2026-03-22 (both — same session, canonical current numbers)

Both implementations re-run in the same session to provide a clean side-by-side
comparison after the Run 7 bitmap algorithm correction.

| Benchmark | C | C++ |
|---|---|---|
| Add | 32 cy | 34 cy |
| Cancel (realistic) | 38 cy | 22 cy |
| Match (data-driven) | 162 cy | 135 cy |
| best_bid (realistic) | 88 cy | 86 cy |

best_bid gap: 2 cy — noise. Algorithms are equivalent and results are now
directly comparable.

---

## Current Best Numbers (Run 8 — data-driven, integer ticks, warm cache, core 2)

| Benchmark | C | C++ |
|---|---|---|
| Add | 32 cy | 34 cy |
| Cancel (realistic) | 38 cy | 22 cy |
| Match (data-driven) | 162 cy | 135 cy |
| best_bid (realistic) | 88 cy | 86 cy |

---

## Open Items

- [ ] C doubly-linked promotion decision — **deferred to server hardware**. 53 cy at q=10 mid is 3 cy over the 50 cy threshold, within laptop noise. `isolcpus` insufficient on a mobile chip (thermal throttling, SMI, power states). Retest on deployment-class hardware (Xeon/EPYC) with IRQ affinity pinned away from the test core. Threshold and promote path unchanged — see architect-spec.md Decision Register.
- [ ] Revisit C++ bitmap unroll if future benchmark shows N=1 worst case becoming load-bearing
- [x] Investigate C best_bid gap vs C++ — resolved in Run 7/8 (algorithmic parity restored, 2 cy residual is noise)
