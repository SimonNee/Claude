# agentInitiator — Research Methodology

## Purpose

This document encodes the research process for SOTA literature reviews. It exists because
the most dangerous failure mode is not "missing an option" — it is "optimising within a
suboptimal frame without knowing the frame exists."

---

## The Frame Trap

Before searching, identify the current frame. A frame is the implicit assumption about
what kind of solution is being sought.

**Example from this project (Iteration 1–5):**
The orderbook was designed as a sorted vector of price levels from the start. Every
subsequent iteration optimised within that frame: cache layout, lazy deletion, index maps,
SIMD analysis. The bitmap + fixed-array design — a well-known production pattern that
eliminates sorted-vector operations entirely — was not considered until Iteration 6 planning,
because the solution space was never opened before the first design decision was made.

**The rule:** always ask "what would a completely different architecture look like?" before
searching for optimisations of the current one.

---

## What to Search For

Search in three layers, in order:

### Layer 1 — Production Code

Search GitHub and known open-source projects for working implementations:
- Query format: `[domain] [language] implementation site:github.com`
- Look for: exchange simulators, HFT libraries, matching engines, market data handlers
- Read the data structure definitions, not just the README
- Note: what data structures are used? What indexing scheme? What is the allocation pattern?

Production code is the highest-quality evidence because it reflects real constraints
(latency SLAs, memory budgets, hardware targets) that academic work often omits.

### Layer 2 — Engineering Practitioner Sources

Search for blog posts and talks from known practitioners:
- Query format: `[domain] [specific technique] low latency site:engineering.blog`
- Sources to prefer: Jane Street, Databento, Citadel, LMAX Exchange, SIG, Jump Trading
- These often contain benchmark data and motivation that academic papers do not

### Layer 3 — Academic Literature

Search for surveys and foundational papers:
- Query format: `[domain] survey OR "state of the art" [year range]`
- Prefer surveys over individual papers — they already synthesise the option space
- Note publication year — techniques from 2005 may be superseded

---

## Source Quality Ranking

| Rank | Source type | Why |
|------|------------|-----|
| 1 | Production open-source code | Real constraints, real hardware, actual deployed |
| 2 | Published benchmark from named firm | Measured, accountable, specific hardware |
| 3 | Practitioner blog (named author, named firm) | Real experience, may lack rigour |
| 4 | Academic benchmark | Controlled but often idealised workloads |
| 5 | Academic reasoning/model | Useful for understanding, not deployment evidence |
| 6 | Blog post (anonymous or unknown firm) | Treat as hypothesis, not evidence |

---

## How to Extract Options

For each source, ask:
1. What is the core data structure?
2. What is the indexing scheme? (hashed, sorted, bitmap, direct array)
3. What operations is it optimised for? (insert, delete, scan, snapshot)
4. What are the stated or implied constraints? (tick range, order count, price band)
5. What does the code allocate? (per-order heap nodes? flat arrays? pools?)

Group sources by structural similarity. Each distinct structural approach is one option.

---

## How to State the Envelope

Every option has a regime where it wins. State it as a constraint:
- "Wins when tick range ≤ N (fits in a fixed-size array)"
- "Wins when order flow is dominated by cancels (O(1) cancel matters)"
- "Wins when price band is bounded and known at compile time"

If the envelope is unknown from sources, state what would need to be measured.

---

## Anti-Patterns to Avoid

1. **Searching for confirmation** — don't search for "why [current approach] is good."
   Search for "alternatives to [current approach]."

2. **Stopping at the first result** — the first search result is often the most
   SEO-optimised, not the most relevant. Read at least 3 distinct sources.

3. **Treating blog posts as ground truth** — blog posts are hypotheses until corroborated
   by production code or benchmark data.

4. **Omitting options that seem complex** — report all viable options found.
   The Architect decides what is in scope, not agentInitiator.

5. **Narrowing the frame in the output** — if two options are structurally different,
   do not merge them. Present them as separate options with their respective envelopes.
