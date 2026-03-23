# agentCPP — C++ Idioms

Canonical C++ patterns for performance-critical systems code. Sources: CppCoreGuidelines, Folly, Abseil, Bloomberg BDE, LMAX Disruptor.

---

## Idiom 1 — `std::pmr` Monotonic Buffer for Arena Allocation

Use `std::pmr::monotonic_buffer_resource` to back a container with a pre-allocated buffer. Zero heap allocation on the hot path; the buffer is allocated at construction and never grows.

```cpp
#include <memory_resource>
#include <vector>

constexpr std::size_t POOL_BYTES = 100'000 * sizeof(Record);
alignas(64) static std::byte pool_buf[POOL_BYTES];

std::pmr::monotonic_buffer_resource arena{pool_buf, sizeof(pool_buf)};
std::pmr::vector<Record> records{&arena};
records.reserve(100'000);   // pre-allocate; no further allocation on hot path
```

**Key points:**
- `alignas(64)` aligns the buffer to a cache line boundary — prevents false sharing
- `monotonic_buffer_resource` never frees individual allocations — it resets the whole resource
- For a free-list (O(1) alloc + free), use a hand-rolled arena with a `uint32_t` free stack (see agentC Idiom 1 — the pattern is identical in C++)
- `std::pmr::vector` drops in as a replacement for `std::vector` with no interface change

---

## Idiom 2 — Template Compile-Time Sizing

Use template parameters for compile-time constants that affect array sizing and loop bounds. This enables the compiler to specialise, unroll loops, and verify sizes at compile time.

```cpp
template<int N_SLOTS, int SCALE>
class IntKeyedTable {
    static constexpr int N_BITMAP_WORDS = (N_SLOTS + 63) / 64;

    std::array<Bucket, N_SLOTS> table_a{};
    std::array<Bucket, N_SLOTS> table_b{};
    uint64_t active_a[N_BITMAP_WORDS]{};
    uint64_t active_b[N_BITMAP_WORDS]{};

    static_assert(N_SLOTS > 0, "N_SLOTS must be positive");
    static_assert(N_SLOTS <= 16384, "N_SLOTS exceeds L2 budget");
};

// Exemplar: an order book instantiates as IntKeyedTable<N_TICKS, TICKS_PER_UNIT>
// where table_a = bid levels, table_b = ask levels.
```

**Key points:**
- `static_assert` enforces invariants at instantiation time, not runtime
- `std::array` has the same layout as a C array with bounds-checking in debug builds
- `constexpr` members are zero-cost — computed at compile time

---

## Idiom 3 — `static_assert` for Layout Enforcement

Every struct in the hot path must have its size asserted. A field addition or reordering that changes the layout breaks cache analysis and must be caught at compile time.

```cpp
struct Node {
    uint32_t id;
    uint32_t value;
    uint32_t next_idx;
};
static_assert(sizeof(Node) == 12, "Node layout changed");
static_assert(alignof(Node) == 4,  "Node alignment changed");

struct Bucket {
    uint32_t head_idx = UINT32_MAX;
    uint32_t tail_idx = UINT32_MAX;
    uint32_t count    = 0;
    uint32_t _pad     = 0;
};
static_assert(sizeof(Bucket) == 16, "Bucket layout changed");
```

---

## Idiom 4 — Bitmap Operations with `__builtin_ctzll` / `__builtin_clzll`

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

**Key points:**
- `#pragma GCC unroll 64` forces full unrolling when `NWORDS` is a compile-time template parameter — emits straight-line independent loads; OOO engine issues them in parallel
- `__builtin_ctzll` → `TZCNT` with `-march=native` (1 cycle)
- `__builtin_clzll` → `LZCNT` with `-march=native` (1 cycle)
- The template parameter `NWORDS` must be a compile-time constant for unrolling to work

---

## Idiom 5 — Strong Typedef for Type-System Enforcement of No-Cast Rule

Use a wrapper type to make integer types distinct at the type level. This prevents implicit mixing of tick indices, order IDs, and quantities — the compiler rejects incorrect combinations.

```cpp
template<typename Tag, typename T>
struct StrongInt {
    T value;
    explicit StrongInt(T v) : value(v) {}
    explicit operator T() const { return value; }
    bool operator==(StrongInt o) const { return value == o.value; }
    bool operator< (StrongInt o) const { return value <  o.value; }
};

using Tick    = StrongInt<struct TickTag,    uint32_t>;
using OrderId = StrongInt<struct OrderIdTag, uint32_t>;
using Qty     = StrongInt<struct QtyTag,     uint32_t>;
```

