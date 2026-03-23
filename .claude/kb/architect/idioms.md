# agentArchitect — Design Idioms

Canonical architecture patterns for producing designs that are precise, implementable, and honest about their trade-offs.

---

## Idiom 1 — Data Model First

Define the data model before any interface or component. The data model is the hardest thing to change — every function signature, every module boundary, every performance claim derives from it.

**What to lock before writing any interface:**
1. The representation of each primary value type (integer? float? struct? array?)
2. The ownership model (who allocates, who frees, who may hold a reference)
3. The size and alignment of each hot-path struct (state explicitly)
4. The indexing strategy (how do you find a record given a key?)

**Output format for data model section:**

```
## Data Model

### Primary Types
| Type         | Representation    | Size  | Rationale                        |
|--------------|-------------------|-------|----------------------------------|
| Price        | uint32_t tick     | 4B    | Fixed tick size; integer exact   |
| Quantity     | uint32_t          | 4B    | Whole units only                 |
| ItemId       | uint32_t          | 4B    | Dense session integer            |

### Key Structures
struct Node { uint32_t id; uint32_t value; uint32_t next_idx; }
// sizeof == 12; static_assert required

### Indexing
- Primary: flat array[MAX_KEYS] indexed by integer key — O(1)
- Secondary: flat array[MAX_IDS] indexed by item ID — O(1) cancel/lookup
- Active set: uint64_t bitmap[N_WORDS] — O(1) per word, hardware scan
```

---

## Idiom 2 — Separate Locked Decisions from Open Decisions

When producing a specification from upstream analysis (agentContext, agentDuality reports), explicitly categorise each decision:

- **Locked**: resolved by upstream analysis; implementation must follow; no reopening
- **Conditional**: depends on a runtime parameter; state the crossover; both paths specified
- **Open**: not yet resolved; state what information is needed to resolve it

This prevents implementation agents from inadvertently reopening closed questions and from being blocked on decisions that don't yet need to be made.

```
## Decision Register

| Decision                    | Status      | Resolution                              |
|-----------------------------|-------------|----------------------------------------|
| Price representation        | LOCKED      | uint32_t integer tick (agentDuality A0)|
| Outer structure             | LOCKED      | flat array indexed by tick (agentDuality A1) |
| Bitmap for active slots     | LOCKED      | uint64_t bitmap companion (agentDuality A4) |
| Arena size                  | CONDITIONAL | confirm peak concurrent items first    |
| Cancel index structure      | CONDITIONAL | confirm ID density (dense→array, sparse→hashmap) |
| Sliding window              | OPEN        | measure L2 latency first               |
```

---

## Idiom 3 — Interface Specification by Precondition and Postcondition

Every function in the specification must state:
- **Precondition**: what must be true before the call (validated inputs, ownership state)
- **Postcondition**: what is guaranteed to be true after the call
- **Error behaviour**: what happens when the precondition is violated

This removes ambiguity for the implementation agent and defines the test oracle for agentTest.

```
## Interface Specification

### insert(table, key, value) → id
Precondition:  key in [0, MAX_KEYS); pool not exhausted; id not already live
Postcondition: item visible at table[key]; id_index[id].slot == key; bitmap bit set
Error:         key out of range → return -1, no mutation
               pool exhausted → return -1, no mutation

### remove(table, id) → bool
Precondition:  id in [0, MAX_IDS)
Postcondition: if id was live: item removed, id_index cleared, bitmap updated if slot empty
               if id was not live: no mutation
Returns:       true if item was removed, false if id was not live
Error:         id out of range → return false, no mutation
```

---

## Idiom 4 — Module Boundary by Hidden Decision

A module boundary is justified when it hides one design decision that could change independently of the rest of the system.

**Test for a justified boundary**: complete this sentence — *"This module hides _______________."* If you cannot fill in the blank with a specific, concrete design decision, the boundary is not justified.

**Examples:**
- "This module hides the choice of memory allocator." → justified; the caller does not know whether memory comes from malloc, an arena, or a pool
- "This module hides the bitmap implementation." → justified; the caller queries active slots without knowing it is a bitmap
- "This module hides the fact that there is a Table." → not justified; this is just renaming

---

## Idiom 5 — Narrow Interfaces Over Wide Ones

