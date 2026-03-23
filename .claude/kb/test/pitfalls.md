# agentTest — Testing Pitfalls

Systematic errors in testing performance-critical systems code. A test that passes but does not verify correctness is worse than no test — it provides false confidence.

---

## Pitfall 1 — Circular Tests (Testing the Implementation With Itself)

A test that verifies output by recomputing it with the same algorithm as the implementation proves nothing. If the implementation is wrong, the test will also produce the wrong expected value and pass.

```c
// BAD: expected value computed the same way as the implementation
int expected = compute_tick(price, base, scale);  // same formula as the function under test
assert(price_to_tick(price, base, scale) == expected);

// GOOD: expected value derived independently
// For tick size 0.25, price 5500.25, base 5500.0:
// tick = (5500.25 - 5500.0) * 4.0 = 1 — computed by hand
assert(price_to_tick(5500.25, 5500.0, 4.0) == 1);
```

**Rule**: expected values in tests must be derived independently of the implementation. Use hand-calculated constants, reference tables, or a known-correct reference implementation.

---

## Pitfall 2 — Testing the Happy Path Only

A function that works for typical inputs may fail at boundaries. The happy path test passes; the boundary case is the bug.

**Always test:**
- Empty input (zero elements, empty queue, empty book)
- Single element (off-by-one errors surface here)
- Maximum valid input (boundary of pre-allocated pool, maximum key value)
- Values that differ by exactly one unit (adjacent tick levels, ID=0 vs ID=1)
- Operations on the same element twice (cancel twice, insert duplicate ID)

---

## Pitfall 3 — Tests That Pass Because of Undefined Behaviour

UB can make tests pass on one platform/build and fail on another. Common sources:
- Signed integer overflow used as a sentinel (e.g. `INT_MAX + 1` wrapping to `INT_MIN`)
- Uninitialized memory that happens to be zero in debug builds
- Out-of-bounds array access that hits valid memory in the test binary layout

**Rule**: always run the test suite with `-fsanitize=undefined,address`. A test that passes without sanitizers but fails with them was never correct.

---

## Pitfall 4 — Benchmark Tests That Measure Nothing (LTO Elimination)

A benchmark that measures 0 cycles is not fast — it was eliminated by the compiler. If a function's return value is discarded, the optimiser removes the call entirely under LTO.

```c
// BAD: return value discarded — compiler eliminates the call
for (int i = 0; i < N; ++i)
    get_best(table);

// GOOD: accumulate into a volatile sink
volatile int sink = 0;
for (int i = 0; i < N; ++i)
    sink += get_best(table);
if (sink == INT_MIN) abort();  // prevent sink elimination
```

**Rule**: before trusting a benchmark result of 0 or suspiciously few cycles, confirm the call is present in the `-S` assembly output.

---

## Pitfall 5 — Benchmarking Cold Cache

A benchmark that runs the same operation N times in a tight loop measures warm-cache performance. Production workloads may have a cold cache. These are different numbers and should be reported separately if both matter.

The OU (Ornstein-Uhlenbeck) price walk used in financial benchmarks naturally keeps the working set warm — prices cluster near mid, so only a few dozen slots are active. A uniform-distribution benchmark forces the full array into the working set and reveals L2/L3 transition costs.

**Rule**: always document which cache regime the benchmark represents. A warm-cache number is not a production throughput number unless the production workload is also cache-warm.

---

## Pitfall 6 — Sanitizer Builds Used for Benchmarking

`-fsanitize=address,undefined` adds significant overhead (2–10× for ASan, 1.5–3× for UBSan). Benchmark numbers from sanitizer builds are not representative of production performance.

**Rule**: correctness tests run with sanitizers; benchmark builds never do. Use separate build targets.

---

## Pitfall 7 — Testing Properties Instead of Specific Values

Tests that check relative properties ("result A is less than result B") rather than absolute values can mask bugs where both A and B are wrong but still satisfy the relationship.

