# agentTest — Testing Idioms

Canonical patterns for testing performance-critical systems code in C and C++.

---

## Idiom 1 — Invariant Check Function

Write a dedicated invariant checker and call it after every mutating operation in tests. It verifies the internal consistency of the data structure — not just the return values of operations.

```c
/* Returns 1 if all invariants hold, 0 + prints failure otherwise */
static int check_invariants(const table_t *t) {
    for (int slot = 0; slot < MAX_KEYS; ++slot) {
        int bit = bitmap_test(t->active, slot);
        int count = queue_count(&t->slots[slot]);

        if (bit && count == 0) {
            fprintf(stderr, "INVARIANT FAIL: bit set but queue empty at slot %d\n", slot);
            return 0;
        }
        if (!bit && count > 0) {
            fprintf(stderr, "INVARIANT FAIL: bit clear but queue non-empty at slot %d\n", slot);
            return 0;
        }
    }
    return 1;
}

/* Usage in tests */
add_item(&table, key, value);
assert(check_invariants(&table));

remove_item(&table, id);
assert(check_invariants(&table));
```

---

## Idiom 2 — Test Naming Convention: Operation_State_ExpectedResult

Name tests to encode the operation being tested, the initial state, and the expected result. This makes failures self-documenting.

```
test_add_to_empty_slot          — add first item to a slot
test_add_to_existing_slot       — add second item to a slot with one item
test_remove_only_item           — remove item; slot should become empty
test_remove_nonexistent_id      — remove ID never inserted; should return false
test_remove_already_removed     — double-remove; should return false, not corrupt
test_add_crosses_capacity       — fill pool to maximum; next add should fail cleanly
test_bitmap_cleared_on_drain    — remove last item from slot; bitmap bit must clear
```

---

## Idiom 3 — RDTSC Benchmark Harness (C)

```c
#include <stdint.h>
#include <stdio.h>

static inline uint64_t rdtsc_start(void) {
    uint32_t hi, lo;
    __asm__ volatile ("cpuid\n\trdtsc\n\t"
                      "mov %%edx,%0\n\tmov %%eax,%1"
                      : "=r"(hi), "=r"(lo) :: "%rax","%rbx","%rcx","%rdx");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_end(void) {
    uint32_t hi, lo;
    __asm__ volatile ("rdtscp\n\t"
                      "mov %%edx,%0\n\tmov %%eax,%1\n\tcpuid"
                      : "=r"(hi), "=r"(lo) :: "%rax","%rbx","%rcx","%rdx");
    return ((uint64_t)hi << 32) | lo;
}

#define BENCH_N 500000

void bench_add(table_t *t) {
    /* warm up */
    for (int i = 0; i < 1000; ++i) exercise(t);

    uint64_t t0 = rdtsc_start();
    volatile int sink = 0;
    for (int i = 0; i < BENCH_N; ++i)
        sink += run_operation(t);
    uint64_t t1 = rdtsc_end();
    (void)sink;

    printf("add_no_cross: %.1f cycles/op\n", (double)(t1 - t0) / BENCH_N);
}
```

**Key points:**
- Warm-up loop before measurement — primes branch predictor and cache
- `volatile sink` prevents the compiler from eliminating the measured calls
- Median of 3+ runs more reliable than a single measurement
- Pin to one core: `taskset -c 0 ./bench`

---

## Idiom 4 — Separate Build Targets for Correctness vs Performance

```makefile
# Correctness — sanitizers on, LTO off
test: CFLAGS += -fsanitize=undefined,address -fno-lto
test: tests.c implementation.c
	$(CC) $(CFLAGS) -o $@ $^
	./$@

# Benchmarks — sanitizers off, LTO on
bench: CFLAGS += -flto -fno-sanitize=all
bench: bench.c implementation.c
	$(CC) $(CFLAGS) -o $@ $^
	./$@
```

Never mix sanitizer and benchmark builds. Sanitizer overhead (2–10×) makes benchmark numbers meaningless.

---

## Idiom 5 — Boundary Value Table

For integer-keyed structures, test the exact boundaries explicitly:

```c
static void test_boundary_values(void) {
    table_t t;
    table_init(&t);

    /* Key = 0 — lowest valid */
    assert(insert(&t, 0, 42) == 1);
    assert(lookup(&t, 0) == 42);
    assert(check_invariants(&t));

    /* Key = MAX_KEYS - 1 — highest valid */
    assert(insert(&t, MAX_KEYS - 1, 99) == 1);
    assert(lookup(&t, MAX_KEYS - 1) == 99);
    assert(check_invariants(&t));

    /* Key = MAX_KEYS — out of bounds; must not corrupt */
    assert(insert(&t, MAX_KEYS, 0) == 0);
    assert(check_invariants(&t));
}
```

