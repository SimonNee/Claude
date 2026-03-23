# Architecture Specification — Orderbook Visualiser & Simulation Platform (Phase 1)

**Produced by**: agentArchitect
**Inputs**: agentContext report (in brief), agentDuality verdicts (in brief), eth/cpp/book.hpp, emini/cpp/book.hpp, eth/cpp/loader.hpp, emini/cpp/loader.hpp, third_party/imgui/imconfig.h
**Date**: 2026-03-23
**Target**: agentCPP (Phase 1 implementation)
**Status**: COMPLETE — ready for agentCPP. Amended 2026-03-23: IEventSource abstraction + OUEventSource added. Amended 2026-03-23: theme.hpp added to view layer.

---

## Scope

Phase 1 only. The visualiser reads a CSV replay file or generates synthetic OU events in memory, advances a virtual clock, dispatches events into the ETH/USDT book, publishes double-buffered snapshots to a render thread that draws an ImGui DOM ladder and match tape with playback controls.

No algo interface is built in Phase 1. The module boundaries must not foreclose it.

---

## Decision Register

| Decision | Status | Resolution / Condition |
|---|---|---|
| Rendering architecture | LOCKED | Double-buffered atomic pointer swap (agentContext A2). Sim thread owns live book. Render thread reads a fixed-size snapshot copied each frame. |
| Replay engine type | LOCKED | Virtual-clock replay (agentContext B2). No sleep-based pacing. Virtual clock advances to each event's timestamp. Render thread runs at real 60 Hz independently. |
| Algo interface | LOCKED | Not built in Phase 1. Boundaries must not foreclose C1 virtual `on_*` callback pattern. |
| Visualisation widgets | LOCKED | ImGui Table for DOM ladder (agentContext D1). ImPlot deferred to Phase 2 (agentContext D2). |
| ImDrawIdx size | LOCKED | `unsigned int` (32-bit) — already set in imconfig.h. Do not re-set it anywhere else. |
| Model/view separation | LOCKED | Enforced structurally. No ImGui header included in any model-layer file. No model-layer pointer/reference readable from the view layer. Violation must be a compile error, not a convention. |
| Observable data flow | LOCKED | Data flows model → snapshot/queue → view only. The view has no function pointer, reference, or friendship into the model. |
| Which book is used | LOCKED | ETH/USDT book (`eth::book::Book`, `eth::book` namespace) for Phase 1. The snapshot struct is book-agnostic at the view layer so ES book can be added in Phase 2 without changing view code. |
| Timestamp format | LOCKED | `uint64_t nanoseconds` — monotonic virtual clock. Replay CSV must carry a `timestamp_ns` column. The existing `events.csv` lacks timestamps; a new replay CSV with timestamps is required and is specified below. |
| DOM ladder depth (snapshot) | LOCKED | 50 levels per side. Chosen because: 50 × 2 × 16 bytes = 1,600 bytes per snapshot; fits in L1 cache; sufficient for visual inspection. Configurable at compile time via `VIZ_LADDER_DEPTH`. |
| Match tape depth (snapshot) | LOCKED | 32 most-recent fills. `VIZ_TAPE_DEPTH`. |
| Snapshot synchronisation primitive | LOCKED | `std::atomic<uint32_t>` generation counter + two statically allocated `BookSnapshot` buffers. Sim writes to the inactive buffer, then increments the generation; render reads from the buffer indexed by `generation & 1`. No mutex on the hot path. |
| Playback controls state | LOCKED | Owned by sim thread. Render thread writes to a `PlaybackCmd` struct protected by a single `std::mutex` + `std::condition_variable`. Sim polls on each event dispatch. |
| Build system | LOCKED | CMake 3.16+. Single `CMakeLists.txt` at `cpp/viz/`. Third-party sources compiled in-tree. |
| C++ standard | LOCKED | C++17. Matches existing books. |
| Compiler flags | LOCKED | `-std=c++17 -O2 -march=native -Wall -Wextra -Wconversion -Wsign-conversion -Werror -fno-exceptions`. Same as existing books. |
| Namespaces | LOCKED | `viz::model` for sim/snapshot code. `viz::view` for render code. No cross-namespace direct access — only the snapshot struct crosses the boundary. |
| Event source abstraction | LOCKED | `SimEngine` consumes from `IEventSource*` (non-owning). The concrete type — `CsvEventSource` or `OUEventSource` — is constructed in `main.cpp` and injected. |
| CsvEventSource | LOCKED | Wraps `std::vector<ReplayEvent>` produced by `replay_loader`. Iterates the pre-loaded vector via `next_event()`. |
| OUEventSource | LOCKED | Pre-bakes a configurable number of OU-process events in its constructor. Default: `OU_DEFAULT_EVENT_COUNT = 86400` (one synthetic trading day at 1 ms/event). Timestamps are synthetic: uniform 1 ms spacing. `next_event()` iterates the pre-baked vector. The interface does not foreclose lazy generation in Phase 2. |
| OUEventSource — no file by default | LOCKED | The OU generator writes no file unless `save_csv(path)` is explicitly called. The CSV loader path is unchanged. |
| OUEventSource — save command | LOCKED | The view layer triggers `save_csv()` via a new `SaveCmd` crossing struct (not `PlaybackCmd`). `SaveCmd` is protected by a separate `std::mutex`. The save executes on the sim thread (polled alongside `PlaybackCmd`). |
| OU algorithm reference | LOCKED | Parameters and process are sourced from `cpp/emini/data/generate.q`. See Notes for agentCPP note 12. |
| View-layer style constants | LOCKED | All visual style values used by draw functions (colours, spacing, rounding, alpha, font reference) are defined in `src/view/theme.hpp`. No inline style literals in any draw function. See Notes for agentCPP note 18. |
| Phase 2 foreclosure check — ImPlot | OPEN | implot.h is present. Phase 2 can add `#include "implot.h"` inside view-layer files only. No action required now. |
| Phase 2 foreclosure check — algo | OPEN | The `IAlgoHandler` base class is defined in the spec but not instantiated. Its header lives in the model layer. Phase 2 wires a concrete subclass into `SimEngine`. No view changes needed. |
| Phase 2 foreclosure check — ES book | OPEN | `BookSnapshot` is already book-agnostic. Phase 2 adds a second `SimEngine` wrapping `es::book::Book`. A second `SnapshotBuffer` is passed to a second instance. The render layer gets a second snapshot pointer and an instrument selector widget. No changes to `BookSnapshot`, `LevelEntry`, or `FillEntry`. |
| Phase 2 foreclosure check — lazy OU generation | OPEN | `IEventSource::next_event()` returns one event at a time. Phase 2 can override `OUEventSource::next_event()` to generate on demand rather than from a pre-baked vector. No interface change required. |

---

## Directory and File Layout

