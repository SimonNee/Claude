# agentArchitect — Design Pitfalls

Systematic errors in software architecture. Sources: Parnas (1972, 1984), Dijkstra, Ousterhout "A Philosophy of Software Design", Kleppmann "Designing Data-Intensive Applications", C2 wiki, production postmortems.

---

## Pitfall 1 — Designing in the Wrong Frame

The most expensive architectural mistake is optimising a design that should not exist. All subsequent work — components, interfaces, tests — is built on a foundation that will be replaced wholesale.

The frame is the implicit assumption about *what kind of solution is being built*. It is invisible from inside it. A sorted vector of price levels is not a bad implementation — it is the wrong frame entirely once the fixed-tick flat array is known.

**Symptoms**: the architecture feels correct but keeps hitting friction. Performance targets require heroic micro-optimisation. The simplest operations require the most code. Every extension requires touching the core abstraction.

**Fix**: do not begin design until the solution space has been opened (agentContext). The frame trap cannot be escaped by being more careful inside the wrong frame.

**Reference**: Ousterhout, "A Philosophy of Software Design", Chapter 3 — Working Code Isn't Enough.

---

## Pitfall 2 — Premature Abstraction

An abstraction introduced before two concrete cases exist is speculation. It adds complexity, reduces performance, and often turns out to be the wrong abstraction for the third case.

```
// Premature: generalised before the second use case exists
interface PriceSource { double getPrice(Symbol s, Timestamp t); }

// Concrete: one use case, no interface
double get_es_price(uint32_t tick, double base) { return base + tick * 0.25; }
```

**The rule of three**: wait until you have three concrete cases before extracting an abstraction. One case is a function. Two cases is a coincidence. Three cases reveals the real pattern.

**Reference**: C2 wiki, "Rule Of Three"; Sandi Metz, "The Wrong Abstraction".

---

## Pitfall 3 — Coupling Disguised as Flexibility

A design that passes a "context object" or "options struct" to every function appears flexible — it avoids hard-coding decisions. In practice it creates invisible coupling: every caller must know what fields the context contains, what their valid ranges are, and how they interact. The coupling is real; it is just not visible in the type signatures.

True flexibility comes from narrow interfaces with few parameters. A function that takes two integers and returns one has no hidden coupling. A function that takes a pointer to a 20-field struct has coupling to all 20 fields.

**Symptoms**: changing one field of the context struct requires auditing every function that receives it. "Unused" fields accumulate. Defaults are wrong for some callers.

**Fix**: pass only what a function needs. If a function needs 8 fields from a 20-field struct, it needs a different abstraction — or the struct is the wrong boundary.

**Reference**: Parnas (1972), "On the Criteria to Be Used in Decomposing Systems into Modules".

---

## Pitfall 4 — Designing for Hypothetical Requirements

Every feature added for a requirement that does not exist yet:
- Adds complexity that must be maintained
- Adds code that must be tested
- Adds interfaces that constrain future real requirements
- Is almost certainly the wrong design for the requirement when it eventually arrives

