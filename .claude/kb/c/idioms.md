# agentC — C Idioms

Canonical C patterns for performance-critical systems code. Sources: Linux kernel coding style, SQLite internals, Redis source, QuantCup C reference, SEI CERT C.

---

## Idiom 1 — Arena Allocator (Pre-Allocated Pool + Free List)

Eliminate `malloc`/`free` from the hot path entirely. Pre-allocate a fixed pool at startup; manage availability with a free list (LIFO stack of available indices).

```c
#define POOL_CAPACITY 100000

typedef struct {
    uint32_t id;
    uint32_t value;
    uint32_t next_idx;   /* index into pool; UINT32_MAX = end of list */
} node_t;

static_assert(sizeof(node_t) == 12, "node_t layout changed");

/* Pool */
static node_t pool[POOL_CAPACITY];

/* Free list: stack of available indices */
static uint32_t free_stack[POOL_CAPACITY];
static uint32_t free_top = 0;

void pool_init(void) {
    for (uint32_t i = 0; i < POOL_CAPACITY; ++i)
        free_stack[i] = i;
    free_top = POOL_CAPACITY;
}

static inline uint32_t pool_alloc(void) {
    return free_stack[--free_top];   /* O(1), no malloc */
}

static inline void pool_free(uint32_t idx) {
    free_stack[free_top++] = idx;    /* O(1), no free */
}
```

**Key points:**
- `static` storage — no heap involvement, no fragmentation
- Free list is LIFO — recently freed nodes are immediately reused, staying warm in cache
- `static_assert` on sizeof — enforces layout; fails at compile time if struct changes
- Check `free_top > 0` before alloc in production; omit only if the maximum is provably bounded

**Exemplar**: order books, event queues, connection pools — any system with a bounded maximum of live objects and high object churn.

---

## Idiom 2 — Intrusive Singly-Linked List via Index (Not Pointer)

Use array indices instead of pointers for linked list next pointers. Indices are 4 bytes (vs 8 for a pointer), giving better cache density. No pointer arithmetic, no address-space dependency.

```c
#define NULL_IDX UINT32_MAX

typedef struct {
    uint32_t head_idx;   /* index of first node in this queue */
    uint32_t tail_idx;   /* index of last node; for O(1) tail append */
    uint32_t count;      /* live node count */
} queue_t;

/* Append to tail — O(1) */
static inline void queue_push(queue_t *q, uint32_t node_idx) {
    pool[node_idx].next_idx = NULL_IDX;
    if (q->tail_idx != NULL_IDX)
        pool[q->tail_idx].next_idx = node_idx;
    else
        q->head_idx = node_idx;
    q->tail_idx = node_idx;
    q->count++;
}

/* Pop from head — O(1) */
static inline uint32_t queue_pop(queue_t *q) {
    uint32_t idx = q->head_idx;
    q->head_idx = pool[idx].next_idx;
    if (q->head_idx == NULL_IDX)
        q->tail_idx = NULL_IDX;
    q->count--;
    return idx;
}
```

---

## Idiom 3 — Flat Array Indexed by Integer Key

Replace hash maps and search trees with a flat array when the key space is bounded and integer-valued. Direct indexing is O(1) with no hash computation, no collision handling, no pointer indirection.

```c
#define MAX_KEYS 8192

static queue_t slot_table[MAX_KEYS];

/* O(1) access — no search, no hash */
static inline queue_t *get_slot(uint32_t key) {
    return &slot_table[key];
}
```

**Key points:**
- Array is statically allocated — no heap, no fragmentation, no growth cost
- `key` must be a validated integer in [0, MAX_KEYS) before use — validate at the API boundary
- `static_assert(MAX_KEYS * sizeof(queue_t) <= 256*1024, "exceeds L2")` — enforce cache tier
- Works wherever the key is a bounded integer: tick indices, user IDs, slot numbers, port numbers

**Exemplar**: a limit order book uses one `queue_t` per price tick, with the tick integer as the direct array index — no hash, no search.

---

## Idiom 4 — Bitmap for Sparse Set Membership

Track which of N slots are non-empty using a `uint64_t` bitmap. One bit per slot; one word covers 64 slots. Finding the lowest/highest set bit is a single hardware instruction.

```c
#define N_BITMAP_WORDS ((MAX_KEYS + 63) / 64)

static uint64_t active_bitmap[N_BITMAP_WORDS];

static inline void bitmap_set(uint64_t *bm, uint32_t slot) {
    bm[slot >> 6] |= (UINT64_C(1) << (slot & 63));
}

static inline void bitmap_clear(uint64_t *bm, uint32_t slot) {
    bm[slot >> 6] &= ~(UINT64_C(1) << (slot & 63));
}

/* Find lowest active slot — O(N_BITMAP_WORDS) worst case, O(1) hardware per word */
static inline int bitmap_lowest(const uint64_t *bm) {
    for (int w = 0; w < N_BITMAP_WORDS; ++w)
        if (bm[w]) return w * 64 + __builtin_ctzll(bm[w]);
    return -1;
}

/* Find highest active slot */
static inline int bitmap_highest(const uint64_t *bm) {
    for (int w = N_BITMAP_WORDS - 1; w >= 0; --w)
        if (bm[w]) return w * 64 + 63 - __builtin_clzll(bm[w]);
    return -1;
}
```