```
cpp/viz/
  CMakeLists.txt                  — single build file
  src/
    model/
      replay_event.hpp            — ReplayEvent struct; no book.hpp dependency
      replay_loader.hpp           — load_replay_csv() declaration
      replay_loader.cpp           — load_replay_csv() implementation
      event_source.hpp            — IEventSource abstract interface (NEW)
      csv_event_source.hpp        — CsvEventSource declaration (NEW)
      csv_event_source.cpp        — CsvEventSource implementation (NEW)
      ou_event_source.hpp         — OUEventSource declaration (NEW)
      ou_event_source.cpp         — OUEventSource implementation (NEW)
      sim_engine.hpp              — SimEngine class declaration; holds IEventSource* not vector
      sim_engine.cpp              — SimEngine class implementation
      book_snapshot.hpp           — BookSnapshot, LevelEntry, FillEntry; no ImGui dependency
      snapshot_buffer.hpp         — SnapshotBuffer (double-buffer + atomic generation)
      playback_cmd.hpp            — PlaybackCmd, PlaybackState enums
      save_cmd.hpp                — SaveCmd crossing struct (NEW)
    view/
      theme.hpp                   — header-only; all visual style constants (colours, spacing, rounding, alpha, font reference)
      render_loop.hpp             — RenderLoop class declaration
      render_loop.cpp             — RenderLoop class implementation
      dom_ladder.hpp              — draw_dom_ladder() declaration
      dom_ladder.cpp              — draw_dom_ladder() implementation
      match_tape.hpp              — draw_match_tape() declaration
      match_tape.cpp              — draw_match_tape() implementation
      playback_controls.hpp       — draw_playback_controls() declaration
      playback_controls.cpp       — draw_playback_controls() implementation
    main.cpp                      — entry point; constructs engine and render loop; joins threads
```

**Structural rule**: files under `src/model/` must not include any file from `src/view/` or any ImGui header. Files under `src/view/` must not include any file from `src/model/` except `book_snapshot.hpp`, `snapshot_buffer.hpp`, `playback_cmd.hpp`, and `save_cmd.hpp`. `main.cpp` is the only file that includes both layers.

This is enforced via include-path scoping in CMakeLists.txt: the `view` compile target does not receive the model source directory on its include path (except for the four crossing headers listed above, which are placed at a top-level include path shared by both).

---

## Data Model

### Primary Types

| Type | Representation | Size | Rationale |
|---|---|---|---|
| `viz::ns_t` | `uint64_t` | 8B | Virtual clock; nanoseconds since epoch |
| `viz::tick_t` | `uint32_t` | 4B | Mirrors `eth::book::tick_t`; absolute price tick |
| `viz::qty_t` | `uint64_t` | 8B | Mirrors `eth::book::qty_t`; scaled 10^8 |
| `viz::price_f` | `double` | 8B | Human-readable price for display only; computed at render time from tick; never stored in model structs |
| `viz::LadderDepth` | `uint32_t` compile-time | — | `VIZ_LADDER_DEPTH = 50` |
| `viz::TapeDepth` | `uint32_t` compile-time | — | `VIZ_TAPE_DEPTH = 32` |

### Replay Event (model layer — `replay_event.hpp`)

This is the in-memory parsed form of one row of the replay CSV. It is not a wire format. It is also the element type produced by `OUEventSource`.

```
struct ReplayEvent {
    uint64_t  timestamp_ns;  // offset  0 — virtual clock target; nanoseconds
    uint32_t  tick;          // offset  8 — absolute price tick
    uint32_t  _pad;          // offset 12 — explicit pad
    uint64_t  qty;           // offset 16 — scaled qty; 0 = delete level
    uint8_t   side;          // offset 24 — 0=BID, 1=ASK
    uint8_t   _pad2[7];      // offset 25 — pad to 32 bytes
};                           // total: 32 bytes
```

`static_assert(sizeof(ReplayEvent) == 32U)`
`static_assert(alignof(ReplayEvent) == 8U)`

Note: `side` is stored as `uint8_t` rather than `eth::book::side_t` to keep `replay_event.hpp` free of any dependency on the book headers. Conversion to `eth::book::side_t` occurs in `SimEngine::dispatch_event()` — the single conversion site.

### LevelEntry (crossing struct — `book_snapshot.hpp`)

One price level as published to the render thread. All fields are plain integers; `price_f` is computed at render time.

```
struct LevelEntry {
    uint32_t  tick;       // offset 0 — absolute price tick
    uint32_t  _pad;       // offset 4 — explicit pad
    uint64_t  qty;        // offset 8 — scaled qty (10^8); 0 = empty slot
};                        // total: 16 bytes
```

`static_assert(sizeof(LevelEntry) == 16U)`

### FillEntry (crossing struct — `book_snapshot.hpp`)

One recent fill as published to the render thread. For Phase 1 (ETH L2 MBP with no matching engine), fills are synthesised from crossed-spread events rather than from a real matcher. See Notes for agentCPP.

```
struct FillEntry {
    uint64_t  timestamp_ns;   // offset  0 — virtual clock at fill time
    uint32_t  price_tick;     // offset  8 — fill price tick
    uint32_t  _pad;           // offset 12 — explicit pad
    uint64_t  qty;            // offset 16 — filled quantity (scaled)
};                            // total: 24 bytes
```

`static_assert(sizeof(FillEntry) == 24U)`

### BookSnapshot (crossing struct — `book_snapshot.hpp`)

The complete state the render thread reads each frame. This is the only struct that crosses the model/view boundary.

```
struct BookSnapshot {
    // Bid levels: bids[0] = best bid, bids[1] = next best, ...
    LevelEntry  bids[VIZ_LADDER_DEPTH];     // 50 × 16 = 800 bytes
    // Ask levels: asks[0] = best ask, asks[1] = next best, ...
    LevelEntry  asks[VIZ_LADDER_DEPTH];     // 50 × 16 = 800 bytes
    // Recent fills: fills[0] = most recent
    FillEntry   fills[VIZ_TAPE_DEPTH];      // 32 × 24 = 768 bytes
    uint32_t    fill_count;                 // number of valid fills in fills[]
    uint32_t    best_bid_tick;              // TICK_INVALID if no bids
    uint32_t    best_ask_tick;             // TICK_INVALID if no asks
    uint32_t    _snap_pad;                  // explicit pad
    uint64_t    virtual_clock_ns;           // virtual time at snapshot publication
    uint64_t    window_base_tick;           // eth::book window_base at snapshot time
};
// sizeof: 800 + 800 + 768 + 4 + 4 + 4 + 4 + 8 + 8 = 2,400 bytes
```

`static_assert(sizeof(BookSnapshot) == 2400U)`

Working set: two snapshots = 4,800 bytes. Fits comfortably in L1 cache (typical 32–64 KB).

`VIZ_LADDER_DEPTH` and `VIZ_TAPE_DEPTH` are `static constexpr uint32_t` in `book_snapshot.hpp`. They are the only place these values are defined.

### PlaybackCmd (crossing struct — `playback_cmd.hpp`)

Written by the render thread (view layer) when the user interacts with playback controls. Read by the sim thread. Protected by `std::mutex`.

```
enum class PlaybackState : uint8_t { RUNNING = 0, PAUSED = 1 };

struct PlaybackCmd {
    PlaybackState  state;         // RUNNING or PAUSED
    uint8_t        _pad[3];       // explicit pad
    float          speed_mult;    // replay speed multiplier; 1.0 = real-time
                                  // range: [0.0625, 16.0] (1/16× to 16×)
                                  // 0.0 is not valid; clamped to 0.0625 on write
};                                // total: 8 bytes
```

`static_assert(sizeof(PlaybackCmd) == 8U)`

### SaveCmd (crossing struct — `save_cmd.hpp`)

Written by the render thread when the user triggers a CSV save of the current OU-generated event sequence. Read by the sim thread. Protected by a separate `std::mutex` (`save_mutex_` in `main.cpp`, distinct from `cmd_mutex_`). The save is not on the hot path. This struct is separate from `PlaybackCmd` because it carries a path string and has different semantics (one-shot trigger vs persistent state).

```
struct SaveCmd {
    bool        pending;         // offset 0 — true = a save has been requested
    uint8_t     _pad[7];         // offset 1 — pad to 8 bytes before path
    char        path[512];       // offset 8 — null-terminated output path
};                               // total: 520 bytes
```

`static_assert(sizeof(SaveCmd) == 520U)`