**The YAGNI principle** (You Aren't Gonna Need It): do not add structure for requirements that have not been stated. The cost of adding it later is almost always less than the cost of maintaining it speculatively.

**Reference**: Beck, "Extreme Programming Explained"; Fowler, "Refactoring".

---

## Pitfall 5 — Over-Layering

Every layer of abstraction adds indirection. Each layer must be understood to debug problems that cross it. More than three layers between a user action and its effect is a smell.

The justification for a layer is that it *hides* something: a detail the layers above should not know about. If a layer merely *renames* the layer below it without hiding anything, it adds no value and should be removed.

**Test**: can you state in one sentence what each layer hides from the layers above it? If not, the layer does not have a clear reason to exist.

**Reference**: Ousterhout, "A Philosophy of Software Design", Chapter 4 — Modules Should Be Deep.

---

## Pitfall 6 — Leaky Abstractions

An abstraction leaks when its implementation details are visible to callers. The caller must understand the implementation to use the interface correctly. The abstraction provides no encapsulation.

**Common forms:**
- A function that returns an error code only meaningful in terms of an internal data structure
- An interface that requires callers to call functions in a specific order (temporal coupling)
- A "clean" API that performs differently based on internal state the caller cannot observe
- An interface that requires callers to allocate or manage memory that the abstraction owns

**Fix**: the interface should express *what* without revealing *how*. A caller should never need to read the implementation to use the interface correctly.

**Reference**: Spolsky, "The Law of Leaky Abstractions" (2002).

---

## Pitfall 7 — The Wrong Boundary

Modules are divided wrong when:
- Changing one business requirement forces changes to multiple modules
- Two modules must always be deployed together
- A module has many small public functions that are only ever called in a fixed sequence
- Two modules share more internal state than they expose to the rest of the system

**Correct boundary criterion** (Parnas): modules should be divided so that each hides one *design decision* from the rest of the system. The design decision is the thing most likely to change. When it changes, only one module changes.

**Reference**: Parnas (1972), "On the Criteria to Be Used in Decomposing Systems into Modules".

---

## Pitfall 8 — API Surface Area Inflation

Every public function, type, and constant is a commitment. It must be maintained, documented, and tested. It constrains future evolution — removing a public API breaks callers.

**Rule**: start with the smallest API that satisfies the current requirements. Add to the public surface only when a concrete caller needs it. Private functions cost nothing to change; public functions cost everything.

**Reference**: Bloch, "How to Design a Good API and Why It Matters" (Google Tech Talk, 2007).

---

## Pitfall 9 — Temporal Coupling in Interfaces

An interface with temporal coupling requires callers to invoke functions in a specific order. The correct order is not expressed in the type system — it is enforced by convention, documentation, or runtime errors.

```
// Bad: temporal coupling — init must be called before use, not enforced
table_t t;
table_use(&t);   // silent corruption if init not called
table_init(&t);

// Good: coupling expressed in types — construction enforces initialisation
table_t *t = table_create();   // returns fully-initialised object or NULL
table_use(t);
```

**Reference**: Ousterhout, "A Philosophy of Software Design", Chapter 10 — Define Errors Out of Existence.

---

## Pitfall 10 — Misidentifying the Hot Path

A design optimised for the wrong operation pays the cost everywhere. Architects regularly misidentify the hot path because they reason about what *should* be fast rather than measuring what *is* called most often.

**Rule**: do not make architectural trade-offs for performance without a measured or analytically grounded claim about operation frequency. "Cancel is rare" is an assumption — it may be wrong. In a live market, cancel rate can exceed add rate by 10:1.

**Reference**: Knuth, "Premature optimisation is the root of all evil" (in context: optimise the right thing, not any thing).

---

## Pitfall 11 — Ownership Ambiguity

When it is unclear who owns a resource (who allocates it, who frees it, who may read/write it concurrently), bugs are inevitable. Ownership ambiguity is the architectural root of use-after-free, double-free, and data race bugs.

**Every resource must have exactly one owner at every point in time.** The owner is responsible for its lifetime. Transfer of ownership must be explicit.

In C: ownership is expressed by convention and documented in comments. In C++: ownership is expressed by `unique_ptr`, `shared_ptr`, or explicit arena semantics.

**Reference**: Stroustrup, "The C++ Programming Language", resource management chapters; CERT C MEM rules.

---

## Pitfall 12 — Ignoring the Data

Architecture is primarily about data: what data exists, where it lives, how it flows, and who transforms it. A design that focuses on behaviour (functions, classes, patterns) without first establishing the data model is building on an uncertain foundation.

**The data model is the hardest thing to change.** A poorly chosen data representation — float instead of integer tick, AoS instead of SoA, pointer instead of index — requires touching every operation that processes it. The API surface and the behaviour layer are easy to change relative to the representation.

**Rule**: define the data model first. Lock the representation before defining any function signature. The representation choice constrains everything downstream.

**Reference**: Linus Torvalds: "Bad programmers worry about the code. Good programmers worry about data structures and their relationships."