```c
// BAD: property test — passes even if both values are wrong
assert(get_best_ask(book) > get_best_bid(book));

// GOOD: absolute test against known values
add_order(book, BID, 100, 10);  // bid at tick 100
add_order(book, ASK, 105, 10);  // ask at tick 105
assert(get_best_bid(book) == 100);
assert(get_best_ask(book) == 105);
assert(get_spread(book) == 5);
```

**Exception**: property-based testing (invariant testing) is valuable as a supplement to absolute tests, not as a replacement. An invariant test like "spread is always non-negative" catches classes of bugs that specific-value tests miss.

---

## Pitfall 8 — Not Testing Cancellation of Non-Existent or Already-Cancelled Items

Cancel-by-ID is a common source of bugs. Tests that only cancel items that exist miss:
- Cancel of an ID that was never inserted (out-of-bounds or invalid sentinel handling)
- Cancel of an ID that was already cancelled (double-cancel — should be a no-op or error, never a corruption)
- Cancel of an ID that was filled (should be rejected cleanly)

**Rule**: for any system with a cancel/remove operation, write explicit tests for invalid-ID cancel and double-cancel before any other cancel tests.

---

## Pitfall 9 — Not Verifying the Invariant After Every Operation

A data structure can pass individual operation tests while its internal state is corrupt in a way that only manifests later. After each operation in a test sequence, verify the full invariant:

For a FIFO queue: `count == actual_nodes_reachable_from_head`.
For a bitmap + array: `bitmap_bit_set(slot) ↔ queue_count(slot) > 0`.
For an ID index: `id_index_live(id) ↔ item_exists_in_queue(id)`.

**Rule**: write an `invariant_check()` function and call it after every mutating operation in tests. This is cheap in tests; do not ship it in production code.

---

## Pitfall 10 — RDTSC Measurement Without Serialisation

`RDTSC` counts CPU cycles but the CPU can reorder it relative to the instructions being measured. Without serialisation, the measured interval may include unrelated instructions or exclude part of the measured code.

```c
// Correct RDTSC pattern with serialisation
static inline uint64_t rdtsc_start(void) {
    uint32_t hi, lo;
    __asm__ volatile ("cpuid\n\t"    /* serialise */
                      "rdtsc\n\t"
                      "mov %%edx, %0\n\t"
                      "mov %%eax, %1\n\t"
                      : "=r"(hi), "=r"(lo)
                      :: "%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_end(void) {
    uint32_t hi, lo;
    __asm__ volatile ("rdtscp\n\t"   /* implicitly serialises on the read side */
                      "mov %%edx, %0\n\t"
                      "mov %%eax, %1\n\t"
                      "cpuid\n\t"
                      : "=r"(hi), "=r"(lo)
                      :: "%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)hi << 32) | lo;
}
```

**Rule**: always use `CPUID` + `RDTSC` at the start and `RDTSCP` + `CPUID` at the end. Plain `RDTSC`/`RDTSC` pairs can be reordered by the CPU and produce spurious low readings.

---

## Pitfall 11 — Single-Run Benchmark Results

A single benchmark run is not a measurement — it is a sample. CPU frequency scaling, OS scheduling, branch predictor warm-up, and cache state all affect the result.

**Rule**:
- Run N iterations (minimum 100,000 for sub-microsecond operations)
- Report median, not mean — the mean is dominated by outliers (OS preemptions, TLB misses)
- Run the benchmark at least 3 times and confirm stability across runs
- Pin the benchmark to a single CPU core (`taskset -c 0`) to eliminate migration noise
- Disable frequency scaling (`cpupower frequency-set -g performance`) for reproducible results

---

## Pitfall 12 — Tests That Depend on Insertion Order of a Non-Ordered Structure

If a data structure does not guarantee insertion order in its iteration, tests that assume a specific traversal order are fragile.

For FIFO queues: traversal order IS defined (head to tail). Tests may rely on it.
For hash maps or bitmaps: iteration order is not insertion order. Tests must not assume it.

**Rule**: document the iteration order guarantee of every data structure. Tests that rely on order must verify the order guarantee exists.