**Protocol**: the render thread sets `pending = true` and writes `path` under `save_mutex_`. The sim thread polls `pending` under `save_mutex_` after each event dispatch. When `pending` is true, the sim thread calls `ou_source->save_csv(path)` (if the current source is `OUEventSource`; otherwise it is a no-op), then sets `pending = false` and releases the mutex. The render thread does not wait for the save to complete — it is fire-and-forget. The sim thread performs the file write synchronously (blocking its event loop) since this is an explicit user action, not part of the replay hot path. For Phase 1 this is acceptable; Phase 2 can offload to a worker thread.

The view layer does not know whether the current source is `OUEventSource` or `CsvEventSource`. It sends `SaveCmd` unconditionally. The sim thread is responsible for routing it correctly.

### OUParams (`ou_event_source.hpp`)

Parameters for the OU process. Defined in `ou_event_source.hpp`, not a crossing struct (model-only).

```
struct OUParams {
    double   mu;               // long-run mean, dollars        (default 5500.00)
    double   theta;            // mean-reversion speed per step (default 0.005)
    double   sigma;            // per-step diffusion, dollars   (default 1.25)
    double   drift;            // per-step directional drift    (default 0.0)
    double   tick_size;        // dollar size of one tick       (default 0.01 for ETH/USDT)
    double   base_price;       // price at tick 0               (default 0.0 for ETH/USDT)
    uint32_t ticks_per_dollar; // ticks per dollar              (default 100 for ETH/USDT)
    uint32_t max_tick;         // maximum valid tick index      (default 65535 for ETH/USDT)
    uint32_t cancels_per_add;  // cancel events per add         (default 10)
    uint32_t seed;             // RNG seed                      (default 42)
};
```

No `static_assert` required — this is a parameter bag, not a crossing struct. The default values listed above are for the ETH/USDT instrument. The OU mid-price parameters (`mu`, `theta`, `sigma`) are calibrated from the ES instrument reference but rescaled to ETH/USDT price levels.

**Note on ETH/USDT tick convention**: ETH/USDT prices on Binance are quoted to $0.01. So `tick_size = 0.01`, `ticks_per_dollar = 100`. The OU `mu` should be set to a representative ETH price (e.g. 2000.00 dollars) rather than the ES value of 5500.00. The `theta` and `sigma` values from the q reference are dimensionally per-step in dollars and are appropriate for a different price scale; agentCPP must rescale them. See Note 13 for the scaling rule.

### IEventSource (`event_source.hpp`)

Abstract interface. Model-layer only. `SimEngine` holds a non-owning `IEventSource*`.

```
class IEventSource {
public:
    virtual ~IEventSource() = default;

    // Returns true and writes the next event into `out` if one is available.
    // Returns false when the source is exhausted (no more events).
    // The source is stateful: each call advances an internal cursor.
    virtual bool next_event(ReplayEvent& out) = 0;

    // Resets the internal cursor to the first event.
    // After reset(), the next call to next_event() returns the first event again.
    virtual void reset() = 0;

    // Returns the total number of events available from this source.
    // For pre-baked sources: the count of pre-baked events.
    // For lazy sources (Phase 2): may return SIZE_MAX to indicate unbounded.
    virtual std::size_t event_count() const = 0;
};
```

Include: `<cstddef>` for `std::size_t`. No other includes in this header. `ReplayEvent` is forward-declared here; `#include "replay_event.hpp"` in the `.hpp` files that implement this interface.

**Precondition for `next_event()`**: `out` is a valid writable `ReplayEvent`. The source has not been destroyed.

**Postcondition for `next_event()`**: If returns true, `out` contains a valid event with well-formed fields (side 0 or 1; timestamp_ns monotonically non-decreasing within a session). If returns false, `out` is unmodified.

**Error behaviour**: No exceptions. If an implementation encounters an internal error, it returns false (treats source as exhausted).

### SnapshotBuffer (`snapshot_buffer.hpp`)

The double-buffer mechanism. This is model-layer infrastructure; it is not book-agnostic in the sense that it contains `BookSnapshot`, but it hides the synchronisation mechanism from both producers and consumers.

```
struct SnapshotBuffer {
    BookSnapshot       buffers[2];            // two statically allocated snapshots
    std::atomic<uint32_t> generation;         // incremented by sim thread after each write
};
```

`SnapshotBuffer` is not copyable or movable. It is constructed once in `main.cpp` on the stack or as a static object and passed by pointer to both `SimEngine` and `RenderLoop`.

The generation counter encodes which buffer was last written: the **write buffer** index is `generation.load() & 1` before the write; after incrementing it is `(generation + 1) & 1`. The render thread always reads from `generation.load() & 1` — the most recently completed write.

**Producer protocol** (sim thread, inside `SimEngine::publish_snapshot()`):
1. Determine write index: `uint32_t wi = (generation.load(std::memory_order_relaxed) + 1) & 1`
2. Write all fields of `buffers[wi]`
3. `generation.fetch_add(1, std::memory_order_release)`

**Consumer protocol** (render thread, inside `RenderLoop::read_snapshot()`):
1. `uint32_t gen = generation.load(std::memory_order_acquire)`
2. Read from `buffers[gen & 1]`
3. No lock needed. A torn read is acceptable: the render thread may read a snapshot that is one frame old; it will never read a partially written snapshot because the generation increment acts as the publication barrier.

The render thread does not spin on the generation counter. It reads once per frame and draws whatever is there.

---

## Replay CSV Format

The existing `cpp/eth/data/events.csv` has no timestamps. The replay engine requires a new CSV with timestamps. The spec for that file is as follows.

**File**: `cpp/eth/data/replay.csv` (new file, generated separately)
**Columns**: `timestamp_ns,event_type,side,tick,qty`
- `timestamp_ns`: `uint64_t` nanoseconds, monotonically non-decreasing
- `event_type`: `UPSERT` or `DELETE`
- `side`: `BID` or `ASK`
- `tick`: plain unsigned integer (absolute price tick)
- `qty`: scaled uint64_t (10^8 units); must be 0 for `DELETE`

The loader (`replay_loader.cpp`) produces `std::vector<ReplayEvent>` sorted by `timestamp_ns` ascending. If the file is not sorted, the loader sorts it. Events with equal timestamps are processed in file order.

---

## API Boundary

### Model-layer API boundary (`replay_loader.cpp`)

All external data (CSV strings) enters the system here. After loading, all values are plain integer types with no further string parsing in the hot path.

Conversions at this boundary:
- `timestamp_ns` column: decimal string parsed to `uint64_t`. Overflow → event skipped, warning count incremented.
- `event_type` column: string `"UPSERT"` → `qty` field passed through; string `"DELETE"` → `qty` field is forced to 0 regardless of CSV value.
- `side` column: string `"BID"` → `uint8_t(0)`; string `"ASK"` → `uint8_t(1)`; other → event skipped.
- `tick` column: decimal string parsed to `uint32_t`. Overflow or blank → event skipped.
- `qty` column: decimal string parsed to `uint64_t`. Blank or parse failure → 0 (treated as DELETE).

No floating-point arithmetic occurs anywhere in the loader.

### Model-layer API boundary (`sim_engine.cpp`, `dispatch_event()`)

`ReplayEvent::side` (`uint8_t`) is converted to `eth::book::side_t` here:
- `0` → `eth::book::side_t::BID`
- `1` → `eth::book::side_t::ASK`
- other → precondition violation; event is skipped.

This is the only site where `uint8_t` side is converted to `eth::book::side_t`. It does not occur in the loader.

### Model-layer API boundary (`ou_event_source.cpp`, constructor)

OU floating-point prices are converted to integer ticks at construction time (during pre-bake), not during event dispatch. This is the only place floating-point arithmetic occurs in the OU path.