---

## Idiom 6 — Fill-Then-Drain Sequence

The most revealing test sequence for a FIFO queue or keyed table: fill it to capacity, then drain it completely. Verifies pool management, bitmap state, and ID index consistency across a full lifecycle.

```c
static void test_fill_and_drain(void) {
    table_t t;
    table_init(&t);

    /* Fill to capacity */
    for (int i = 0; i < POOL_CAPACITY; ++i) {
        assert(insert(&t, i % MAX_KEYS, i) == 1);
        assert(check_invariants(&t));
    }

    /* Drain completely */
    for (int i = 0; i < POOL_CAPACITY; ++i) {
        assert(remove_by_id(&t, i) == 1);
        assert(check_invariants(&t));
    }

    /* Table must be fully empty */
    for (int slot = 0; slot < MAX_KEYS; ++slot)
        assert(queue_count(&t.slots[slot]) == 0);
    assert(bitmap_all_clear(t.active));
}
```

---

## Idiom 7 — Cross Test: Operation Ordering Must Not Affect Correctness

For systems that support add and remove, verify that interleaved operations produce correct results regardless of order.

```c
static void test_interleaved_ops(void) {
    table_t t;
    table_init(&t);

    /* Insert A, Insert B, Remove A, Insert C, Remove B, Remove C */
    int id_a = insert(&t, 10, 100); assert(check_invariants(&t));
    int id_b = insert(&t, 10, 200); assert(check_invariants(&t));
    assert(remove_by_id(&t, id_a));  assert(check_invariants(&t));
    int id_c = insert(&t, 10, 300); assert(check_invariants(&t));
    assert(remove_by_id(&t, id_b));  assert(check_invariants(&t));
    assert(remove_by_id(&t, id_c));  assert(check_invariants(&t));

    /* Slot must be empty */
    assert(queue_count(&t.slots[10]) == 0);
    assert(!bitmap_test(t.active, 10));
}
```

---

## Idiom 8 — Fixed-Point Conversion Round-Trip Test

For any system using fixed-precision integer representation, verify the boundary conversion is exact for all valid inputs.

```c
static void test_conversion_roundtrip(void) {
    /* All valid ES tick prices round-trip exactly */
    double base = 5500.0;
    double scale = 4.0;  /* 0.25 tick size */

    for (int tick = 0; tick < 8800; ++tick) {
        double price = base + tick * 0.25;
        int   recovered = value_to_int(price, base, scale);
        assert(recovered == tick);
    }
}

static void test_conversion_known_values(void) {
    /* Hand-calculated expected values — not derived from the implementation */
    assert(value_to_int(5500.00, 5500.0, 4.0) == 0);
    assert(value_to_int(5500.25, 5500.0, 4.0) == 1);
    assert(value_to_int(5501.00, 5500.0, 4.0) == 4);
    assert(value_to_int(5600.00, 5500.0, 4.0) == 400);
}
```

---

## Idiom 9 — Test Output Format

Each test function prints one line: PASS or FAIL with the test name. A test runner calls all tests and reports a summary. No framework required.

```c
#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    test_##name(); \
    printf("PASS  " #name "\n"); \
} while(0)

/* Each test uses assert() — failure prints location and aborts */

int main(void) {
    RUN(add_to_empty_slot);
    RUN(remove_only_item);
    RUN(remove_nonexistent_id);
    RUN(double_remove);
    RUN(fill_and_drain);
    RUN(interleaved_ops);
    RUN(conversion_roundtrip);
    RUN(boundary_values);

    printf("\nAll tests passed.\n");
    return 0;
}
```

**Key points:**
- `assert()` is sufficient — it prints file and line on failure and aborts
- No external test framework needed for C
- Tests never modify their expected values to match the implementation
- Test file is never modified once written — it is the authoritative invariant

---

## Idiom 10 — Benchmark Validity Checklist

Before reporting a benchmark result, verify:

- [ ] Return values accumulated into a `volatile` sink
- [ ] Sink guard present (`if (sink == IMPOSSIBLE_VALUE) abort()`)
- [ ] Warm-up loop run before measurement
- [ ] At least 100,000 iterations measured
- [ ] Median of 3+ runs recorded, not a single run
- [ ] Binary inspected with `-S` or `objdump` to confirm the measured call is present
- [ ] Build flags confirmed: `-O2 -march=native -flto`, no sanitizers
- [ ] CPU pinned (`taskset -c 0`) and frequency scaling disabled
- [ ] Cache regime documented: warm (OU walk) or cold (uniform distribution)
