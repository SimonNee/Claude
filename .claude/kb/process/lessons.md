# Process Lessons — From the Orderbook Project

*Derived from 18 iterations of C++ orderbook optimisation. Full case study: `cpp/orderbook/whitepaper.md`*

---

## 1. Open the solution space before the first design decision

Run agentContext before any architecture or implementation work begins. The orderbook project
spent 6 iterations optimising within a sorted-vector frame that was replaced wholesale in
Iteration 7 by a well-known production pattern (bitmap + fixed array). A SOTA survey at the
start would have reached that design directly. **Optimising the wrong frame is wasted work
regardless of how carefully it is done.**

## 2. The hierarchy of gains is steep

In performance work the gains follow a strict order of magnitude hierarchy:

```
Algorithm change      →  10–1000×  (cancelOrder O(p×q) → O(1): 2,600×)
Data structure change →  3–10×     (bitmap + fixed array: 3–5× across all ops)
Micro-optimisation    →  1.1–1.5×  (ASM, LTO, unrolling: accumulated ~1.3×)
```

Start at the top. Micro-optimisation before the right algorithm and structure is in place
produces small wins on a problem that is about to be restructured. The orderbook project's
15 micro-optimisation iterations together matched roughly one structural decision.

## 3. One algorithm fix can dwarf everything else combined

The cancelOrder O(1) fix (adding an id→location index and lazy deletion) was ~10 lines of
code and produced the largest single gain in the project — larger than all subsequent
iterations combined. It required no performance expertise, no assembly knowledge, no
profiling — just recognising that a nested O(p×q) scan was structurally unacceptable.

**When a hotspot is obvious, fix the algorithm before reaching for any other tool.**

## 4. Measure before predicting

Pre-implementation analysis predicted inner order buffer depth q≈10. Instrumentation showed
q_mean=1,085 — off by 100×. The performance model was wrong; the structural conclusion
happened to still be correct, but for different reasons. **Layout, cache, and working-set
predictions are hypotheses until measured. Instrument first, then optimise.**

## 5. A reviewer finds more than a writer

The agentASM pre-flight review (compile with `-S`, inspect the output) found a structural
problem — `3× divq + operator delete` per cancel inside compiled STL template code — that
was invisible from C++ source and that no amount of inline ASM could have addressed. The
structural fix that followed was worth −30–69% across all operations. **Assembly inspection
is more valuable than assembly writing. Apply the same principle to any specialist review:
the most important finding is often the one that says "don't optimise here — fix this instead."**

## 6. KISS applies to process as well as code

Each iteration added benchmarks, agents, KB files, and documentation. By Iteration 18 the
project had 1,500+ lines of status notes, a whitepaper, and 8 template instantiations — for
a book that fits in 358 lines of source. The learning objective was met by Iteration 13.
**Recognise when the goal is achieved. More iterations, more infrastructure, and more
measurement are themselves a form of over-engineering.**

---

*Worked example: `cpp/orderbook/whitepaper.md`*