- `tick = static_cast<uint32_t>(std::floor((price - params.base_price) * params.ticks_per_dollar + 0.5))`
- Prices are clamped to `[base_price, base_price + max_tick / ticks_per_dollar]` before conversion.
- Quantities are computed as integers directly (no float-to-int on the hot path).

After construction, `OUEventSource::next_event()` iterates the pre-baked `std::vector<ReplayEvent>` with no floating-point operations.

### View-layer API boundary (`render_loop.cpp`, `RenderLoop::read_snapshot()`)

`LevelEntry::tick` is converted to a display price (`double`) for rendering:
- `price_display = static_cast<double>(tick) * 0.01` — one multiplication per visible level, at render time only
- This conversion occurs in `draw_dom_ladder()` and `draw_match_tape()` only; never in any model-layer file.
- `TICK_INVALID` (0xFFFFFFFF) must be checked before this conversion; display "--.--" if invalid.

---

## Interface Specification

### `replay_loader.hpp` / `replay_loader.cpp`

**`load_replay_csv(path) -> std::vector<ReplayEvent>`**

Precondition: `path` is a null-terminated string naming a readable file in the format specified above. File need not be sorted by `timestamp_ns`.

Postcondition: Returns a vector of `ReplayEvent` sorted ascending by `timestamp_ns`. All entries have well-formed fields (side is 0 or 1; qty is 0 for DELETE events). Events that fail validation are silently skipped. The vector may be empty if the file is empty or unreadable.

Error behaviour: On file open failure, returns empty vector. On any row parse error, that row is skipped; parsing continues. No exception is thrown (`-fno-exceptions`).

---

### `event_source.hpp` — `class viz::model::IEventSource`

Abstract interface. Header-only (pure virtual class with defaulted destructor). No `.cpp` file.

The interface is defined in namespace `viz::model`. All three methods are pure virtual. The destructor is `virtual` and `= default`.

See Data Model section for the method signatures, preconditions, postconditions, and error behaviour.

---

### `csv_event_source.hpp` / `csv_event_source.cpp` — `class viz::model::CsvEventSource`

`CsvEventSource` wraps a pre-loaded `std::vector<ReplayEvent>`. It takes ownership of the vector (moves it in). It does not re-read the file.

**Constructor: `CsvEventSource(std::vector<ReplayEvent> events)`**

Precondition: `events` is sorted ascending by `timestamp_ns` (guaranteed by `load_replay_csv`). May be empty.

Postcondition: The internal vector is populated. `cursor_` is set to 0. `event_count()` returns `events_.size()`.

Error behaviour: No allocation failure handling beyond what `std::vector` move constructor provides.

**`next_event(ReplayEvent& out) -> bool`**

Precondition: None beyond the class invariant.

Postcondition: If `cursor_ < events_.size()`: copies `events_[cursor_]` into `out`, increments `cursor_`, returns true. If `cursor_ >= events_.size()`: returns false, `out` unmodified.

**`reset() -> void`**

Postcondition: `cursor_` is set to 0. The next call to `next_event()` returns the first event.

**`event_count() const -> std::size_t`**

Returns `events_.size()`.

---

### `ou_event_source.hpp` / `ou_event_source.cpp` — `class viz::model::OUEventSource`

`OUEventSource` generates a pre-baked sequence of OU-process events in its constructor. The sequence is stored as `std::vector<ReplayEvent>`. After construction, the class behaves identically to `CsvEventSource` with respect to `next_event()`, `reset()`, and `event_count()`.

**`static constexpr std::size_t OU_DEFAULT_EVENT_COUNT = 86400U`**

Defined in `ou_event_source.hpp`. One synthetic trading day at 1 ms/event = 86,400 events. This is the default if no count is specified.

**Constructor: `OUEventSource(OUParams params, std::size_t event_count = OU_DEFAULT_EVENT_COUNT)`**

Precondition: `params.theta > 0.0`. `params.sigma > 0.0`. `params.tick_size > 0.0`. `params.ticks_per_dollar > 0`. `event_count > 0`.

Postcondition: `events_` is populated with `event_count` pre-baked events. All events have well-formed fields (side 0 or 1; ticks within `[0, params.max_tick]`; timestamps monotonically increasing with 1 ms spacing). `cursor_` is 0.

Algorithm (pre-bake, executed entirely in constructor):
1. Seed a 64-bit Mersenne Twister (`std::mt19937_64`) with `params.seed`.
2. Compute the number of ADD events: `adds = event_count / (1 + params.cancels_per_add + 1)` (integer division). This replicates the q script's `ADDS_COUNT` formula.
3. Generate `adds` standard normal variates via Box-Muller (see Note 14 for the exact algorithm).
4. Apply the OU recurrence starting at `params.mu`: `P[t+1] = P[t] + theta*(mu - P[t]) + drift + sigma*Z[t]`. Snap each price to the nearest tick: `snapped = tick_size * std::floor(0.5 + price / tick_size)`. Clamp to `[base_price, base_price + max_tick * tick_size]`.
5. Convert snapped prices to integer ticks using the formula at the API Boundary section.
6. Alternate ADD sides: even index = BID (tick = mid_tick - 1), odd index = ASK (tick = mid_tick + 1).
7. Generate quantities: 70% of events use qty in `[1, 5]` (uniform integer), 30% use qty in `[6, 50]` (uniform integer). Scale by `10^8` for ETH/USDT. Use the same Mersenne Twister.
8. Generate `cancels_per_add` CANCEL events per ADD event. CANCEL events have `qty = 0`, same `side` as the ADD, `tick = 0`. Timestamps are interleaved uniformly within the 1 ms block following the ADD event.
9. Generate MATCH events for the remainder of `event_count`. MATCH events use an independent OU step around `mu` for price, qty in `[1, 20]`, alternating BID/ASK aggressor.
10. Assign timestamps: uniform 1 ms spacing starting at `timestamp_ns = 0`. Event at index `i` gets `timestamp_ns = static_cast<uint64_t>(i) * 1'000'000ULL`.
11. Store in `events_` as `std::vector<ReplayEvent>`. Set `cursor_ = 0`.

Error behaviour: If `event_count == 0` or any parameter precondition is violated, `events_` is left empty and the source behaves as exhausted. No exception.

**`next_event(ReplayEvent& out) -> bool`**

Identical to `CsvEventSource::next_event()`.

**`reset() -> void`**

Identical to `CsvEventSource::reset()`.

**`event_count() const -> std::size_t`**

Returns `events_.size()`.

**`save_csv(const char* path) -> bool`**

Not part of `IEventSource`. Declared only on `OUEventSource`.

Precondition: `path` is a non-null, null-terminated string naming a writable path.

Postcondition: Writes the pre-baked `events_` to the file at `path` in the replay CSV format (`timestamp_ns,event_type,side,tick,qty`). Returns true if the write succeeded, false on any I/O failure.

Side effects: Does not modify `events_` or `cursor_`. Does not reset the source.

Error behaviour: If `events_` is empty, writes the header row and returns true (empty file is valid). On file open failure, returns false. No exception.

**Note on `save_csv()` routing in `SimEngine`**: `SimEngine` holds an `IEventSource*`. To call `save_csv()`, it must `dynamic_cast<OUEventSource*>(source_)`. If the cast returns null (source is `CsvEventSource`), the save request is silently dropped. This is the only use of `dynamic_cast` in the codebase. It is acceptable because `save_csv()` is not on the hot path.

---

### `sim_engine.hpp` / `sim_engine.cpp` — `class viz::model::SimEngine`

