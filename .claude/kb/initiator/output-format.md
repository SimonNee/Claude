# agentContext — Output Format

## Required Structure

Every agentContext report must contain all six sections below. Do not omit any section;
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
explicitly and describe the search terms used — the user needs to know the search
was thorough.

---

## Section 3 — Affordance Analysis

For each option in Section 2, cross-reference against the problem's known constraints.
Ask: does any constraint *amplify* this solution beyond its general case?

```
#### Affordance — [Option name]
**Constraint**: [The specific property of this problem]
**Effect**: [How it amplifies the approach — not just "works" but "over-delivers"]
**Compounds with**: [Other affordances or properties that reinforce this one]
```

If no affordances are identified for an option, state "None identified" — this is valid.
If the constraints are not yet measured (mid-project run with no data), state what would
need to be known to assess affordances.

---

## Section 4 — Contradiction Analysis (TRIZ)

State the core contradiction(s) the problem presents, and which options resolve them.

```
**Contradiction**: [Improving X degrades Y / Component must be both A and not-A]
**Type**: Technical / Physical
**Resolved by**: [Which option(s) and which TRIZ principle(s)]
```

Also apply trimming: list any components in the current design whose function could be
redistributed, enabling removal.

---

## Section 5 — Framing Risks

List any design decisions that, if made before this report is reviewed, would close off
superior options. These are decisions that are easy to make early and hard to reverse.

Example: "Choosing to use a sorted container for price levels early commits to O(log p)
insert — this forecloses the bitmap + direct-index approach without a full rewrite."

If no framing risks identified, state "None identified."

---

## Section 6 — Sources Consulted

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
- [ ] Affordance analysis present for each option (or "None identified")
- [ ] TRIZ contradiction named and resolved
- [ ] Framing risks section present (even if empty)
- [ ] No design recommendations made (the user decides)
- [ ] No code written
