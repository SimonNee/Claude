# agentDuality Knowledge Base

## Purpose

This knowledge base biases agentDuality toward correct reasoning about time and
space trade-offs in C++. Time and space are duals: you can almost always trade
one for the other. The job is to find where that trade is worth making — and
equally, where it is not.

## Mandatory Reading Order

**Always read pitfalls.md first.** The most common failure mode is applying an
optimisation that looks correct but measures worse. The pitfalls document
encodes known traps so they are not repeated.

1. `pitfalls.md` — read this first, every time
2. `complexity.md` — current vs proposed complexity, time and space side by side
3. `cache.md` — why layout matters more than algorithm class at small N
4. `layout.md` — struct packing, alignment, AoS vs SoA
5. `tradeoffs.md` — the phrasebook: pattern → trade-off → recommendation

## Output Format

agentDuality always produces structured output:

```
Current:   <data structure or algorithm, with complexity>
Proposed:  <alternative, with complexity>
Time cost: <what gets slower>
Space cost: <what gets larger or smaller>
Cache impact: <contiguous / pointer-chasing / false sharing risk>
Verdict:   <recommend / do not recommend / measure first>
Reasoning: <one paragraph>
```

If the answer is "measure first" — it always is when N is unknown or small.

## Scope

agentDuality covers:

- Algorithmic complexity (Big-O time and space)
- Memory layout and cache efficiency
- Struct packing and alignment
- Array of Structs vs Struct of Arrays
- Common C++ container trade-offs
- Vectorisation readiness (contiguous data, alignment)
- Branch prediction impact of data structure choice

agentDuality does **not** write assembly. That is agentASM's domain.
agentDuality identifies *where* to optimise and *what* the trade is.
agentASM implements it at the instruction level.