`SimEngine` owns the live `eth::book::Book`, the virtual clock, and the `PlaybackCmd` mutex. It does **not** own `SnapshotBuffer`, `IEventSource`, `PlaybackCmd`, or `SaveCmd` — it holds non-owning pointers to all of them.

**Constructor: `SimEngine(IEventSource* source, SnapshotBuffer* sb, std::mutex* cmd_mutex, PlaybackCmd* cmd, std::mutex* save_mutex, SaveCmd* save_cmd)`**

Precondition: All pointer arguments are non-null and outlive this object. `cmd` is initialised to `{PlaybackState::PAUSED, {}, 1.0f}` by the caller before construction. `save_cmd` is initialised to `{false, {}, ""}` by the caller. `source` has been constructed and is ready (i.e. its first `next_event()` call will return the first event or false if empty).

Postcondition: The book is constructed with `NULL_BASE_TICK`. The virtual clock is set to 0. No snapshot is published until `run()` is called.

Error behaviour: Book construction failure calls `std::terminate` (per `-fno-exceptions` and `operator new` semantics).

**`SimEngine::run() -> void`**

This is the sim thread entry point. Called from `std::thread` in `main.cpp`.

Precondition: The `IEventSource`, `SnapshotBuffer`, `PlaybackCmd`, and `SaveCmd` pointers are valid.

Postcondition: Runs until `stop()` is called. On each iteration:
1. Reads `PlaybackCmd` under `cmd_mutex`.
2. If PAUSED, sleeps 5 ms and loops.
3. Polls `SaveCmd` under `save_mutex`. If `save_cmd->pending` is true: calls `save_csv()` via `dynamic_cast` (see Note 17), sets `save_cmd->pending = false`, releases mutex.
4. If RUNNING, calls `source_->next_event(e)`. If the source is exhausted, transitions to PAUSED and waits for `stop()`.
5. Advances the virtual clock to `e.timestamp_ns`. Calls `dispatch_event(e)`. Calls `publish_snapshot()`.

Real-time pacing: unchanged from the original spec (wall-clock delay computation via `std::chrono::steady_clock`).

Error behaviour: If `dispatch_event()` fails (e.g. tick out of window), the event is skipped; the engine does not abort.

**`SimEngine::stop() -> void`** — unchanged.

**`SimEngine::dispatch_event(const ReplayEvent& e) -> bool` (private)** — unchanged.

**`SimEngine::publish_snapshot() -> void` (private)** — unchanged.

---

### `snapshot_buffer.hpp` — `struct viz::model::SnapshotBuffer`

Unchanged from the original spec.

---

### `save_cmd.hpp` (NEW)

No member functions. Plain data struct. Defined in namespace `viz::model`.

The struct is defined in the Data Model section above. No `.cpp` file.

`save_cmd.hpp` includes only `<cstddef>` and `<cstring>`.

---

### `render_loop.hpp` / `render_loop.cpp` — `class viz::view::RenderLoop`

`RenderLoop` owns the GLFW window, the ImGui context, and the ImGui frame lifecycle. It does not own `SnapshotBuffer`, `PlaybackCmd`, or `SaveCmd`.

**Constructor: `RenderLoop(SnapshotBuffer* sb, std::mutex* cmd_mutex, PlaybackCmd* cmd, std::mutex* save_mutex, SaveCmd* save_cmd, const char* title)`**

Precondition: All pointer arguments are non-null and outlive this object. `title` is a null-terminated string.

Postcondition: GLFW window is created. ImGui context is created. `IMGUI_CHECKVERSION()` is called. `ImDrawIdx` must be `unsigned int` — verified at runtime via `IM_ASSERT(sizeof(ImDrawIdx) == 4)`. Returns fully ready render loop or calls `std::terminate` on GLFW/ImGui init failure.

**`RenderLoop::run() -> void`**

Runs on the main thread (GLFW requirement: window must be created and polled on the same thread). Loops at up to 60 Hz. Each frame:
1. `glfwPollEvents()`. Exit loop if window close requested.
2. Begin ImGui frame.
3. `read_snapshot()` — load current snapshot from `SnapshotBuffer`.
4. `draw_dom_ladder(snapshot_)` — draws the ladder window.
5. `draw_match_tape(snapshot_)` — draws the tape window.
6. `draw_playback_controls(snapshot_, cmd_mutex_, cmd_)` — draws controls; writes `PlaybackCmd` under mutex if user changed a control.
7. `draw_ou_controls(save_mutex_, save_cmd_)` — draws the OU save button (NEW; see below). Only renders a "Save CSV" button; the source-type indicator (CSV vs OU) is not shown in Phase 1 — agentCPP may choose to show it as a static label if convenient.
8. `ImGui::Render()`, `ImGui_ImplOpenGL3_RenderDrawData()`.
9. `glfwSwapBuffers()`.
10. Frame-rate cap: sleep if the frame completed in less than 16.67 ms.

**`RenderLoop::stop() -> void`** — unchanged.

Destructor: unchanged.

---

### `dom_ladder.hpp` — `draw_dom_ladder(const BookSnapshot&) -> void`

Unchanged from original spec.

---

### `match_tape.hpp` — `draw_match_tape(const BookSnapshot&) -> void`

Unchanged from original spec.

---

### `playback_controls.hpp` — `draw_playback_controls(const BookSnapshot&, std::mutex*, PlaybackCmd*) -> void`

Unchanged from original spec.

---

### `ou_controls.hpp` / `ou_controls.cpp` (NEW — view layer)

**`draw_ou_controls(std::mutex* save_mutex, SaveCmd* save_cmd) -> void`**

Precondition: Called only within an ImGui frame. `save_mutex` and `save_cmd` are non-null.

Postcondition: Draws one ImGui window titled "OU Controls" containing:
- A "Save CSV" button. When clicked: acquires `save_mutex`, sets `save_cmd->pending = true`, copies the path from an internal text input buffer into `save_cmd->path` (null-terminated, truncated to 511 characters), releases `save_mutex`.
- A text input field for the output path (default text: `"ou_events.csv"`). This is a local ImGui `InputText` state; it is not part of `SaveCmd`.

The mutex is held only during the `SaveCmd` write. The ImGui draw calls are outside the lock.

Error behaviour: If `save_cmd->pending` is already true (previous save not yet processed), the button is disabled (greyed out). This prevents a second save overwriting the path before the first is processed.

---

## Threading Contract

| Resource | Owner | Accessed by | Synchronisation |
|---|---|---|---|
| `eth::book::Book` | sim thread | sim thread only | none (single owner) |
| `IEventSource` (cursor) | sim thread | sim thread only | none (single owner) |
| `SnapshotBuffer::buffers[wi]` (write side) | sim thread | sim thread only | atomic generation fence |
| `SnapshotBuffer::buffers[ri]` (read side) | render thread | render thread only | atomic generation acquire |
| `SnapshotBuffer::generation` | sim thread (write) | both threads (read) | `std::atomic<uint32_t>` |
| `PlaybackCmd` | render thread (write) | both threads (read) | `std::mutex` (`cmd_mutex_`) |
| `SaveCmd` | render thread (write) | both threads (read/write) | `std::mutex` (`save_mutex_`) |
| GLFW window | main thread | main thread only | none |
| ImGui context | main thread | main thread only | none |
| `SimEngine::stop_flag_` | main thread (write) | sim thread (read) | `std::atomic<bool>` |

**Thread count**: 2 threads in Phase 1. Unchanged.

