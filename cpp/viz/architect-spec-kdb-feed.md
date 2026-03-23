# Architecture Specification — KdbEventSource: Live KDB+/q TCP Feed

**Produced by**: agentArchitect
**Inputs**: agentContext report (locked decisions in brief), existing codebase (event_source.hpp, replay_event.hpp, sim_engine.cpp, main.cpp, CMakeLists.txt), architect-spec.md (Phase 1)
**Date**: 2026-03-23
**Target**: agentCPP (C++ implementation), agentQ (q script)
**Status**: COMPLETE — ready for handoff

---

## Scope

This spec covers one new feature only: `KdbEventSource`, a concrete `IEventSource` that receives a live push feed of raw `ReplayEvent` structs from a KDB+/q simulated exchange over a TCP socket. It also covers the associated SPSC queue header, the q publisher script, required changes to `main.cpp`, required changes to `CMakeLists.txt`, and the test target.

`IEventSource` gains one non-pure virtual method `is_live()` (default `false`). All existing implementations are unaffected. `SimEngine` gains one `bool live_source_` member used to skip virtual-clock pacing for live sources.

---

## Decision Register

| Decision | Status | Resolution / Condition |
|---|---|---|
| Wire format | LOCKED | KDB+/q sends byte vectors via `neg[h]` which adds a fixed 48-byte KDB+ IPC frame (16-byte envelope + 32-byte payload). C++ reads 48 bytes per event, discards the 16-byte envelope, and `memcpy`s the 32-byte payload into a `ReplayEvent`. No `k.h`. No IPC parsing beyond a fixed-offset `memcpy`. (agentContext P3 refined here) |
| Queue type | LOCKED | SPSC bounded ring buffer, single-header, implemented inline in `spsc_queue.hpp`. No third-party dependency added. (agentContext Q1) |
| Connection topology | LOCKED | C++ `bind`/`listen`/`accept`. KDB+ calls `hopen "::port"` outbound. KDB+ owns the clock (`.z.ts` timer drives push). (agentContext K1) |
| Backpressure policy | LOCKED | Drop-on-full. `try_push()` returns false; the frame is discarded. L2 MBP data is self-healing. (agentContext B1) |
| `reset()` semantics | LOCKED (resolved here) | No-op. `SimEngine` calls `reset()` when a finite source is exhausted; for a live source `next_event()` returns false only when the queue is momentarily empty — never due to permanent exhaustion. The no-op is safe and correct. |
| `event_count()` return value | LOCKED | Returns `SIZE_MAX`. The view layer suppresses the progress bar when `SIZE_MAX` is detected. `SimEngine` does not call `event_count()`. |
| `is_live()` method | LOCKED (resolved here) | Added to `IEventSource` as a non-pure `virtual` method returning `false`. `KdbEventSource` overrides it to return `true`. `SimEngine` caches `source_->is_live()` into `bool live_source_` at construction and uses it to skip the `sleep_for` pacing block. This is the only change to `IEventSource`. |
| TCP framing | LOCKED | 48-byte fixed reads. The KDB+ IPC frame for `neg[h] 32_byte_bvec` is always exactly 48 bytes: 8-byte IPC header + 4-byte type tag + 4-byte count field + 32-byte payload. `ReplayEvent` data sits at bytes [16..47]. C++ reads 48 bytes per frame; no dynamic length parsing required. |
| SPSC capacity | LOCKED | Fixed at compile time: `KDB_QUEUE_CAPACITY = 4096U`. Baked into `SpscQueue<ReplayEvent, KDB_QUEUE_CAPACITY>` as a member of `KdbEventSource`. `KdbEventSource` is not templated. |
| `accept_fd_` type | LOCKED | `std::atomic<int>`. Written by receive thread (after `accept()`); read by main thread in `stop()`. Must be atomic to avoid data race. |
| `listen_fd_` type | LOCKED | Plain `int`. Written only by main thread (constructor + `stop()`). Receive thread does not access `listen_fd_`. |

---

## Data Model

### Primary Types — unchanged from Phase 1

`ReplayEvent` is the queue element type and the in-memory representation. Its layout is fixed.

### Struct Layout Tables

#### `ReplayEvent` (existing — do not change)

| Field | Type | Offset | Size | Notes |
|---|---|---|---|---|
| `timestamp_ns` | `uint64_t` | 0 | 8 | Virtual clock nanoseconds from KDB+ |
| `tick` | `uint32_t` | 8 | 4 | Absolute price tick |
| `_pad` | `uint32_t` | 12 | 4 | Explicit pad; zero on wire |
| `qty` | `uint64_t` | 16 | 8 | Scaled qty (×10^8); 0 = delete level |
| `side` | `uint8_t` | 24 | 1 | 0 = BID, 1 = ASK |
| `_pad2[7]` | `uint8_t[7]` | 25 | 7 | Explicit pad; zero on wire |

`sizeof(ReplayEvent) == 32`. `alignof(ReplayEvent) == 8`.
`static_assert` already present in `replay_event.hpp`; do not add a second one.

#### KDB+ IPC Frame Layout (wire format, 48 bytes total)

| Bytes | Content | Notes |
|---|---|---|
| 0 | `0x01` | KDB+ IPC magic: little-endian encoding |
| 1 | `0x00` | Async message (1-way, no response) |
| 2 | `0x00` | Uncompressed |
| 3 | `0x00` | Reserved |
| 4–7 | `0x30 0x00 0x00 0x00` | Total message length = 48 (uint32_t LE) |
| 8–11 | `0x04 0x00 0x00 0x00` | KDB+ type 4 = byte vector; attribute byte = 0x00 |
| 12–15 | `0x20 0x00 0x00 0x00` | Count = 32 (uint32_t LE) |
| 16–47 | `<ReplayEvent bytes>` | 32 bytes of raw struct data — C++ memcpy target |

`KDB_IPC_FRAME_SIZE = 48U`. `KDB_IPC_PAYLOAD_OFFSET = 16U`.
Both constants are defined in `kdb_event_source.hpp`.

The q script uses standard `neg[h] bytes` where `bytes` is a 32-element byte vector. KDB+/q constructs the 16-byte envelope automatically. The C++ side never parses the envelope — it simply `memcpy`s from offset 16.