**Key points:**
- `__builtin_ctzll` compiles to `TZCNT` with `-march=native` — one cycle
- `__builtin_clzll` compiles to `LZCNT` with `-march=native` — one cycle
- `UINT64_C(1)` prevents UB from shifting a plain `1` (int) by ≥ 32 bits
- For 8192 slots: 128 words = 1 KB — fits in L1

---

## Idiom 5 — Fixed-Precision Integer Representation

Convert floating-point or high-precision external values to integers at the API boundary. Never use floating-point inside the performance-critical path.

The pattern applies wherever values have a known fixed precision — financial prices, sensor readings, fixed-unit quantities:

```c
/* Generalised: value_to_int converts to integer units at the boundary */
/* scale = 1 / precision. E.g. precision=0.25 → scale=4               */
static inline uint32_t value_to_int(double value, double base, double scale) {
    return (uint32_t)((value - base) * scale + 0.5);  /* +0.5 = round-to-nearest */
}

static inline double int_to_value(uint32_t i, double base, double scale) {
    return base + i / scale;
}
```

**Rule**: conversion happens once, at the public API entry point. Everything inside the hot path operates on `uint32_t` integer units. No floating-point, no casts, no promotions on the critical path.

**Exemplar**: ES futures prices have tick size 0.25 → scale=4; `price_to_tick = (uint32_t)((price - base) * 4.0 + 0.5)`. All book operations work on integer tick indices.

---

## Idiom 6 — `static inline` for Hot-Path Functions

Functions on the critical path must be inlined by the compiler. `static inline` in a header (or at the top of the translation unit) achieves this without LTO dependency.

```c
static inline void bitmap_set(uint64_t *bm, uint32_t slot);
static inline uint32_t pool_alloc(void);
static inline void queue_push(queue_t *q, uint32_t node_idx);
```

**Key points:**
- `static` prevents multiple-definition errors when included in multiple TUs
- `inline` is a hint; `static inline` in a single TU is effectively always inlined at `-O2`
- Do not use `__attribute__((always_inline))` unless profiling confirms the compiler is refusing — it bypasses heuristics and can hurt

---

## Idiom 7 — Direct-Index Lookup by Integer ID (O(1) Location Index)

Maintain a flat array mapping a dense integer ID to an object's current location. Enables O(1) lookup and removal without scanning.

```c
typedef struct {
    uint32_t slot;        /* slot index; UINT32_MAX = not live */
    uint32_t item_idx;    /* index within the slot's queue */
    uint8_t  category;    /* which table/side the item is in */
    uint8_t  _pad[3];
} location_t;

static_assert(sizeof(location_t) == 12, "location_t layout changed");

#define MAX_IDS 100000
static location_t id_index[MAX_IDS];

static inline void id_index_set(uint32_t id, uint32_t slot, uint32_t idx, uint8_t cat) {
    id_index[id] = (location_t){ .slot = slot, .item_idx = idx, .category = cat };
}

static inline void id_index_clear(uint32_t id) {
    id_index[id].slot = UINT32_MAX;
}

static inline int id_index_live(uint32_t id) {
    return id_index[id].slot != UINT32_MAX;
}
```

**Key points:**
- Requires dense sequential IDs — assign IDs at the application boundary (monotonic counter)
- `UINT32_MAX` sentinel — avoids a separate `valid` flag field
- Flat array gives O(1) access with zero hash overhead; 100,000 entries × 12 bytes = 1.2 MB

**Exemplar**: an order book assigns sequential order IDs; `id_index[order_id]` gives the tick and queue position in O(1), enabling O(1) cancel without scanning.

---

## Idiom 8 — Struct Layout for Cache Line Density

Pack the hottest fields into the first cache line (64 bytes). Fields accessed together should be adjacent.

```c
/* Hot path: head_idx and count are accessed on every insert/remove */
typedef struct {
    uint32_t head_idx;    /* 4 — accessed every operation */
    uint32_t tail_idx;    /* 4 — accessed on insert */
    uint32_t count;       /* 4 — accessed to check empty */
    uint32_t _pad;        /* 4 — align to 16 bytes */
} entry_t;                /* 16 bytes — 4 per cache line */

static_assert(sizeof(entry_t) == 16, "entry_t layout changed");
```

**Rule**: verify sizeof after every field addition. Use `static_assert` as the enforcement mechanism.

---

## Idiom 9 — Designated Initialisers and Compound Literals

Use designated initialisers for struct initialisation. They zero-fill unnamed fields, are explicit, and survive field reordering.

```c
/* Preferred */
location_t loc = { .slot = s, .item_idx = idx, .category = cat };

/* Not preferred — breaks if fields are reordered */
location_t loc = { s, idx, cat, {0, 0, 0} };
```

Use compound literals for temporary structs passed to functions:

```c
id_index[id] = (location_t){ .slot = UINT32_MAX };  /* clear */
```

---

## Idiom 10 — Compiler Flag Enforcement of the No-Cast Rule

The no-cast rule ("no casts or promotions in hot-path code") is enforced mechanically by:

```makefile
CFLAGS = -std=c17 -O2 -march=native \
         -Wall -Wextra \
         -Wconversion -Wsign-conversion -Wsign-compare \
         -Wstrict-aliasing=2 \
         -Werror
```

`-Werror` promotes all warnings to errors — a cast that triggers `-Wconversion` will fail the build. This turns the design rule into a compile-time guarantee.

During development, add:
```
-fsanitize=undefined,address
```
Remove sanitizers for benchmarks — they add significant overhead.
