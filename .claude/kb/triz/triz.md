# TRIZ — Engineering Problem-Solving Principles

Developed by Altshuller from 400,000+ patent analyses. Core finding: inventive problems
recur across domains; solutions map to ~40 principles. Use this to find non-obvious
solutions by pattern-matching your contradiction to known resolutions.

---

## How to Use This KB — Not Cargo Cult TRIZ

TRIZ was built for physical systems. Do not apply it mechanically to software. Apply it
selectively where the problem has mechanical structure — which is more often than you'd
expect.

**Software systems are mechanical at the design level.** Classes, modules, and data
structures behave like physical components: they have contact surfaces (interfaces), they
transfer something (data instead of force), they constrain what connects to them. The
analogues are direct:

| Physical | Software |
|----------|----------|
| Components with interfaces | Classes, modules, services |
| Force/energy transfer | Data transfer, function calls |
| Friction at boundaries | Serialisation cost, context switches, lock contention |
| Shims | Adapters, translators, format converters |
| Stress concentration | Hot paths, lock bottlenecks, zones of conflict |
| Redundant structure | Fields whose function is served elsewhere |

**The useful elements of TRIZ for software:**
- Naming contradictions — forces you to state what the actual conflict is
- Trimming — identifies components whose function is already served elsewhere
- Ideality — asks what the mechanism looks like when it disappears
- Affordance identification — finds where problem constraints amplify a solution
- Separation principles — resolves physical contradictions by separating in time, space, or condition

**What to ignore:**
- The full 40-principle procedure applied to every problem
- The contradiction matrix (useful for physical patents; too coarse for software)
- Any principle applied without first naming the contradiction it resolves

**The real vs incidental contradiction test — apply this first:**

Before treating a contradiction as fundamental, ask: does this conflict exist because of
*physics* (it cannot be otherwise), or because of an *implementation choice* (it exists
because of how something was built)?

- **Real contradiction**: O(1) lookup and unbounded range cannot both be true simultaneously — physics, not choice. TRIZ applies.
- **Incidental contradiction**: an extra copy exists because two teams wrote code independently — implementation choice, not physics. Delete the layer; don't invent around it.

Elaborate TRIZ resolutions applied to incidental contradictions are waste. The discipline
is recognising which type you have before reaching for a principle.

---

## The Fundamental Move: Name the Contradiction

Every hard engineering problem contains a contradiction. Name it before searching for
solutions. There are two types:

**Technical contradiction**: Improving parameter A degrades parameter B.
*Example: faster level lookup (A) requires larger index (B).*

**Physical contradiction**: A component must have property X and not-X simultaneously.
*Example: the index must cover all ticks (large, complete) and fit in a register (small, bounded).*

Naming the contradiction precisely is the hardest and most valuable step. A solution found
without naming the contradiction is luck; a solution found by resolving the contradiction
is engineering.

---

## The 40 Principles — Most Relevant to Systems Engineering

Match your contradiction to the relevant principle(s). Do not scan all 40 for every
problem — name the contradiction first, then look for principles that address *that
specific conflict type*.

| # | Principle | Core idea | Engineering application |
|---|-----------|-----------|------------------------|
| 1 | Segmentation | Divide the object | Split AoS into SoA; separate hot/cold fields |
| 2 | Extraction | Remove the conflicting part | Remove `Order.price` — redundant with `level.price` |
| 3 | Local quality | Each part serves its own function | Separate bid/ask arrays; each sorted independently |
| 5 | Merging | Combine in space or time | bitmap + flat array — one structure, two capabilities |
| 7 | Nesting | Put one inside another | Embed tick index inside flat array; O(1) via nesting |
| 10 | Prior action | Perform the action in advance | Pre-sort levels; precompute tick from price at insert |
| 13 | Inversion | Do the opposite | Instead of searching for the level, let the tick *be* the index |
| 15 | Dynamism | Make rigid parts adaptable | Lazy deletion: don't compact eagerly, mark and defer |
| 16 | Partial/excess action | Slightly more or less | Bitmap covers 128 ticks; price band needs 100 — surplus is free |
| 25 | Self-service | The object serves its own function | BSR/BSF — the register finds its own leading bit |
| 26 | Copying | Use a cheaper representation | Tombstone = cancelled state copy; avoids shifting |
| 35 | Parameter change | Change the representation | Price (float) → tick (int); same semantics, better structure |
| 40 | Composite structures | Two primitives, one compound capability | bitmap + flat array — neither alone achieves both O(1) lookup and full level access |

---

## Trimming

Remove a component. Redistribute its useful function to components that already exist.

**Process:**
1. List all components and their functions
2. Ask: which functions are already served by another component?
3. Remove the redundant component; confirm the function survives

**Example from this project**: `Order.price` — useful function is "know the price at
which this order rests". That function is already served by `PriceLevel.price` (the
level the order lives in). `Order.price` can be trimmed. Result: 24→16 bytes, +50%
cache line density.

**Rule**: Before adding a field, ask if another component already carries that
information. Before keeping a field, ask if removing it would lose any function not
served elsewhere.

---

## Ideality

The ideal final result: the function is performed without the mechanism existing.

*"The level finds itself"* — with a bitmap, BSR finds the best bid in one instruction.
No search loop. No comparison. The hardware performs the function.

*"The cancel finds the order"* — with a direct-index array, cancellation is an array
dereference. No hash, no search. The index *is* the location.

**Use ideality as a target**: ask "what would this look like if the mechanism
disappeared?" The answer often reveals a representation change (Principle 35) or a
structural inversion (Principle 13) that eliminates the mechanism entirely.

---

## Separation Principles for Physical Contradictions

When something must be both X and not-X, resolve by separating in one of four dimensions:

| Separation | Meaning | Example |
|------------|---------|---------|
| In time | X at one time, not-X at another | Lazy deletion: cancelled now, compacted later |
| In space | X in one place, not-X in another | Hot outer array (L1), cold inner buffer (L3) — accept the split |
| By condition | X under condition C, not-X otherwise | Deferred compaction: only when `drained` flag is set |
| By scale | X at one level, not-X at another | Bitmap (coarse, fast) + array (fine, complete) |

---

## Affordance Identification

An affordance is a property of the specific problem that makes a general solution work
*better than expected* — not just applicable, but compounding.

**Process:**
1. List the problem's concrete constraints (bounds, distributions, access patterns)
2. For each candidate solution, ask: does any constraint *amplify* this solution?
3. Flag where a constraint turns a good solution into an exceptional one

**Example**: Bitmap index is a good solution for bounded tick ranges. The OU price band
(100 ticks, fits in 2× uint64) affords:
- Bitmap fits in a register → BSR/BSF in one cycle (not just fast, but *essentially free*)
- No heap allocation → no `operator delete` overhead
- Integer tick arithmetic → replaces float comparison, eliminates rounding risk

The constraint didn't just permit the solution — it compounded it across three
independent dimensions simultaneously.

**Rule**: An affordance check is not "does this approach work here?" It is "does this
problem's shape cause this approach to over-deliver?"

---

## Pre-Analysis Checklist

Before proposing any solution:

1. **Name the contradiction** — technical or physical?
2. **Apply trimming** — what components can be removed?
3. **Apply ideality** — what would it look like if the mechanism disappeared?
4. **Check affordances** — which constraints amplify this solution beyond its general case?
5. **Select principles** — which of the 40 address this contradiction type?

---

*Source: Altshuller, G. (1996). And Suddenly the Inventor Appeared. Technical Innovation
Center. The 40 principles and contradiction matrix are in the public domain.*
