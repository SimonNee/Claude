# agentCPP — C++ Pitfalls

Systematic errors in C++, especially hidden costs in performance-critical code. Sources: CppCoreGuidelines, Herb Sutter's GotW, Scott Meyers' Effective C++, cppreference, GCC/Clang documentation.

---

## Pitfall 1 — `std::map` Node Allocation and `divq` in `unordered_map`

`std::map` allocates each node separately on the heap — one `new` per insert, one `delete` per erase. Every lookup pointer-chases through the tree. At M=200 active items, that is 200 scattered heap allocations with no spatial locality.

`std::unordered_map` (libstdc++) uses prime bucket counts via `_Prime_rehash_policy`. The modulo operation to find a bucket cannot be strength-reduced to a bitmask — it emits `divq` (64-bit divide, 35–90 cycles, non-pipelined). Additionally, each bucket entry is separately heap-allocated (`operator new` per node, ~50–100 cycles, L1/L2 polluting).

**The cost is invisible from C++ source.** You must inspect `-S` output to see it.

**Fix**: for bounded integer key spaces, use a flat array indexed directly by key. For unbounded key spaces, use a hand-rolled open-addressing hash map with power-of-two bucket count (bitmask modulo, O(1) with no `divq`).

---

## Pitfall 2 — `std::deque` Chunk Overhead

`std::deque` allocates memory in fixed-size chunks (512 bytes in libstdc++). Even a deque containing one element allocates a 512-byte chunk. At M=200 queues, that is 100 KB in chunk metadata before any element data is stored. `push_front` and `pop_front` are O(1) amortised but involve chunk pointer indirection.

**Fix**: intrusive singly-linked list with a pre-allocated arena. No chunk overhead; O(1) push/pop with direct index arithmetic.

---

## Pitfall 3 — `std::optional` Size and Branch Cost

`std::optional<T>` stores a `bool` engaged flag alongside T. This inflates the size: `sizeof(optional<T>) = sizeof(T) + padding_to_alignment`. For `optional<uint32_t>` this is typically 8 bytes, not 4.

More critically, every access to the contained value involves a branch (`if (engaged_)`). In a struct array indexed by order ID, replacing `optional<OrderLocation>` with a plain struct + sentinel value removes this branch and reduces element size. A 24-byte `optional<OrderLocation>` becomes a 16-byte plain struct — the stride change converts a multiply (for 24-byte stride) to a shift (for 16-byte, power-of-two stride).

**Fix**: use a sentinel value (e.g. `UINT32_MAX` for an index field) to represent the "not present" state. Enforce with `static_assert(sizeof(T) == N)`.

---

## Pitfall 4 — Virtual Dispatch on Hot-Path Structs

A class with any `virtual` function has a vtable pointer as its first member (8 bytes on 64-bit). This inflates every instance and every cache line. Virtual dispatch involves a pointer dereference to the vtable, then a second pointer dereference to the function — two cache misses on a cold call.

For hot-path structs stored in arrays and processed in tight loops, this is never correct. Use templates or function pointers if runtime polymorphism is genuinely needed.

---

## Pitfall 5 — Exception Handling Overhead

Even with `-fno-exceptions`, EH table metadata is emitted for functions that could throw. With exceptions enabled (the default), every non-trivial destructor on any live object adds unwinding table entries that bloat the binary and pollute instruction cache.

For performance-critical systems code, exceptions on the critical path are never appropriate. Use `-fno-exceptions` for hot-path translation units. Return error codes or use `[[nodiscard]]` result types.

---

## Pitfall 6 — `std::function` Overhead

`std::function` stores a type-erased callable with a virtual dispatch table. Even for a simple lambda, `std::function` may heap-allocate the closure if it exceeds the small-buffer optimisation threshold. Every call involves an indirect call through the type-erased wrapper.

**Fix**: use function pointers (`void (*f)(void*)`) or template parameters (`template<typename F>`) for callbacks on the hot path. `std::function` is appropriate for non-hot-path configurability only.

---

## Pitfall 7 — Inadvertent Copies

