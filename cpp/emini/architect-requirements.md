# agentArchitect Brief — E-mini S&P 500 Limit Order Book

## The Problem

Design a limit order book for the CME E-mini S&P 500 futures contract (ES), implemented in both C and C++. The two implementations will be benchmarked head-to-head. Implementation difficulty relative to performance is a secondary goal — the comparison should be informative, not just a race.

A limit order book maintains two sides: resting bid orders (buyers) and resting ask orders (sellers), each organised by price level with time priority within a level. Incoming orders either rest in the book (if no crossing price exists) or match against resting orders on the opposite side (if the price crosses the spread). The three primary operations are: **add** (place a resting order), **cancel** (remove a resting order by ID), and **match** (execute against resting orders).

## Domain Constraints

- Tick size: 0.25 index points. All valid ES prices are exact multiples of 0.25.
- Price range: ±20% hard CME limit = 8800 ticks bidirectional from any reference price.
- Session order volume: up to 1,000,000 orders per session (QuantCup canonical bound).
- Cancel rate exceeds add rate by 10:1 or more in live markets. Cancel latency is a first-class requirement, not a secondary one.
- Price-time priority: within a price level, orders execute in arrival order (FIFO).
- High throughput is a first-class requirement. Every operation should drive toward O(1).

## What the Book Is

The book is **N integer FIFO queues**, one per price level per side, tiered by integer tick magnitude. Orders arrive in a Poisson-ish distribution across price levels. Each queue is independent for add and cancel; the matcher couples the two sides. The matcher is an explicit component — it is not implicit in the per-level queue operations.

## The Integer Engine

There is no floating-point arithmetic inside the book. Prices are represented as integer ticks (`tick = (uint32_t)((price - base) * 4.0 + 0.5)`). One sanctioned cast occurs at the API boundary. No cast or implicit promotion appears inside any hot-path function. This is enforced by `-Wconversion -Werror` in C and by the type system in C++.

## Locked Decisions

| Decision | Resolution |
|---|---|
| Price representation | uint32_t integer tick — no float inside the book |
| Primary structure | Flat array indexed by integer tick, one per side |
| Active level tracking | uint64_t bitmap companion, synchronous updates only — never lazy |
| Arena policy | No-reuse, monotonic allocation. slot_index == order_id by construction. Session-sized. |
| Node size | 16 bytes maximum — keeps 1M-order arena within L3 (16 MB) |
| Per-level queue | Intrusive singly-linked FIFO. Doubly-linked is benchmark-conditional only. |
| Cancel index | Dissolved. arena[order_id] is the direct dereference. |
| Free list | Dissolved. alloc is next_slot++, free is a no-op (dead-mark only). |
| Cancel latency | First-class requirement — equal to add/match throughput |

## Open Decisions for the Architect

1. **Sliding window vs full array.** The full 8800-tick array is 140 KB — L2 boundary. A sliding window anchored on mid-price reduces this to 32 KB (1540-tick ±7% overnight range) — comfortably L2. The window is stationary under normal conditions and shifts only on large price excursions (not a hot-path event). The Architect must choose and state the crossover condition explicitly.

2. **Singly-linked promote condition.** Singly-linked is the starting point. The Architect must state the condition under which `prev_idx` is added: specifically, what benchmark result (cycles/op at what queue depth q) triggers promotion to doubly-linked.

3. **Matcher interface.** The matcher couples bid and ask sides. The Architect must name it as an explicit component with a defined interface — it must not emerge informally from implementation.

## Deliverables

The Architect must produce a complete implementation specification covering:

1. **Data model** — all types, struct layouts with sizeof, ownership
2. **API boundary** — where external types convert to internal; preconditions on inputs
3. **Interface specification** — every public function: precondition, postcondition, error behaviour
4. **Performance contract** — complexity and cache tier for every hot-path operation
5. **Module boundaries** — what each module hides, justified by a named design decision
6. **Test suite contract** — invariants the checker must verify; test case catalogue; oracle rules (expected values must be derived independently of the implementation)
7. **Benchmark contract** — operations to benchmark, cache regimes (warm and cold), and specifically a cancel benchmark at varying queue depths (q = 1, 5, 10, 50) to inform the singly-linked vs doubly-linked conditional

The spec must cover both C and C++ implementations. Where the two languages impose different design choices, state them explicitly.