#### `SpscQueue<T, N>` internal layout

| Field | Location | Cache line | Notes |
|---|---|---|---|
| `head_` | offset 0, padded to 64 bytes | line 0 (consumer) | Consumer writes; producer reads for full-check |
| `tail_` | offset 64, padded to 64 bytes | line 1 (producer) | Producer writes; consumer reads for empty-check |
| `buf_[N]` | offset 128 | lines 2+ | Ring buffer storage |

`head_` and `tail_` must each be on their own cache line (64-byte alignment, padded with `char _pad[]`). This prevents false sharing between the producer thread (which writes `tail_`) and the consumer thread (which writes `head_`).

Both `head_` and `tail_` are `std::atomic<uint32_t>`. `N` must be a power of 2; enforced by `static_assert((N & (N-1U)) == 0U)` inside the template.

`sizeof(SpscQueue<ReplayEvent, 4096>)`:
- head_ slot: 64 bytes
- tail_ slot: 64 bytes
- buf_: 4096 × 32 = 131072 bytes
- Total: 131200 bytes (~128 KB)

#### `KdbEventSourceConfig`

| Field | Type | Default | Notes |
|---|---|---|---|
| `port` | `uint16_t` | `KDB_DEFAULT_PORT` (7890) | TCP port to `bind()` and `listen()` on |
| `drop_on_full` | `bool` | `true` | Always true; field exists for documentation clarity; implementation always drops |

`sizeof(KdbEventSourceConfig)`: `uint16_t` (2) + `bool` (1) + 1 byte implicit pad = 4 bytes. No `static_assert` required (not a wire type).

Named constants in `kdb_event_source.hpp`:
```
static constexpr uint16_t    KDB_DEFAULT_PORT        = 7890U;
static constexpr uint32_t    KDB_QUEUE_CAPACITY      = 4096U;
static constexpr std::size_t KDB_IPC_FRAME_SIZE      = 48U;
static constexpr std::size_t KDB_IPC_PAYLOAD_OFFSET  = 16U;
```

#### `KdbEventSource` member layout (private, for sizing reference)

| Member | Type | Notes |
|---|---|---|
| `cfg_` | `KdbEventSourceConfig` | Stored config |
| `queue_` | `SpscQueue<ReplayEvent, KDB_QUEUE_CAPACITY>` | ~128 KB; largest member |
| `listen_fd_` | `int` | Bound, listening socket fd; -1 if not open |
| `accept_fd_` | `std::atomic<int>` | Accepted connection fd; -1 until receive_loop stores it |
| `stop_flag_` | `std::atomic<bool>` | Receive thread exit signal |
| `recv_thread_` | `std::thread` | Receive thread; default-constructed until `start()` |
| `events_dropped_` | `std::atomic<uint64_t>` | Drop-on-full diagnostic counter |

No `static_assert` on `KdbEventSource` sizeof required (not a wire type; size dominated by the queue).

---

## API Boundary

There is no type conversion in this component. The only boundary is:

1. **TCP receive → queue**: `recv_exact()` reads 48 bytes into a local buffer; `memcpy` extracts the 32-byte `ReplayEvent` from offset 16; `queue_.try_push(ev)` copies into the ring buffer. No field-by-field parsing; no conversion; no casting beyond the `memcpy`.
2. **Queue → caller**: `next_event(out)` calls `queue_.front()` to get a pointer, copies to `out` via assignment, then calls `queue_.pop()`. No conversion.

The only numeric narrowing is `port` (uint16_t) passed to `htons()`, which accepts `uint16_t` — no cast needed.

**Little-endian assumption**: KDB+/q on Linux x86-64 stores integer atoms in little-endian byte order. The C++ target is Linux x86-64 (little-endian). No byte-swap is required. This assumption is documented in `kdb_event_source.hpp` and `kdb_feed.q`.

---

## Interface Specification

### `IEventSource` change — `event_source.hpp`

Add one non-pure virtual method with a default implementation:

```cpp
virtual bool is_live() const noexcept { return false; }
```

All existing implementations (`CsvEventSource`, `OUEventSource`) inherit the default `false`. `KdbEventSource` overrides it to return `true`. No changes to any existing `.cpp` file for this method.

---

### `SpscQueue<T, N>` — `cpp/viz/src/model/spsc_queue.hpp`

Single-header template. No `.cpp` file. Namespace: `viz::model`.

```
template<typename T, uint32_t N>
class SpscQueue { ... };
```

Compile-time preconditions:
- `static_assert((N & (N - 1U)) == 0U, "SpscQueue N must be power of 2")`.
- `static_assert(N >= 2U, "SpscQueue N must be at least 2")`.

#### `try_push(const T& item) -> bool`

Precondition: called only from the producer thread.
Postcondition (success): `item` is copied into `buf_[(tail_ & (N-1U))]`; `tail_` is incremented with `memory_order_release`; returns `true`.
Postcondition (full): queue is full when `(tail_val - head_val) >= N` where `head_val = head_.load(memory_order_acquire)`; returns `false`; no mutation.
Error behaviour: no exception; returns `false` on full.