C++ copy semantics are silent. Passing a `std::vector` or `std::string` by value makes a deep copy. Returning a `std::vector` without NRVO/RVO guaranteed makes a copy.

```cpp
void process(std::vector<Order> orders);     // copies the entire vector
void process(const std::vector<Order>& orders);  // no copy
```

**Detection**: `-Weffc++` or Clang's `-Wunused-result`. For performance code, every function signature must be reviewed for pass-by-value of non-trivial types.

---

## Pitfall 8 — LTO Cold-Call Refusal for Inlining

With `-flto`, GCC inlines cross-TU functions based on a profile estimate of call frequency. Functions classified as "cold" may be refused inlining even with `inline` keyword. Functions defined in a separate `.cpp` file and called from a benchmark harness can be classified cold if the LTO profile is not available.

**Effect**: a function that should be inlined is called via a PLT stub, adding 4 callee-saved register push/pop pairs and a call/ret overhead. This has been observed to produce >50% overhead on small frequently-called functions (e.g. query functions in high-throughput data structures).

**Fix**: define hot small functions in the class body (header-defined) — GCC always inlines class-body definitions regardless of LTO heuristics. Or use `__attribute__((always_inline))` with measured justification.

---

## Pitfall 9 — `std::vector` Growth Reallocation

`std::vector::push_back` triggers reallocation when capacity is exceeded — a copy of all elements to a new allocation, O(n). In systems with a pre-known maximum element count, `reserve(N)` at construction eliminates all reallocations.

More importantly: for FIFO queues, `std::vector` is the wrong container even with `reserve`. `pop_front` is O(n) (memmove). Use a head-index advancing past consumed entries, or a singly-linked list from a pre-allocated pool. **Exemplar**: inner queues per slot in a flat-array keyed structure (e.g. an order book's per-level order queue).

---

## Pitfall 10 — Implicit Conversion in Template Code

Template type deduction can introduce silent promotions:

```cpp
template<typename T>
T add(T a, T b) { return a + b; }

uint32_t x = add(1u, 2);   // deduction fails: 1u is uint32_t, 2 is int — type mismatch
uint32_t y = add<uint32_t>(1u, 2);  // 2 promoted to uint32_t — silent, correct
```

With `-Wconversion`, mixed-type arithmetic in templates will warn. Do not silence warnings with casts — fix the call site to use consistent types.

---

## Pitfall 11 — Iterator Invalidation

`std::vector` iterators and pointers to elements are invalidated by any operation that causes reallocation (`push_back` past capacity, `insert`, `erase`). Storing a pointer into a vector element and then modifying the vector is undefined behaviour.

**Fix**: store indices, not pointers, into container elements. Indices remain valid after reallocation (the element's logical position is unchanged even if its address changes). A location index should store `(slot, item_idx)` as integers, not `(slot, Item*)` as a pointer.

---

## Pitfall 12 — `#pragma GCC unroll` Interaction with Loop Structure

`#pragma GCC unroll N` unrolls the immediately following loop N times. It only applies when the loop count is known at compile time (or can be bounded at compile time via template parameters). Applying it to a loop with a runtime-variable bound has no effect.

For bitmap scanning loops where `N_BITMAP_WORDS` is a compile-time template parameter, `#pragma GCC unroll 64` forces full unrolling regardless of N — the compiler emits a straight-line sequence of independent loads, enabling out-of-order parallel execution. Without unrolling, GCC may emit a runtime loop with `incq`/`cmpq`/`jne` that creates a serial dependency chain.

---

## Pitfall 13 — Compiler Flag Discipline

Minimum flags for performance-critical C++:

```
-std=c++17
-O2 -march=native
-Wall -Wextra
-Wconversion -Wsign-conversion
-fno-exceptions (for hot-path TUs)
-flto (benchmark binaries only — not correctness tests)
-Werror
```

`-march=native` is required for `__builtin_ctzll`/`__builtin_clzll` to compile to `TZCNT`/`LZCNT` rather than `BSF`/`BSR` + zero guard.

`-flto` on test builds can mask correctness bugs by inlining and eliminating dead code. Build tests without `-flto`; add it only to benchmark binaries.
