# agentInitiator — Output Format

## Required Structure

Every agentInitiator report must contain all five sections below. Do not omit any section;
if a section has nothing to report, state "None identified" and explain why.

---

## Section 1 — Domain

State the problem domain as given, plus any scope clarifications you made before searching.

If the domain was ambiguous (e.g. "orderbook" without specifying exchange-style vs dark pool
vs FX), state which interpretation you used and why.

---

## Section 2 — Solution Space

One subsection per option found. Each subsection must contain:

```
#### Option N — [Short name]
**Used by**: [Production examples, firms, or papers that use this approach]
**Core idea**: [One sentence — what is the structural insight?]
**Wins when**: [The constraints/regime where this approach outperforms alternatives]
**Loses when**: [The constraints/regime where this approach underperforms]
**Evidence quality**: [Production code / Published benchmark / Practitioner blog / Academic / Inferred]
**Source**: [URL or reference]
```

Minimum: 3 options. If fewer than 3 distinct structural approaches are found, state this
explicitly and describe the search terms used — the Architect needs to know the search
was thorough.

---

## Section 3 — Recommended Reading Order for Architect

List the top 3–5 sources the Architect should read, in order, to build the right mental
model. These should span from "most concrete" (production code) to "most explanatory"
(survey or practitioner post).

---

## Section 4 — Framing Risks

List any design decisions that, if made before the Architect reviews this report, would
close off superior options. These are the decisions that are easy to make early and hard
to reverse.

Example: "Choosing to use a sorted container for price levels early commits to O(log p)
insert — this forecloses the bitmap + direct-index approach without a full rewrite."

If no framing risks identified, state "None identified."

---

## Section 5 — Sources Consulted

List every URL or reference reviewed, with a one-line description of what it contained
and its evidence quality ranking (see methodology.md).

Include sources that were consulted but did not yield useful options — this confirms the
search was thorough.

---

## Quality Check Before Submission

Before returning your report, verify:

- [ ] At least 3 structurally distinct options are described
- [ ] Each option has a stated "wins when" envelope
- [ ] Evidence quality is noted for every option
- [ ] Framing risks section is present (even if empty)
- [ ] No implementation recommendations made (those belong to Architect/agentDuality)
- [ ] No code written
