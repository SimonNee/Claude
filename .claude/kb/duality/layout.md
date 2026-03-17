# Memory Layout Reference

How data is arranged in memory determines cache behaviour. The compiler controls
some of this automatically; the rest is your responsibility.

---

## Struct Padding and Natural Alignment

The compiler inserts padding so each member is aligned to its natural alignment
(typically its own size, up to 8 bytes on x86-64).

```cpp
struct Bad {
    char   a;    // 1 byte
                 // 7 bytes padding (double needs 8-byte alignment)
    double b;    // 8 bytes
    char   c;    // 1 byte
                 // 7 bytes padding (struct size must be multiple of largest member)
    // sizeof(Bad) = 24 bytes — 15 bytes are padding
};

struct Good {
    double b;    // 8 bytes
    char   a;    // 1 byte
    char   c;    // 1 byte
                 // 6 bytes padding
    // sizeof(Good) = 16 bytes — 6 bytes padding
};
```

**Rule:** declare members largest to smallest to minimise padding.

Use `static_assert(sizeof(T) == expected)` to catch accidental layout changes:
```cpp
static_assert(sizeof(Order) == 24, "Order layout changed");
```

---

## The Order Struct (Iteration 1)

```cpp
struct Order {
    int    id;        // 4 bytes, offset 0
                      // 4 bytes padding (double needs 8-byte align)
    double price;     // 8 bytes, offset 8
    double quantity;  // 8 bytes, offset 16
    Side   side;      // 4 bytes (enum, underlying int), offset 24
                      // 4 bytes padding
    // sizeof(Order) = 32 bytes
};
```

Reordered for tighter packing:

```cpp
struct Order {
    double price;     // 8 bytes, offset 0
    double quantity;  // 8 bytes, offset 8
    int    id;        // 4 bytes, offset 16
    Side   side;      // 4 bytes, offset 20
    // sizeof(Order) = 24 bytes — saves 8 bytes (25%) per order
};
```

At 1M orders, that is 8MB saved. More importantly, 24 bytes means ~2.6 orders
per cache line instead of 2 — better packing density during iteration.

---

## alignas

`alignas(N)` forces a type or variable to be aligned to N bytes. Use it when:

1. **SIMD requires it** — SSE2 loads/stores need 16-byte alignment; AVX needs 32.
   `alignas(32)` on a float array enables AVX operations.
2. **False sharing prevention** — pad a hot variable to 64 bytes so it occupies
   its own cache line in multi-threaded code.

Do **not** use it to "improve cache performance" on a struct that does not need
it. See pitfalls.md Pitfall 4.

```cpp
// Correct use — SIMD array
alignas(32) float prices[8];   // AVX operates on 8 floats at once

// Incorrect use — wastes 40 bytes per Order
alignas(64) struct Order { ... };  // 24 bytes data, 40 bytes wasted
```

---

## Array of Structs (AoS) vs Struct of Arrays (SoA)

**AoS** — one array, each element is the full struct:
```cpp
std::vector<Order> orders;
// Memory: [price|qty|id|side][price|qty|id|side][price|qty|id|side]...
```

**SoA** — separate arrays, one per field:
```cpp
struct OrdersSoA {
    std::vector<double> prices;
    std::vector<double> quantities;
    std::vector<int>    ids;
    std::vector<Side>   sides;
};
// Memory: [price][price][price]... [qty][qty][qty]... [id][id][id]...
```

| Pattern | AoS wins | SoA wins |
|---------|----------|----------|
| Process all fields of one order | ✓ — all fields in same cache line | ✗ — must load from 4 arrays |
| Scan prices across all orders | ✗ — strides over non-price fields | ✓ — sequential price array, SIMD-friendly |
| Match one order (read price+qty+side) | ✓ | ✗ |
| Find best price in level array | ✗ | ✓ |

**For the orderbook matching loop:** AoS is correct — each order is processed as
a unit (price, qty, and side are all read together). SoA becomes relevant only
in Iteration 4 (SIMD price level scan), where prices are scanned independently.

---

## PriceLevel Layout (Iteration 2 Target)

```cpp
struct PriceLevel {
    double             price;   // 8 bytes — the key
    std::vector<Order> orders;  // 24 bytes (ptr + size + capacity)
    // sizeof(PriceLevel) = 32 bytes
};
```

A `std::vector<PriceLevel>` gives contiguous level metadata with each level's
orders stored in a separate heap allocation. This is still one indirection to
reach an order, but the level scan is now sequential (prefetcher-friendly).

For Iteration 4, the price field can be extracted into its own contiguous array
(SoA split) to enable SIMD comparison across all levels simultaneously.

---

## Measuring Layout

```cpp
#include <cstddef>
#include <iostream>

// Print field offsets and struct size
std::cout << "sizeof(Order)       = " << sizeof(Order)            << "\n";
std::cout << "offsetof price      = " << offsetof(Order, price)   << "\n";
std::cout << "offsetof quantity   = " << offsetof(Order, quantity) << "\n";
std::cout << "offsetof id         = " << offsetof(Order, id)      << "\n";
std::cout << "offsetof side       = " << offsetof(Order, side)    << "\n";
```

Run this whenever the struct changes. Unexpected padding is a bug in struct
design, not in the compiler.