**Effect**: `table[slot.value]` — explicit extraction at the boundary; arithmetic between `SlotKey` and `ItemId` is a compile error. Enforces the no-cast rule structurally rather than by discipline.

**Note**: use only if the team discipline for the `uint32_t` approach is insufficient. Strong typedefs add verbosity. The `static_assert` approach (Idiom 3) is lighter and often sufficient.

---

## Idiom 6 — Class-Body Inline Definitions for Hot Small Functions

Define hot small functions directly in the class body (in the header). GCC always inlines class-body definitions regardless of LTO heuristics. Functions defined in a separate `.cpp` file can be refused inlining by the LTO cold-call classifier.

```cpp
// In table.h — always inlined by GCC
class IntKeyedTable {
public:
    int lowestActive() const {
        return lowestBit<N_BITMAP_WORDS>(active_a);
    }
    int highestActive() const {
        return highestBit<N_BITMAP_WORDS>(active_b);
    }
    int gap() const {
        int lo = lowestActive(), hi = highestActive();
        if (lo < 0 || hi < 0) return -1;
        return hi - lo;
    }
};
// Exemplar: an order book exposes getBestAsk() = lowestActive(ask_bitmap),
// getBestBid() = highestActive(bid_bitmap), getSpread() = gap().
```

**Rule**: any function whose inlining is required for correctness of performance measurement must be defined in the class body, not in a separate TU.

---

## Idiom 7 — Fixed-Point Price Conversion at the API Boundary

```cpp
// General pattern: convert a fixed-precision external value to an integer unit
// scale = 1 / precision (e.g. precision=0.25 → scale=4.0)
class FixedPrecisionIndex {
    double base_;
    double scale_;

public:
    explicit FixedPrecisionIndex(double base, double scale)
        : base_(base), scale_(scale) {}

    int toIndex(double value) const {
        return static_cast<int>((value - base_) * scale_ + 0.5);
    }
    double fromIndex(int idx) const {
        return base_ + idx / scale_;
    }
};
```

**Key points:**
- `+0.5` is the round-to-nearest idiom — avoids `std::round` (PLT call through math library)
- These functions are called once, at the public API entry; everything inside the hot path is integer
- The `static_cast<int>` here is the one sanctioned cast — it is the boundary conversion, not a hot-path operation
- **Exemplar**: an order book with tick size 0.25 uses `scale=4.0`; `toIndex(price)` returns the integer tick index

---

## Idiom 8 — `[[nodiscard]]` and Attribute Discipline

```cpp
[[nodiscard]] bool remove(int id);          // caller must check return value
[[nodiscard]] int  queryLow() const;        // -1 return means empty — must not be ignored
```

Use `[[likely]]` / `[[unlikely]]` for branch prediction hints on known-rare paths:

```cpp
if (__builtin_expect(free_top == 0, 0)) {   // GCC idiom
    handle_arena_exhausted();
}
// or C++20:
if (qty == 0) [[unlikely]] { return; }
```

---

## Idiom 9 — Build Configuration for Correctness vs Performance

```makefile
# Correctness tests — no LTO, sanitizers on
CXXFLAGS_TEST = -std=c++17 -O2 -march=native -Wall -Wextra \
                -Wconversion -Wsign-conversion -Werror \
                -fno-exceptions \
                -fsanitize=undefined,address

# Benchmarks — LTO on, sanitizers off
CXXFLAGS_BENCH = -std=c++17 -O2 -march=native -Wall -Wextra \
                 -Wconversion -Wsign-conversion -Werror \
                 -fno-exceptions \
                 -flto
```

**Rule**: tests build without `-flto`. LTO can eliminate benchmark calls (if return values are discarded) and mask correctness bugs through inlining. `-fsanitize` must be off for benchmarks — it adds significant overhead that distorts cycle counts.

---

## Idiom 10 — Benchmark Sink Pattern

Prevent the compiler from eliminating benchmark calls whose return values are unused:

```cpp
volatile double sink = 0.0;

for (int i = 0; i < N; ++i) {
    sink += table.lowestActive() + table.highestActive() + table.gap();
}
if (sink == -3.0) std::abort();  // prevent sink from being optimised away
```

**Key points:**
- `volatile` on `sink` forces every accumulation to memory — prevents loop elimination
- The `if (sink == -3.0)` guard makes `sink` observable without adding a branch to the measured hot path (the probability is essentially zero)
- Without this pattern, LTO may determine that query function return values are unused and eliminate the calls entirely — producing a 0-cycle benchmark artifact