`main.cpp` constructs: `SnapshotBuffer`, `PlaybackCmd`, `cmd_mutex`, `SaveCmd`, `save_mutex`, the chosen `IEventSource` concrete type (`CsvEventSource` or `OUEventSource`), `SimEngine`, `RenderLoop`. The sim thread is created via `std::thread`. `render_loop.run()` blocks the main thread. On window close, `main.cpp` calls `sim_engine.stop()` and joins the sim thread.

---

## Build System — CMakeLists.txt Structure

File path: `/home/developer/Documents/Claude/cpp/viz/CMakeLists.txt`

Required content (structure, not literal code):

1. `cmake_minimum_required(VERSION 3.16)`
2. `project(viz CXX)`
3. `set(CMAKE_CXX_STANDARD 17)` / `set(CMAKE_CXX_STANDARD_REQUIRED ON)`
4. `find_package(OpenGL REQUIRED)`
5. `find_package(glfw3 REQUIRED)` — system GLFW via pkg-config or CMake module
6. Compiler flags: add `-O2 -march=native -Wall -Wextra -Wconversion -Wsign-conversion -Werror -fno-exceptions` to `CMAKE_CXX_FLAGS`

7. Define `IMGUI_SOURCES` as the list of ImGui `.cpp` files from `third_party/imgui/`:
   - `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
   - `imgui_impl_glfw.cpp`, `imgui_impl_opengl3.cpp`

8. Define `IMPLOT_SOURCES` as the list of ImPlot `.cpp` files from `third_party/implot/`:
   - `implot.cpp`, `implot_items.cpp`
   Note: ImPlot is compiled now so it links without error; it is not called from Phase 1 code. Phase 2 activates it by adding draw calls in view files.

9. Define the `viz` executable target with sources:
   - All `src/model/*.cpp`
   - All `src/view/*.cpp`
   - `src/main.cpp`
   - `${IMGUI_SOURCES}`
   - `${IMPLOT_SOURCES}`

10. Include directories for `viz`:
    - `third_party/imgui` — for ImGui headers
    - `third_party/implot` — for ImPlot headers
    - `src/model` — for model layer headers (available to model sources and main.cpp)
    - `src/view` — for view layer headers (available to view sources and main.cpp)
    - `../eth/cpp` — for `eth::book::Book` (needed by sim_engine.cpp)

    The structural separation of model and view is enforced by the fact that `src/view` sources do not include `book.hpp` or `sim_engine.hpp` directly — they access only `book_snapshot.hpp`, `snapshot_buffer.hpp`, `playback_cmd.hpp`, and `save_cmd.hpp`, all of which are in `src/model`. Since both include paths are present on the compile line, the enforcement is by convention with the include-path rule stated in the directory layout section. A stricter enforcement would require two separate CMake targets; that is left as a Phase 2 option.

11. Link libraries for `viz`:
    - `glfw`
    - `OpenGL::GL`
    - `pthread`
    - `dl` (required by the OpenGL3 loader embedded in `imgui_impl_opengl3.cpp`)

12. Add a compile definition `IMGUI_IMPL_OPENGL_LOADER_CUSTOM` is not needed — `imgui_impl_opengl3_loader.h` is the default loader and is already present in `third_party/imgui/`.

---

## Performance Contract

| Operation | Complexity | Cache tier | Notes |
|---|---|---|---|
| `dispatch_event()` | O(1) | L2 (book Impl is ~1 MB) | Same cost as ETH book upsert_by_tick |
| `needs_rebase()` | O(1) | L1 | One field load |
| `rebase()` | O(WINDOW_SIZE) | L3 | Cold path; memset of ~512 KB |
| `publish_snapshot()` | O(VIZ_LADDER_DEPTH) | L1 (snapshot buffers ~5 KB) | Walks up to 50 levels per side; not on event hot path |
| `read_snapshot()` | O(1) | L1 | Atomic load + pointer index |
| `draw_dom_ladder()` | O(VIZ_LADDER_DEPTH) | L1 | ImGui table fill; 50 rows |
| `draw_match_tape()` | O(VIZ_TAPE_DEPTH) | L1 | 32 rows |
| Full render frame | O(1) amortised | L1/L2 | ImGui vertex buffer build dominates |
| `OUEventSource` construction | O(N) | L3 | Pre-bake of N=86,400 events; one-time cost at startup |
| `OUEventSource::next_event()` | O(1) | L2 | Vector index increment; same cost as CsvEventSource |
| `OUEventSource::save_csv()` | O(N) | L3 | File I/O; not on hot path; user-triggered only |

The sim thread event throughput is not a design target for Phase 1 — replay is paced to real-time or slower. The book can sustain the replay rate at any speed multiplier up to 16× given its measured O(1) upsert latency.

---

## Module Boundaries

| Module | Hides | Justified by |
|---|---|---|
| `replay_loader` | The CSV format and all string parsing | If the file format changes (e.g. binary format), only this module changes |
| `IEventSource` / `CsvEventSource` / `OUEventSource` | Whether events come from a file or are generated; the generation algorithm | If the source type changes (CSV → live feed → OU → hybrid), `SimEngine` does not change |
| `OUEventSource` | The OU process algorithm, its parameter set, the pre-bake implementation, the RNG | If the generation algorithm changes (OU → GBM → real data), only this module changes |
| `SimEngine` | The live book state, the virtual clock, the event source cursor, the rebase logic | Model/view separation: the view never sees the book; the view never sees the event source |
| `SnapshotBuffer` | The atomic double-buffer synchronisation mechanism | If the synchronisation strategy changes (e.g. triple buffer, lock-based), only this module changes |
| `RenderLoop` | GLFW, ImGui context lifecycle, frame pacing | If the windowing library changes, only this module changes |
| `draw_dom_ladder` | The ImGui Table layout for the ladder | If the ladder layout changes, only this function changes; snapshot struct is unchanged |
| `draw_match_tape` | The ImGui layout for the fill tape | Same rationale |
| `draw_playback_controls` | The ImGui layout for playback UI + mutex protocol for writing PlaybackCmd | If the control layout changes, only this function changes |
| `draw_ou_controls` | The ImGui layout for the OU save UI + mutex protocol for writing SaveCmd | If the save UI changes, only this function changes |
| `theme.hpp` | All visual style values: colours, spacing, rounding, alpha, font reference | A future design pass can restyle the entire view layer by editing this one file; `theme.hpp` is view-layer only and must not be included by any model-layer file |

---

## Phase 2 Foreclosure Analysis

These decisions are explicitly designed to not foreclose Phase 2 work.

**Algo interface (C1 pattern)**: `SimEngine::dispatch_event()` is the natural insertion point. In Phase 2, `SimEngine` accepts a non-owning pointer to an `IAlgoHandler` (virtual base, default no-ops). `IAlgoHandler::on_level_update(side, tick, qty, ns)` is called after each book update. `IAlgoHandler::on_fill(FillEntry)` is called after each synthesised fill. The `IAlgoHandler` interface is defined in a new `src/model/algo_handler.hpp` header. No view files change.

**ES book (second instrument)**: `BookSnapshot` is already book-agnostic. Phase 2 adds a second `SimEngine` wrapping `es::book::Book`. A second `SnapshotBuffer` is passed to a second instance. The render layer gets a second snapshot pointer and an instrument selector widget. No changes to `BookSnapshot`, `LevelEntry`, or `FillEntry`.

**ImPlot graphs**: Phase 2 adds new view files (e.g. `src/view/spread_chart.cpp`) that `#include "implot.h"` and draw from a rolling history buffer. The rolling history buffer is a new model-layer struct (`src/model/history_buffer.hpp`) written by `SimEngine` and read by the render thread via a third `std::atomic` pointer (or added as a field in `SnapshotBuffer`). No existing interfaces change.

**Live feed**: Phase 2 replaces `replay_loader` with a WebSocket feed reader. `SimEngine::load()` is replaced or augmented with `SimEngine::connect(url)`. The rest of the system is unchanged because the event dispatch and snapshot pipeline are feed-source-agnostic.

**Lazy OU generation**: Phase 2 can subclass `OUEventSource` and override `next_event()` to generate events on demand rather than from a pre-baked vector. The `IEventSource` interface does not change. `OU_DEFAULT_EVENT_COUNT` becomes irrelevant for the lazy subclass. `save_csv()` would need to be reconsidered for a lazy source (it cannot save a vector that does not exist); this is a Phase 2 design question and does not constrain Phase 1.

---

## Implementation Sequencing for agentCPP

Build in this order. Each step produces a compilable, testable artifact before the next begins.

**Step 1 — Data types and crossing structs (no compilation dependencies)**

Files to create:
- `src/model/replay_event.hpp` — `ReplayEvent` struct, `static_assert`s, no other includes
- `src/model/book_snapshot.hpp` — `LevelEntry`, `FillEntry`, `BookSnapshot`, `VIZ_LADDER_DEPTH`, `VIZ_TAPE_DEPTH`, `static_assert`s; includes `<cstdint>` only
- `src/model/playback_cmd.hpp` — `PlaybackState` enum, `PlaybackCmd` struct, `static_assert`
- `src/model/save_cmd.hpp` — `SaveCmd` struct, `static_assert`

Verification: each header compiles standalone with `g++ -std=c++17 -c`.

**Step 2 — IEventSource interface**

File to create:
- `src/model/event_source.hpp` — `IEventSource` abstract class with `next_event()`, `reset()`, `event_count()`

Verification: compiles standalone.

**Step 3 — Replay loader + CsvEventSource**

Files to create:
- `src/model/replay_loader.hpp` — declaration of `load_replay_csv()`
- `src/model/replay_loader.cpp` — implementation
- `src/model/csv_event_source.hpp` / `csv_event_source.cpp` — `CsvEventSource`

Also create the replay CSV generator or a sample `replay.csv` (10 rows minimum) for testing.

Verification: link a small `test_loader.cpp` that loads `replay.csv`, constructs a `CsvEventSource`, calls `next_event()` five times, and prints events. Confirm `timestamp_ns` is parsed correctly.

**Step 4 — OUEventSource**

Files to create:
- `src/model/ou_event_source.hpp` / `ou_event_source.cpp`

Verification: unit test that constructs `OUEventSource` with default params, calls `next_event()` for all events, and asserts: (a) all timestamps are monotonically increasing, (b) all ticks are within `[0, max_tick]`, (c) sides are 0 or 1. Also test `reset()` and `save_csv()`.

**Step 5 — SnapshotBuffer**

File to create:
- `src/model/snapshot_buffer.hpp`

This is header-only. Verification: compile standalone.

**Step 6 — SimEngine (no render dependency)**

Files to create:
- `src/model/sim_engine.hpp`
- `src/model/sim_engine.cpp`

At this stage, `publish_snapshot()` may be stubbed (writes a zeroed snapshot). The threading, virtual clock, playback pacing, `dispatch_event()`, and `SaveCmd` polling logic must be complete.

Verification: write a `test_sim.cpp` that constructs an `OUEventSource`, a `SnapshotBuffer`, a `SimEngine`, runs the engine with a very high `speed_mult` and a short timeout, and asserts that the generation counter has incremented.

**Step 7 — publish_snapshot() (level extraction)**

Fill in the real `publish_snapshot()` implementation.

**Step 8 — ImGui/GLFW scaffolding (render thread, no ladder/tape yet)**

Files to create:
- `src/view/render_loop.hpp`
- `src/view/render_loop.cpp`
- `src/main.cpp`
- `CMakeLists.txt`

`main.cpp` must construct `SaveCmd`, `save_mutex`, and pass them to both `SimEngine` and `RenderLoop`. The source type (`CsvEventSource` vs `OUEventSource`) is selected by a compile-time flag or a simple runtime argument; for Phase 1 either is acceptable. The simplest: if `argv[1]` is a CSV path, use `CsvEventSource`; if `argv[1]` is `"--ou"`, use `OUEventSource` with default params.

Verification: project compiles and runs. A blank ImGui window appears. Close it cleanly.

**Step 9 — DOM ladder, match tape, playback controls**

Files to create: `src/view/dom_ladder.hpp/.cpp`, `src/view/match_tape.hpp/.cpp`, `src/view/playback_controls.hpp/.cpp`.

**Step 10 — OU controls**

Files to create: `src/view/ou_controls.hpp`, `src/view/ou_controls.cpp`.

Wire `draw_ou_controls(save_mutex_, save_cmd_)` into `RenderLoop::run()`.

Verification: in `--ou` mode, click "Save CSV". Confirm the file appears at the specified path and contains the correct number of rows.

---

## Pre-Handoff Checklist

- [x] Data model section complete: all types, sizes, and rationale stated. `static_assert` values given for every crossing struct. `OUParams` is a model-only parameter bag (no `static_assert` required). `IEventSource` interface fully specified.
- [x] Decision register complete: locked/conditional/open clearly labelled. New rows added for `IEventSource`, `CsvEventSource`, `OUEventSource`, `SaveCmd`, lazy-generation foreclosure, and `theme.hpp`.
- [x] Every public function has precondition, postcondition, and error behaviour. This includes all `IEventSource` methods, `CsvEventSource`, `OUEventSource`, `save_csv()`, `draw_ou_controls()`, updated `SimEngine` and `RenderLoop` constructors.
- [x] API boundary explicitly named and documented. Three conversion sites: loader (strings to integers), OU constructor (float prices to integer ticks), render (tick to display double). No conversion on the hot path.
- [x] Struct layout table present with sizeof values: `ReplayEvent` 32B, `LevelEntry` 16B, `FillEntry` 24B, `BookSnapshot` 2400B, `PlaybackCmd` 8B, `SaveCmd` 520B.
- [x] Performance contract stated. OU pre-bake O(N) one-time cost noted.
- [x] No design decisions left for the implementation agent to make. `dynamic_cast` routing for `save_csv()`, source-type selection in `main.cpp`, `SaveCmd` pending-guard, and `theme.hpp` constant categories are all specified.
- [x] No open questions that block the implementation. OU parameter rescaling rule for ETH/USDT is stated in Note 13. Box-Muller algorithm is referenced in Note 14.

---

## Notes for agentCPP

1. **`-fno-exceptions` and GLFW**: GLFW init failure must call `std::terminate()` directly. Do not use a try/catch pattern anywhere in the codebase.

2. **imconfig.h — do not modify**: `#define ImDrawIdx unsigned int` is already set. Do not add this define anywhere else. `IMGUI_CHECKVERSION()` in `RenderLoop` constructor will catch any mismatch.

3. **ImGui context on main thread**: `ImGui::CreateContext()`, `ImGui::DestroyContext()`, all `ImGui::*` calls, `glfwPollEvents()`, and `glfwSwapBuffers()` must be called from the main thread only. The sim thread never touches ImGui.

4. **`eth::book::Book` include path**: In `sim_engine.cpp`, `#include "book.hpp"` requires that `cpp/eth/cpp/` is on the include path. CMakeLists.txt sets this via the `target_include_directories` directive.

5. **Level iteration — avoid the 65,536-tick loop**: `publish_snapshot()` must not iterate all 65,536 ticks to find the 50 best levels. The correct approach is to start at `best_bid_tick` and scan downward using the book's internal bitmap. Since the bitmap is not directly accessible from outside the class, the simplest correct approach is: call `book_.level_qty(side_t::BID, tick)` for each candidate tick, decrementing `tick` from `best_bid_tick` until either `VIZ_LADDER_DEPTH` levels are found or `tick` underflows below `window_base`. This is at most `VIZ_LADDER_DEPTH` calls if levels are dense (typical for a live orderbook), and at most a few hundred calls if levels are sparse. For Phase 1 this is acceptable. If it becomes a bottleneck, a `visit_levels` API is added to the book.

6. **Synthesised fills**: ETH L2 MBP has no real fills. The synthesised fill logic in `publish_snapshot()` is approximate. Do not present it to users as a real trade record. A display note "estimated" in the tape window title is appropriate.

7. **`PlaybackCmd::speed_mult` clamping**: The render thread must clamp the slider value to `[0.0625f, 16.0f]` before writing to `PlaybackCmd`. The sim thread does not need to validate this; it trusts the render thread's write.

8. **Virtual clock initialisation**: Before the first event is dispatched, `virtual_clock_ns` is 0. The first event's `timestamp_ns` becomes the initial virtual clock. Subsequent delays are computed as `event.timestamp_ns - prev_event.timestamp_ns`. If this delta is 0, dispatch immediately with no sleep.

9. **Replay CSV creation**: The existing `cpp/eth/data/events.csv` has no timestamps. A new `cpp/eth/data/replay.csv` must be created before Step 3 can be verified. The simplest generator: assign timestamps starting at 0, incrementing by 1,000,000 ns (1 ms) per event. This produces a 1 ms/event replay file that runs at normal speed with `speed_mult = 1.0`.

10. **No `std::function` in hot path**: Step 7 mentions `std::function` as a hypothetical for `visit_levels`. If that API is added to the book, the callback must be a function pointer or a template parameter — not `std::function`. `std::function` involves a heap allocation and virtual dispatch.

11. **`VIZ_LADDER_DEPTH` and `VIZ_TAPE_DEPTH`**: These are defined in `book_snapshot.hpp`. They must not be redefined in any other file. All code that uses these values must include `book_snapshot.hpp`.

12. **OU algorithm parameters — reference implementation**: The q reference lives at `/home/developer/Documents/Claude/cpp/emini/data/generate.q`. The OU recurrence is:

    ```
    P[t+1] = P[t] + theta * (mu - P[t]) + drift + sigma * Z[t]
    ```

    where `Z[t]` is a standard normal variate. Default parameters in the q file are for ES (mu=5500, theta=0.005, sigma=1.25, drift=0.0). These are instrument-specific. For ETH/USDT Phase 1 a representative calibration is: `mu = 2000.0` (dollars), `theta = 0.005` (same reversion speed), `sigma = 10.0` (larger diffusion appropriate for crypto), `drift = 0.0`. These are Phase 1 defaults; they are not locked and can be changed at construction time via `OUParams`. The structural logic (recurrence, snap-to-tick, clamp, side alternation, qty distribution, cancel interleaving) must match the q reference exactly.

13. **OU parameter rescaling for ETH/USDT**: The q file uses ES tick convention (`tick_size = 0.25`, `ticks_per_dollar = 4`, `base_price = 4400.0`). For ETH/USDT on Binance use `tick_size = 0.01`, `ticks_per_dollar = 100`, `base_price = 0.0`. The OU process operates in dollar space and is instrument-agnostic. The conversion from dollar price to integer tick is the only place the tick convention matters: `tick = floor((price - base_price) * ticks_per_dollar + 0.5)`. The `OUParams` struct carries all these values so no constants are hard-coded in `ou_event_source.cpp`.

14. **Box-Muller normal generation**: The q reference uses Box-Muller with a lower clamp on `u1` to avoid `log(0)`: `u1 = max(1e-10, U(0,1))`. In C++ use `std::uniform_real_distribution<double>(0.0, 1.0)` and clamp: `u1 = std::max(1e-10, u1_raw)`. Generate pairs: `z1 = sqrt(-2 * log(u1)) * cos(2*pi*u2)`, `z2 = sqrt(-2 * log(u1)) * sin(2*pi*u2)`. Use `std::mt19937_64` seeded with `params.seed`. No third-party RNG library. `<random>` and `<cmath>` are sufficient. The q reference uses `2*acos(-1.0)` for pi; in C++ use `M_PI` or compute as `std::acos(-1.0)`.

15. **Quantity distribution**: The q reference generates: 70% small (uniform integer in `[1, 5]`), 30% large (uniform integer in `[6, 50]`). In C++: draw `u = U(0, 1)`. If `u < 0.70`: `qty = uniform_int(1, 5)`. Else: `qty = uniform_int(6, 50)`. Scale by `10^8` for ETH/USDT (same qty scaling as the ETH book). Match quantities use uniform integer in `[1, 20]` scaled by `10^8`.

16. **`SaveCmd` path safety**: `save_cmd.hpp` defines `path[512]`. The render thread must write at most 511 characters plus a null terminator. Use `std::strncpy(save_cmd->path, input_buffer, 511); save_cmd->path[511] = '\0'` under the mutex. Do not use `strcpy`.

17. **`dynamic_cast` for save routing**: `SimEngine` holds `IEventSource* source_`. When `save_cmd->pending` is true, the sim thread does: `auto* ou = dynamic_cast<OUEventSource*>(source_)`. For this to work, `OUEventSource` must be a complete type visible to `sim_engine.cpp`, meaning `sim_engine.cpp` must `#include "ou_event_source.hpp"`. This is the only model-layer file that includes `ou_event_source.hpp`; all other model files depend on `IEventSource` only. The `dynamic_cast` requires RTTI. Check that `-fno-rtti` is not in the compiler flags — it is not (only `-fno-exceptions` is set). RTTI is available.

18. **`theme.hpp` — style constants and the no-inline-literal rule**: Create `src/view/theme.hpp` as a header-only file in namespace `viz::view`. It must be `#include`d by every draw file (`draw_dom_ladder.cpp`, `draw_match_tape.cpp`, `draw_playback_controls.cpp`, `draw_ou_controls.cpp`, `render_loop.cpp`) and by no model-layer file. The rationale: a future design pass (an ImGui design agent or consultant) must be able to restyle the entire application by editing `theme.hpp` only, without touching any draw function logic.

    **Rule enforced by code review**: no inline style literals in any draw function. Every colour (`ImVec4`), spacing value (`float`), rounding value (`float`), and font size reference in the draw files must reference a named constant from `theme.hpp`. The only permitted exceptions are `ImVec2(0, 0)` and `ImVec2(-1, -1)` layout sentinels that carry no visual meaning.

    **Minimum constant categories for Phase 1** (values are agentCPP's decision; these are the categories that must be present):

    - **Colours** (`ImVec4`): bid level row background, best bid highlight, ask level row background, best ask highlight, spread row background, buy-side fill tape entry, sell-side fill tape entry, window background, header row background.
    - **Spacing** (`float`): DOM ladder row height, match tape row height, price column width, qty column width, depth bar column width.
    - **Rounding** (`float`): window rounding, frame rounding.
    - **Alpha** (`float`): dimmed level alpha (levels far from best bid/ask), active level alpha (levels near best).
    - **Font**: `theme.hpp` declares a `ImFont* body_font` pointer (or equivalent name). The font is loaded once in `RenderLoop::init()` and the pointer is stored here or in `RenderLoop` — agentCPP's choice. What is not permitted is loading the font inside any draw function. Draw functions reference the font via the pointer; they do not call `ImGui::GetIO().Fonts->AddFontFromFileTTF()`.

    `theme.hpp` includes `imgui.h` for `ImVec4` and `ImFont`. It must not include any model-layer header.