Memory ordering:
- Load `head_`: `memory_order_acquire` (synchronises with `pop()`'s release store to `head_`).
- Store `tail_` increment: `memory_order_release` (synchronises with `front()`'s acquire load of `tail_`).

#### `front() -> T*`

Precondition: called only from the consumer thread.
Postcondition (non-empty): returns pointer to `buf_[(head_val & (N-1U))]`; pointer is valid until `pop()` is called; queue is not mutated.
Postcondition (empty): returns `nullptr`. Queue is empty when `head_.load(memory_order_relaxed) == tail_.load(memory_order_acquire)`.
Error behaviour: no exception; returns `nullptr` when empty.

Memory ordering:
- Load `tail_`: `memory_order_acquire` (synchronises with `try_push()`'s release store to `tail_`).
- Load `head_`: `memory_order_relaxed` (private to consumer thread).

#### `pop() -> void`

Precondition: called only from the consumer thread; `front()` must have returned non-null immediately prior. Calling `pop()` on an empty queue is undefined behaviour.
Postcondition: `head_` is incremented with `memory_order_release`, freeing the slot for producer reuse.
Error behaviour: no exception; no return value; no guard against empty (caller is responsible).

Memory ordering:
- Store `head_` increment: `memory_order_release` (synchronises with `try_push()`'s acquire load of `head_`).

#### `size_approx() -> uint32_t`

Precondition: may be called from any thread.
Postcondition: returns an approximate count in `[0, N]`. May be stale (no lock). Not guaranteed exact at the moment of return.
Error behaviour: no exception; always returns a value in `[0, N]`.

Memory ordering: both `head_` and `tail_` loaded with `memory_order_relaxed`.

**Implementation note**: index arithmetic uses `uint32_t` throughout. Mask is `N - 1U`. Full/empty condition uses unsigned subtraction `(tail_val - head_val)` which wraps correctly for `uint32_t` provided the outstanding count never exceeds `N`.

---

### `KdbEventSourceConfig` — `cpp/viz/src/model/kdb_event_source.hpp`

Plain aggregate struct. No constructor. Zero-initialise or use designated initialisers.

See named constants above. `KdbEventSourceConfig` fields:

| Field | Type | Default | Meaning |
|---|---|---|---|
| `port` | `uint16_t` | `KDB_DEFAULT_PORT` | TCP port to `bind()` and `listen()` on |
| `drop_on_full` | `bool` | `true` | Always `true`; `false` is not supported |

---

### `KdbEventSource` — `cpp/viz/src/model/kdb_event_source.hpp` / `.cpp`

Namespace: `viz::model`. Inherits `IEventSource`. `final`.

```
class KdbEventSource final : public IEventSource { ... };
```

#### Constructor: `KdbEventSource(KdbEventSourceConfig cfg)`

Precondition: `cfg.port != 0`; platform is Linux (POSIX sockets available).
Postcondition:
- A TCP socket has been created (`socket(AF_INET, SOCK_STREAM, 0)`).
- `SO_REUSEADDR` has been set on the socket.
- The socket has been bound to `INADDR_ANY:cfg.port`.
- `listen(fd, 1)` has been called (backlog 1 — one KDB+ connection expected).
- `listen_fd_` holds the valid fd.
- `accept_fd_` is `-1` (no connection yet).
- `recv_thread_` is default-constructed (not yet started).
- `stop_flag_` is `false`.
- Returns immediately; does NOT call `accept()`.

Error behaviour: if `socket()`, `bind()`, or `listen()` fails, print to `stderr` with `strerror(errno)` and call `std::terminate()`. Unrecoverable; `-fno-exceptions` requires this.

#### `start() -> void`

Precondition: constructor completed successfully; `start()` has not been called previously.
Postcondition: `recv_thread_` has been launched and is executing `receive_loop()`. Returns immediately to caller — does not block for `accept()`.
Error behaviour: `std::thread` construction failure is fatal (runtime terminates). No explicit error return.

Ordering requirement: `start()` must be called after `KdbEventSource` is constructed (which calls `listen()`) and before `SimEngine::run()` is called. KDB+ may connect at any point after `start()` returns.

#### `next_event(ReplayEvent& out) -> bool`

Precondition: called from the sim thread only (consumer side of SPSC).
Postcondition (non-empty): `out` populated from front element; element popped; returns `true`.
Postcondition (empty): `out` unchanged; returns `false`.
Error behaviour: no exception; returns `false` when queue is empty.

Does NOT spin. Calls `queue_.front()` once; if null, returns `false` immediately. The sim thread's existing 5 ms sleep on empty provides back-off.

#### `reset() -> void`

Precondition: none.
Postcondition: no state changed.
Error behaviour: none.
Rationale: rewind is meaningless for a live feed. `SimEngine` calls `reset()` when a finite source is exhausted; for `KdbEventSource`, `next_event()` returns `false` only when the queue is momentarily empty — not due to permanent exhaustion. `reset()` is a safe no-op.

#### `event_count() const -> std::size_t`

Precondition: none.
Postcondition: returns `std::numeric_limits<std::size_t>::max()`.
Error behaviour: none. Always returns `SIZE_MAX`.

#### `is_live() const noexcept -> bool`

Precondition: none.
Postcondition: returns `true`.
Error behaviour: none. `noexcept`.
Implementation: inline in class body. No `.cpp` definition.

#### `stop() -> void`

Precondition: may be called from any thread; idempotent.
Postcondition:
- `stop_flag_` is set to `true` with `memory_order_release`.
- If `accept_fd_.load(memory_order_acquire) != -1`: calls `shutdown(fd, SHUT_RDWR)` then `close(fd)`, stores `-1` to `accept_fd_`. This unblocks `recv()` in `receive_loop()`.
- If `listen_fd_ != -1`: calls `close(listen_fd_)`, sets `listen_fd_ = -1`.
- Joins `recv_thread_` if `recv_thread_.joinable()`.

Error behaviour: `shutdown()` / `close()` errors silently ignored. No exception.

Idempotency: use `stop_flag_.exchange(true, memory_order_acq_rel)` as the guard; if the exchange returns `true` (already stopped), skip fd-closing and thread-joining.

#### Destructor: `~KdbEventSource()`

Postcondition: `stop()` has been called; receive thread joined; all fds closed.
Implementation: call `stop()` unconditionally. `stop()` is idempotent.

#### Private: `receive_loop() -> void`

Receive thread entry point. Not part of the public API.

**Step 1 — accept**:
- Call `accept(listen_fd_, nullptr, nullptr)`.
- If `accept()` returns `fd >= 0`: store `accept_fd_.store(fd, memory_order_release)`.
- If `accept()` returns `-1`: check `stop_flag_`; if true, return silently; if false, print `strerror(errno)` to stderr and return (unrecoverable).

**Step 2 — receive loop**:
```
while (!stop_flag_.load(memory_order_acquire)) {
    char ipc_buf[KDB_IPC_FRAME_SIZE];   // 48 bytes
    int  fd = accept_fd_.load(memory_order_relaxed);
    if (!recv_exact(fd, ipc_buf, KDB_IPC_FRAME_SIZE)) {
        break;   // disconnect or stop
    }
    ReplayEvent ev{};
    std::memcpy(&ev, ipc_buf + KDB_IPC_PAYLOAD_OFFSET, sizeof(ReplayEvent));
    if (!queue_.try_push(ev)) {
        events_dropped_.fetch_add(1U, memory_order_relaxed);
    }
}
```

**Step 3 — cleanup**:
```
int fd = accept_fd_.exchange(-1, memory_order_acq_rel);
if (fd != -1) { close(fd); }
```

#### Private: `recv_exact(int fd, void* buf, std::size_t n) -> bool`

Precondition: `fd` is a valid open socket; `buf` points to at least `n` bytes of writable memory.
Postcondition (success): exactly `n` bytes have been written to `buf`; returns `true`.
Postcondition (failure): returns `false`; `buf` contents are indeterminate.
Error behaviour: no exception; returns `false` on disconnect, error, or stop.

Implementation:
- Loop: call `recv(fd, ptr, remaining, 0)`.
- Accumulate bytes until exactly `n` received.
- Return `false` if `recv()` returns 0 (clean disconnect) or `-1` with `errno != EINTR`.
- Return `false` if `stop_flag_.load(memory_order_acquire)` is true at the top of each iteration.
- On `errno == EINTR`: retry (signal interruption, not an error).
- `MSG_WAITALL` is NOT used (portability; explicit loop is unambiguous).

---

## TCP Framing Specification

**Protocol**: KDB+ IPC framed messages, fixed 48-byte size.

When KDB+ executes `neg[h] bytes` where `bytes` is a 32-element byte vector (`0x` of length 32), it sends a 48-byte IPC message automatically. C++ reads exactly 48 bytes per event and extracts the `ReplayEvent` from the last 32 bytes (offset 16).

**Frame structure** (see Data Model table above for full byte-by-byte layout):
```
[8 bytes IPC header][4 bytes type/attr][4 bytes count=32][32 bytes ReplayEvent data]
 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 16-byte envelope discarded by C++ (fixed offset, no parsing)
```

**Why 48-byte fixed reads**: both ends control the message size. The q script always sends a 32-element byte vector; KDB+ always produces the same envelope. A fixed read of 48 bytes requires no length-parsing, no dynamic allocation, and no envelope interpretation beyond the fixed-offset `memcpy`.

**Partial reads**: TCP is a byte stream. A single `recv()` call may return fewer than 48 bytes. The `recv_exact()` helper loops until exactly 48 bytes have been received or an error occurs. This is mandatory.

**Byte order of `ReplayEvent` payload**: KDB+/q on Linux x86-64 stores integer atoms in little-endian byte order, matching the C++ x86-64 struct layout. No byte-swap required.

---

## KDB+/q Publisher Specification (`cpp/viz/q/kdb_feed.q`)

### Purpose

Standalone KDB+/q script simulating an exchange pushing L2 MBP events to the C++ visualiser. Connects outbound to the C++ TCP listener; sends KDB+ IPC messages containing raw `ReplayEvent` byte vectors at a configurable rate.

### Configuration Variables

| Variable | Type | Default | Meaning |
|---|---|---|---|
| `PORT` | long | `7890` | TCP port of C++ listener |
| `PUSH_RATE_MS` | long | `100` | Timer interval in milliseconds (100ms = 10 events/s) |
| `MU` | float | `2000.0` | OU mean reversion level (ETH/USDT in dollars) |
| `THETA` | float | `0.005` | OU mean reversion speed per step |
| `SIGMA` | float | `10.0` | OU per-step diffusion (dollars) |
| `TICK_SIZE` | float | `0.01` | Minimum price increment (ETH/USDT = $0.01) |
| `TICKS_PER_DOLLAR` | long | `100` | Inverse of TICK_SIZE |
| `BASE_PRICE` | float | `0.0` | Tick floor; tick = floor((price - BASE_PRICE) * TICKS_PER_DOLLAR) |
| `MAX_TICK` | long | `65535` | Maximum valid tick |
| `SEED` | long | `42` | Random seed |

### Script Structure

```
1. Configuration variables
2. State variables: mid price, virtual clock, connection handle, running flag
3. Helper: .feed.randn  — Box-Muller normal random variable
4. Function: .feed.makeEvent[side;tick;qty]  — 32-byte KDB+ byte vector
5. Function: .feed.push[]  — OU step + send one event
6. Timer callback: .z.ts
7. Disconnect callback: .z.pc
8. Startup: seed RNG, connect, set running, start timer
```

### State Variables

```q
.feed.mid:.feed.mid^MU      / current mid price
.feed.vclock:0j             / virtual clock nanoseconds
.feed.h:0N                  / connection handle (null until connected)
.feed.running:0b            / flag: false after disconnect
```

### `.feed.randn[]` — Box-Muller normal random variable

```q
.feed.randn:{[] u1:1e-15|rand 1.0; u2:rand 1.0;
    sqrt[-2*log u1] * cos 2*acos[-1]*u2}
```

The `1e-15 |` guard prevents `log 0` (which would produce `-0w` in q).

### `.feed.makeEvent[side;tick;qty]` — 32-byte byte vector

Produces a 32-byte byte vector matching `ReplayEvent` wire layout. Fields are encoded little-endian using `reverse 0x0 vs x`.

**Encoding idiom**: in KDB+/q on Linux x86-64, `0x0 vs x` produces big-endian bytes; `reverse` converts to little-endian. Use `reverse 0x0 vs `long$.feed.vclock` for 8-byte fields and `reverse 0x0 vs `int$tick` for 4-byte fields.

```q
.feed.makeEvent:{[side;tick;qty]
    ts_bytes  : reverse 0x0 vs `long$.feed.vclock;      / 8 bytes LE  offset 0
    tick_bytes: reverse 0x0 vs `int$tick;               / 4 bytes LE  offset 8
    pad_bytes : 4#0x00;                                  / 4 bytes     offset 12
    qty_bytes : reverse 0x0 vs `long$qty;               / 8 bytes LE  offset 16
    side_byte : enlist `byte$side;                       / 1 byte      offset 24
    pad2_bytes: 7#0x00;                                  / 7 bytes     offset 25
    ts_bytes,tick_bytes,pad_bytes,qty_bytes,side_byte,pad2_bytes
 }
```

The result must be exactly 32 bytes. Verify: `count .feed.makeEvent[0;2000;100000000]` must equal 32.

Note: `0x0 vs `int$tick` produces 4 bytes for a 4-byte int. `0x0 vs `long$x` produces 8 bytes for a long.

### `.feed.push[]` — generate and send one event

```q
.feed.push:{[]
    / Advance virtual clock by PUSH_RATE_MS milliseconds in nanoseconds
    .feed.vclock+:PUSH_RATE_MS * 1000000j;

    / OU step
    .feed.mid+:(MU - .feed.mid) * THETA;
    .feed.mid+:SIGMA * .feed.randn[];

    / Clamp mid to valid price range
    min_price:BASE_PRICE + TICK_SIZE;
    max_price:BASE_PRICE + MAX_TICK * TICK_SIZE;
    .feed.mid:min_price | max_price & .feed.mid;

    / Convert to tick
    tick:`long$floor (.feed.mid - BASE_PRICE) * TICKS_PER_DOLLAR + 0.5;
    tick:1 | MAX_TICK & tick;

    / Random side (0=BID, 1=ASK) and qty
    side:`long$0.5 < rand 1.0;
    qty:`long$1e8 * 1 + `long$9 * rand 1.0;

    / Construct and send byte vector
    bytes:.feed.makeEvent[side;tick;qty];
    neg[.feed.h] bytes;
 }
```

### `.z.ts` — timer callback

```q
.z.ts:{[] if[.feed.running; .feed.push[]]}
```

### `.z.pc` — disconnect callback

Called when the C++ process closes the connection.

```q
.z.pc:{[h] if[h=.feed.h; .feed.running:0b; \t 0; .feed.h:0N]}
```

`\t 0` stops the timer.

### Startup sequence

```q
/ Override defaults from CLI
if[`port in key .Q.opt .z.x; PORT:`long$.Q.opt[.z.x]`port]
if[`rate in key .Q.opt .z.x; PUSH_RATE_MS:`long$.Q.opt[.z.x]`rate]

/ Seed RNG
system "S ",string SEED

/ Initialise state
.feed.mid:MU
.feed.vclock:0j
.feed.running:0b

/ Connect to C++ listener (blocks until accepted)
.feed.h:hopen `$"::",string PORT

/ Start timer and set running flag
.feed.running:1b
\t PUSH_RATE_MS
```

`hopen` will fail with connection-refused if the C++ process is not yet in `listen()` state. The C++ process must be started and have constructed `KdbEventSource` (which calls `listen()`) before running `kdb_feed.q`.

CLI usage: `q kdb_feed.q -port 7891 -rate 50`

---

## `SimEngine` Changes

### Change 1 — `is_live()` check to skip pacing (REQUIRED)

**Problem**: `SimEngine::run()` applies `sleep_for` between events proportional to `timestamp_ns` delta. For a live feed, KDB+'s `.z.ts` timer already provides pacing. Applying `sleep_for` on top would double-pace.

**`sim_engine.hpp`** — add one private member:
```cpp
bool live_source_;   // true if source is a live feed; skip sleep_for pacing
```

**`sim_engine.cpp` constructor** — add to initialiser list (after `source_(source)`):
```cpp
, live_source_(source->is_live())
```

**`sim_engine.cpp` `run()`** — wrap the pacing block:
```cpp
// ---- Virtual-clock pacing (skipped for live sources) ----
if (!live_source_ && ev.timestamp_ns > prev_event_ns_ && prev_event_ns_ != 0U) {
    // ... existing sleep_for block unchanged ...
}
```

No other changes to `SimEngine`. The `dynamic_cast<OUEventSource*>` for save-CSV will return `nullptr` for a `KdbEventSource*` and take the false branch (no save for live source) — correct behaviour, no change needed.

### Change 2 — `IEventSource::is_live()` (REQUIRED)

Add `virtual bool is_live() const noexcept { return false; }` to `IEventSource` in `event_source.hpp`. All existing implementations inherit the default `false`.

---

## `main.cpp` Changes

### New arguments: `--kdb` and `--kdb-port`

Add to argument parsing alongside the existing `--selftest` and `--ou` checks:

```cpp
bool     use_kdb  = false;
uint16_t kdb_port = viz::model::KDB_DEFAULT_PORT;

// Inside the existing for(int i ...) loop:
if (std::strcmp(argv[i], "--kdb") == 0) {
    use_kdb = true;
} else if (std::strcmp(argv[i], "--kdb-port") == 0 && i + 1 < argc) {
    long p = std::strtol(argv[i + 1], nullptr, 10);
    if (p > 0L && p <= 65535L) {
        kdb_port = static_cast<uint16_t>(p);
    }
    ++i;
}
```

`static_cast<uint16_t>(p)` is safe after the range check. `-Wconversion` requires `static_cast` here.

### Source construction

Replace the existing if/else source construction block:

```cpp
if (use_kdb) {
    viz::model::KdbEventSourceConfig kdb_cfg;
    kdb_cfg.port         = kdb_port;
    kdb_cfg.drop_on_full = true;
    auto* kdb = new viz::model::KdbEventSource(kdb_cfg);
    // Constructor calls bind() + listen(); does NOT block.
    kdb->start();   // launches receive thread; accept() blocks inside that thread
    source_owner.reset(kdb);
} else if (use_ou) {
    source_owner = std::unique_ptr<viz::model::IEventSource>(
        new viz::model::OUEventSource(viz::model::OU_DEFAULT_PARAMS,
                                      viz::model::OU_DEFAULT_EVENT_COUNT));
} else {
    auto events = viz::model::load_replay_csv(csv_path);
    source_owner = std::unique_ptr<viz::model::IEventSource>(
        new viz::model::CsvEventSource(std::move(events)));
}
```

The default (no args) remains `use_ou = true`.

**Ordering**: `kdb->start()` is called before `SimEngine` is constructed. KDB+ may connect at any point after `start()` returns (i.e., after `listen()` is called in the constructor).

**Shutdown**: `sim_engine.stop()` + `sim_thread.join()` as before. `source_owner` (unique_ptr) destroyed after join; `~KdbEventSource()` calls `stop()` — correct ordering.

### Include to add

```cpp
#include "kdb_event_source.hpp"
```

Add to the model layer include block in `main.cpp`.

---

## `CMakeLists.txt` Changes

### Add `kdb_event_source.cpp` to the `viz` target

In the `add_executable(viz ...)` block, add after the existing model sources:
```cmake
src/model/kdb_event_source.cpp
```

No new `find_package` or `target_link_libraries` entries. POSIX sockets are in `libc` on Linux. `pthread` is already linked.

### Add `test_kdb_source` target

```cmake
add_executable(test_kdb_source
    test/test_kdb_source.cpp
    src/model/kdb_event_source.cpp
)

target_include_directories(test_kdb_source PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model
)

target_link_libraries(test_kdb_source PRIVATE
    pthread
)

target_compile_options(test_kdb_source PRIVATE -fsanitize=thread)
target_link_options(test_kdb_source PRIVATE -fsanitize=thread)

add_test(NAME test_kdb_source COMMAND test_kdb_source)

set_tests_properties(test_kdb_source PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS"
)
```

`test_kdb_source` has no ImGui/ImPlot/book dependency. It tests `KdbEventSource` in isolation.

---

## Threading Contract

### Thread Ownership Table

| Resource | Owner (write) | Reader(s) | Synchronisation |
|---|---|---|---|
| `SpscQueue<> buf_[]` slots [head_..tail_) | receive thread (producer) | sim thread (consumer) | SPSC protocol: `tail_` release/acquire |
| `SpscQueue<> head_` | sim thread (consumer) | receive thread (producer, full-check) | `atomic<uint32_t>` release/acquire |
| `SpscQueue<> tail_` | receive thread (producer) | sim thread (consumer, empty-check) | `atomic<uint32_t>` release/acquire |
| `accept_fd_` | receive thread (stores fd after accept; stores -1 on close) | main thread (loads in `stop()`) | `std::atomic<int>` release/acquire |
| `listen_fd_` | main thread only (constructor + `stop()`) | none — receive_loop does not access it | Plain `int` |
| `stop_flag_` | main thread (via `stop()`) | receive thread (polls at top of recv loop) | `std::atomic<bool>` release/acquire |
| `events_dropped_` | receive thread | any (diagnostic) | `std::atomic<uint64_t>` relaxed |
| `recv_thread_` | main thread (`start()` constructs; `stop()` joins) | — | `std::thread`; join is the synchronisation |

### Invariants

- Receive thread is the sole SPSC producer. Sim thread is the sole SPSC consumer. No other thread accesses the queue.
- `accept_fd_` is `std::atomic<int>`. Receive thread stores after `accept()`; main thread loads in `stop()` to call `shutdown()`. The store (release) happens-before the load (acquire) because `stop()` is called after `start()` has returned and the receive thread has been given CPU time.
- `listen_fd_` is plain `int`. Receive thread never accesses it. Main thread accesses it only in the constructor (before `start()`) and in `stop()` (which joins before returning).
- `stop()` calls `shutdown(accept_fd_)` before joining the receive thread, which causes `recv()` in `receive_loop()` to return an error; the loop then checks `stop_flag_` (true) and exits. No deadlock.
- TSan synchronisation edge: receive thread `release`-stores to `queue_.tail_`; sim thread `acquire`-loads `queue_.tail_`. TSan tracks this correctly via `std::atomic`.

---

## Performance Contract

| Operation | Complexity | Cache tier | Notes |
|---|---|---|---|
| `try_push()` | O(1) | L1 | One atomic load (head_), one buf write, one atomic store (tail_) |
| `front()` / `pop()` | O(1) | L1 | One atomic load (tail_), one buf read, one atomic store (head_) |
| `recv_exact()` | O(1) amortised | — | Syscall; ~10 calls/s at 100ms timer rate — not a latency concern |
| `next_event()` | O(1) | L1 | `front()` + assignment + `pop()` |
| `is_live()` | O(1) | register | Returns compile-time constant `true` |
| TCP receive throughput | — | — | 480 bytes/s at 10 events/s — negligible |
| Queue fill level | — | — | Typical occupancy ~1 event at 10 events/s |

The hot path is the sim thread calling `next_event()`. At 10 events/s the queue is almost always empty and `next_event()` returns `false`; the sim thread sleeps 5 ms. No performance concern. At 1000 events/s (`\t 1`) the SPSC queue handles 48 KB/s — still trivial. The 4096-element queue provides ~6.8 minutes of headroom at 10 events/s.

---

## Module Boundaries

| Module | What it hides |
|---|---|
| `SpscQueue<T, N>` | The choice of SPSC ring buffer as the inter-thread transfer mechanism; cache-line padding strategy; memory ordering for head/tail; power-of-2 index masking. |
| `KdbEventSource` | The TCP transport; socket lifecycle (bind/listen/accept/close); 48-byte IPC frame stripping; partial-read loop; SPSC queue decoupling receive thread from sim thread; connection topology (C++ listens, KDB+ connects). |
| `kdb_feed.q` | OU process simulation; byte-vector serialisation of `ReplayEvent`; timer-driven push rate; KDB+ IPC framing (automatic via `neg[h]`); connection to C++. |

---

## Test Specification — `test/test_kdb_source.cpp`

### Purpose

Verify `KdbEventSource` end-to-end: construct, `start()`, connect (simulating KDB+), send 10 events in KDB+ IPC frame format, verify all 10 received with correct field values, verify `stop()` and destructor are clean (no hang, no leak). TSan: 0 data races.

### No KDB+ required

The test uses a raw POSIX TCP socket from the test process to simulate the KDB+ sender. The test constructs the 48-byte IPC frame manually (fixed format, specified above). No q process; no `k.h`.

### Test structure

Single `main()` function. No test framework. Output: `PASS` on success; `FAIL: reason` to stderr and return 1 on failure.

### Step-by-step test procedure

**Step 1 — construct `KdbEventSource`**:
```cpp
viz::model::KdbEventSourceConfig cfg;
cfg.port         = 17890U;   // high port to avoid conflicts
cfg.drop_on_full = true;
viz::model::KdbEventSource source(cfg);
// listen() has been called; source is in LISTENING state
```

**Step 2 — `start()`**:
```cpp
source.start();
// Receive thread launched; blocking on accept() internally
```

**Step 3 — connect from test process (simulating KDB+)**:
```cpp
int sender_fd = socket(AF_INET, SOCK_STREAM, 0);
// assert sender_fd >= 0
struct sockaddr_in addr{};
addr.sin_family      = AF_INET;
addr.sin_port        = htons(17890U);
addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
int rc = connect(sender_fd, (struct sockaddr*)&addr, sizeof(addr));
// assert rc == 0
// Allow receive_loop's accept() to complete before sending:
std::this_thread::sleep_for(std::chrono::milliseconds(50));
```

**Step 4 — send 10 events as 48-byte KDB+ IPC frames**:

Each frame is 48 bytes: a fixed 16-byte envelope followed by 32 bytes of `ReplayEvent` data.

```cpp
// 16-byte KDB+ IPC envelope (fixed for 32-byte byte vector payload)
static const uint8_t KDB_ENVELOPE[16] = {
    0x01, 0x00, 0x00, 0x00,   // bytes 0-3:  LE, async, uncompressed, reserved
    0x30, 0x00, 0x00, 0x00,   // bytes 4-7:  total length = 48 (0x30) LE
    0x04, 0x00, 0x00, 0x00,   // bytes 8-11: type=4 byte vector, attr=0
    0x20, 0x00, 0x00, 0x00    // bytes 12-15: count = 32 (0x20) LE
};

for (int i = 0; i < 10; ++i) {
    viz::model::ReplayEvent ev{};
    ev.timestamp_ns = static_cast<uint64_t>(i + 1) * 1000000ULL;
    ev.tick         = static_cast<uint32_t>(2000 + i);
    ev._pad         = 0U;
    ev.qty          = static_cast<uint64_t>(100000000) * static_cast<uint64_t>(i + 1);
    ev.side         = static_cast<uint8_t>(i % 2);
    // _pad2 zero-initialised by ev{}

    char frame[48];
    std::memcpy(frame,      KDB_ENVELOPE, 16);
    std::memcpy(frame + 16, &ev,          32);

    ssize_t sent = send(sender_fd, frame, 48, 0);
    // assert sent == 48
}
```

**Step 5 — receive and verify 10 events**:
```cpp
int received = 0;
auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
while (received < 10 && std::chrono::steady_clock::now() < deadline) {
    viz::model::ReplayEvent out{};
    if (source.next_event(out)) {
        // Verify fields
        uint64_t expected_ts  = static_cast<uint64_t>(received + 1) * 1000000ULL;
        uint32_t expected_tick = static_cast<uint32_t>(2000 + received);
        uint64_t expected_qty  = static_cast<uint64_t>(100000000)
                                 * static_cast<uint64_t>(received + 1);
        uint8_t  expected_side = static_cast<uint8_t>(received % 2);
        if (out.timestamp_ns != expected_ts ||
            out.tick         != expected_tick ||
            out._pad         != 0U ||
            out.qty          != expected_qty ||
            out.side         != expected_side) {
            std::fprintf(stderr, "FAIL: event %d field mismatch\n", received);
            return 1;
        }
        ++received;
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
if (received != 10) {
    std::fprintf(stderr, "FAIL: received %d events, expected 10\n", received);
    return 1;
}
```

**Step 6 — verify `event_count()` and `is_live()`**:
```cpp
if (source.event_count() != std::numeric_limits<std::size_t>::max()) {
    std::fprintf(stderr, "FAIL: event_count() != SIZE_MAX\n"); return 1;
}
if (!source.is_live()) {
    std::fprintf(stderr, "FAIL: is_live() returned false\n"); return 1;
}
```

**Step 7 — stop and cleanup**:
```cpp
close(sender_fd);
source.stop();   // must return without hanging
// ~KdbEventSource() is called at end of scope; must not double-join
```

The test must complete within 5 seconds total. A hang at `stop()` or at the destructor indicates a `join()` deadlock.

**Step 8 — verify `reset()` is a no-op**:
```cpp
source.reset();   // must return immediately; safe to call after stop()
```

**Step 9 — print PASS**:
```cpp
std::printf("PASS  10 events received and verified\n");
return 0;
```

### TSan requirement

The test target is compiled with `-fsanitize=thread`. TSan must report zero data races. Synchronisation edges under test:
- Receive thread `release`-stores `queue_.tail_` / test main thread `acquire`-loads via `next_event()`.
- Main thread `release`-stores `stop_flag_` / receive thread `acquire`-loads.
- Receive thread `release`-stores `accept_fd_` / main thread `acquire`-loads in `stop()`.

---

## Pre-Handoff Checklist (Idiom 9)

- [x] Data model section complete: all types, sizes, and rationale stated (`ReplayEvent` 32 bytes, KDB+ IPC frame 48 bytes with layout table, `SpscQueue` ~131200 bytes, `KdbEventSourceConfig` 4 bytes, `KdbEventSource` members enumerated)
- [x] Decision register complete: all decisions locked; no open decisions block implementation
- [x] Every public function has precondition, postcondition, and error behaviour (`try_push`, `front`, `pop`, `size_approx`, constructor, `start`, `next_event`, `reset`, `event_count`, `is_live`, `stop`, destructor; private `receive_loop` and `recv_exact` also specified)
- [x] API boundary explicitly named and documented (48-byte IPC frame → 16-byte offset `memcpy` → queue → caller; little-endian assumption documented)
- [x] Struct layout table present with sizeof values
- [x] Performance contract stated
- [x] No design decisions left for the implementation agent to make (queue capacity, IPC frame size, payload offset, framing method, byte order, `is_live()` placement, `accept_fd_` atomicity, `recv_exact` loop strategy — all locked)
- [x] No open questions that block the implementation

---

## Notes for agentCPP

1. **`spsc_queue.hpp`**: implement as a header-only template. Wrap `head_` and `tail_` each in a struct padded to 64 bytes to prevent false sharing. Use `alignas(64)` on the wrapper struct or pad with `char _pad[60]` after a 4-byte atomic. Either approach is acceptable as long as the two atomics are on separate cache lines at runtime.

2. **`kdb_event_source.cpp` includes**: `#include <sys/socket.h>`, `#include <netinet/in.h>`, `#include <unistd.h>`, `#include <cerrno>`, `#include <cstring>`, `#include <cstdio>`, `#include <limits>`, `#include "spsc_queue.hpp"`. Also add `static_assert(sizeof(viz::model::ReplayEvent) == 32U)` near the top.

3. **`accept_fd_` atomic**: `std::atomic<int>` initialised to `-1`. Use `memory_order_release` on the store in `receive_loop()` after `accept()`, and `memory_order_acquire` on the load in `stop()`.

4. **`stop()` idempotency**: use `bool already_stopped = stop_flag_.exchange(true, memory_order_acq_rel)`. If `already_stopped` is true, return immediately. Check `recv_thread_.joinable()` before `recv_thread_.join()` as a belt-and-suspenders guard.

5. **`-Wconversion` compliance**: `port` is `uint16_t` and `htons()` accepts `uint16_t` — no cast needed. `N` in `SpscQueue` is `uint32_t`; `uint32_t - uint32_t` arithmetic is clean. `std::size_t` for `recv_exact`'s `n` parameter: use `std::ptrdiff_t` or `ssize_t` for the `recv()` return value to avoid signed/unsigned comparison warnings.

6. **`-fno-exceptions` compliance**: `KdbEventSource` embeds `SpscQueue<ReplayEvent, KDB_QUEUE_CAPACITY>` directly as a member (~128 KB). Do not use `new[]` for queue storage. Callers must use `new KdbEventSource(...)` or `unique_ptr<KdbEventSource>` (not stack-allocated due to size).

7. **`main.cpp` includes**: add `#include "kdb_event_source.hpp"` to the model layer include block.

8. **`SimEngine` `dynamic_cast`**: `dynamic_cast<OUEventSource*>(source_)` returns `nullptr` for `KdbEventSource*` — takes the false branch, no save for live source. Correct. No change needed.

9. **IPC frame constants**: define `KDB_IPC_FRAME_SIZE = 48U` and `KDB_IPC_PAYLOAD_OFFSET = 16U` in `kdb_event_source.hpp` as `static constexpr std::size_t`. Use them in `receive_loop()` instead of magic numbers.

10. **`SpscQueue` default-constructible requirement**: `T buf_[N]` requires `T` to be default-constructible. `ReplayEvent` is default-constructible (all POD; zero-initialised). No issue.

11. **Port range**: `KDB_DEFAULT_PORT = 7890U` is unprivileged (>1024). If the port is in use, `bind()` fails and `std::terminate()` is called. The test uses port `17890U` to avoid conflicts.

12. **`is_live()` override**: declare inline in the class body: `bool is_live() const noexcept override { return true; }`. No `.cpp` definition needed.

---

## Notes for agentQ

1. **`0x0 vs x` byte order**: `0x0 vs 1j` produces `0x0000000000000001` (big-endian, 8 bytes). `reverse 0x0 vs 1j` produces `0x0100000000000000` (little-endian). Use `reverse 0x0 vs `long$x` for 8-byte fields and `reverse 0x0 vs `int$x` for 4-byte fields. Verify: `reverse 0x0 vs 1j` must equal `0x0100000000000000`.

2. **Sending bytes**: use `neg[h] bytes` (asynchronous send). `h bytes` (synchronous) would block waiting for a KDB+ IPC response; C++ does not implement a KDB+ IPC response. Always use `neg[]`.

3. **IPC framing**: when KDB+ executes `neg[h] bytes` where `bytes` is a 32-element byte vector, it automatically constructs a 48-byte IPC message. C++ receives all 48 bytes and extracts `bytes` from offset 16. The q script does not need to construct the envelope. The `neg[h] bytes` call is correct as-is.

4. **`hopen` format**: `hopen `$"::",string PORT` opens a KDB+ IPC connection to `localhost:PORT`. This is the standard KDB+ IPC protocol. The C++ side handles the 48-byte IPC frames this produces.

5. **Random normal**: Box-Muller defined as `.feed.randn` above. The `1e-15 |` guard on `u1` prevents `log 0`. This is the standard q idiom for normal random variables.

6. **`0x0 vs `int$tick`**: `type 0x0 vs 1i` is a 4-byte byte vector in q. Verify: `count 0x0 vs 1i` must equal 4. `count 0x0 vs 1j` must equal 8.

7. **Virtual clock units**: advance by `PUSH_RATE_MS * 1000000j` per push (milliseconds to nanoseconds). Starting at `0j` is correct; the C++ sim engine treats `timestamp_ns` as a relative virtual clock.

8. **`\t 0`**: stops the timer in `.z.pc`. Required to prevent further pushes after disconnect.

9. **Byte-vector length assertion**: add `if[32 <> count bytes; '"makeEvent returned wrong length"]` to `.feed.makeEvent` during development. Remove or comment out in production.

---

## Summary of Files to Create / Modify

| File | Action | Notes |
|---|---|---|
| `cpp/viz/src/model/spsc_queue.hpp` | CREATE | Single-header SPSC ring buffer template |
| `cpp/viz/src/model/kdb_event_source.hpp` | CREATE | Constants + `KdbEventSourceConfig` + `KdbEventSource` declaration |
| `cpp/viz/src/model/kdb_event_source.cpp` | CREATE | `KdbEventSource` implementation |
| `cpp/viz/src/model/event_source.hpp` | MODIFY | Add `virtual bool is_live() const noexcept { return false; }` |
| `cpp/viz/src/model/sim_engine.hpp` | MODIFY | Add `bool live_source_` private member |
| `cpp/viz/src/model/sim_engine.cpp` | MODIFY | Initialise `live_source_` in constructor; guard pacing block with `if (!live_source_)` |
| `cpp/viz/src/main.cpp` | MODIFY | Add `--kdb`/`--kdb-port` args; construct `KdbEventSource`; call `start()`; add include |
| `cpp/viz/CMakeLists.txt` | MODIFY | Add `kdb_event_source.cpp` to `viz` target; add `test_kdb_source` target |
| `cpp/viz/q/kdb_feed.q` | CREATE | KDB+/q simulated exchange publisher |
| `cpp/viz/test/test_kdb_source.cpp` | CREATE | TSan test; simulates KDB+ from test process using 48-byte IPC frames |