A deep module has a narrow interface and a complex implementation. It hides a large amount of complexity behind a few functions. A shallow module has a wide interface and a trivial implementation — it adds little value.

**Prefer deep modules.** A function that takes two parameters and does substantial work is better than five functions that each do a piece of the work and require the caller to coordinate them.

**Measure interface depth**: lines of implementation / number of public functions. A ratio below 5 is a shallow module. A ratio above 20 is a deep one.

**Reference**: Ousterhout, "A Philosophy of Software Design", Chapter 4.

---

## Idiom 6 — Specify the API Boundary Explicitly

The API boundary is where external values (floating-point prices, string IDs, user-supplied quantities) are converted to the internal representation. This boundary must be:

1. **Named explicitly** in the specification — it is not implicit
2. **The only place conversions happen** — never inside the hot path
3. **Validating** — preconditions are checked here, not inside the implementation
4. **Documented** — what types are accepted, what conversions occur, what errors are possible

```
## API Boundary

All public functions accept external types and convert at entry:
- double price → uint32_t tick via: tick = (uint32_t)((price - base) * scale + 0.5)
  Precondition: price >= base; (price - base) * scale < MAX_KEYS
- Conversion happens exactly once per call, at the first line of each public function
- All internal functions receive uint32_t tick only — no double in any internal signature
```

---

## Idiom 7 — State the Performance Contract

A design that does not state its performance contract is incomplete. The implementation cannot be validated against an unstated target.

The performance contract states:
- The expected complexity of each hot-path operation (O(1), O(log N), O(N))
- The cache tier the primary data structure targets (L1, L2, L3)
- Any operations that are explicitly not on the hot path (rebasing, bulk reset)

```
## Performance Contract

| Operation     | Complexity | Cache tier | Notes                              |
|---------------|-----------|------------|------------------------------------|
| insert        | O(1)      | L1/L2      | One array write + one bitmap write |
| remove        | O(1)      | L1/L2      | One index read + one array write   |
| query_lowest  | O(W)      | L1         | W = bitmap words; hardware scan    |
| query_highest | O(W)      | L1         | W = bitmap words; hardware scan    |

W is bounded by (MAX_KEYS + 63) / 64. For MAX_KEYS = 8192: W = 128 words = 1 KB.
```

---

## Idiom 8 — Produce a Struct Layout Table

The implementation agents must know the exact struct layout. Do not leave it to them to decide — it is an architectural decision that affects cache tier and indexing arithmetic.

```
## Struct Layouts

struct Node {                     // pool element
    uint32_t id;         // 4
    uint32_t value;      // 4
    uint32_t next_idx;   // 4
};                       // 12 bytes — static_assert(sizeof(Node) == 12)

struct Bucket {                   // per-slot queue header
    uint32_t head_idx;   // 4 — NULL_IDX if empty
    uint32_t tail_idx;   // 4 — NULL_IDX if empty
    uint32_t count;      // 4
    uint32_t _pad;       // 4 — pad to 16 bytes
};                       // 16 bytes — static_assert(sizeof(Bucket) == 16)

struct Location {                 // cancel index entry
    uint32_t slot;       // 4 — UINT32_MAX if not live
    uint32_t item_idx;   // 4
    uint8_t  category;   // 1
    uint8_t  _pad[3];    // 3
};                       // 12 bytes — static_assert(sizeof(Location) == 12)

Working set estimate:
  Buckets:  MAX_KEYS × 16 bytes
  Bitmap:   (MAX_KEYS / 64) × 8 bytes
  Nodes:    MAX_ITEMS × 12 bytes (pool, L3)
  Location: MAX_ITEMS × 12 bytes (index, L3)
```

---

## Idiom 9 — Checklist Before Handing to Implementation

Before passing the design to agentC or agentCPP, verify:

- [ ] Data model section complete: all types, sizes, and rationale stated
- [ ] Decision register complete: locked/conditional/open clearly labelled
- [ ] Every public function has precondition, postcondition, and error behaviour
- [ ] API boundary explicitly named and documented
- [ ] Struct layout table present with sizeof values
- [ ] Performance contract stated
- [ ] No design decisions left for the implementation agent to make
- [ ] No open questions that block the implementation

If any item is unchecked, the design is incomplete. Implementation agents should not be asked to fill in architectural gaps.
