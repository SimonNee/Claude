# Agents Project Status

## Current State: agentASM Complete — agentTimeAndSpace Pending

**Date:** 2026-03-14
**Branch:** `feature/agents` (from `main`)

---

## What's Done

### agentASM ✓ COMPLETE

- **Agent definition**: `.claude/agents/agentASM.md`
  - Mandatory workflow: read pitfalls first, consult phrasebook, choose syntax, write, checklist
  - Wired to `cpp/asm-kb/` knowledge base
  - Structured output format with constraint notes and compile test step

- **Knowledge base**: `cpp/asm-kb/` (7 files, 1,660 lines)

  | File | Contents |
  |------|----------|
  | `README.md` | Navigation and mandatory reading order |
  | `syntax.md` | GCC `__asm__` template, AT&T vs Intel, `volatile`, local labels |
  | `registers.md` | x86-64 GPR table, AT&T/Intel naming, caller/callee-saved, SIMD |
  | `constraints.md` | Constraint letters, `=`/`+`/`&` modifiers, clobber list |
  | `patterns.md` | C++ → asm phrasebook (arithmetic, bitwise, memory, atomics, SIMD, timing) |
  | `calling-conv.md` | System V AMD64 ABI — args, return values, register preservation |
  | `pitfalls.md` | 12-point pre-flight checklist and documented failure modes |

### agentTimeAndSpace ✗ NOT STARTED

- Name TBD (candidates: `agentBigO`, `agentOpt`, `agentPerf`)
- Scope agreed: Big-O complexity, space trade-offs, vectorisation, parallelism, cache efficiency
- No knowledge base or agent definition written yet

---

## Design Decisions

- **Pattern**: Both agents follow the AgentQ model — mandatory reading of a pitfalls doc,
  a phrasebook-style knowledge base, and a structured output format
- **Knowledge base location**: `cpp/asm-kb/` (mirrors `kdb/phrases/docs/` for AgentQ)
- **Pitfalls doc**: Lives in both `asm-kb/pitfalls.md` and referenced from the agent file;
  grows from real incidents discovered during use
- **Status docs**: Per-project local files for now; a top-level summary can be added later
  without changing the local files

---

## Next Steps

1. Agree name for agentTimeAndSpace
2. Design knowledge base structure for complexity/optimisation reference
3. Write knowledge base files
4. Write agent definition and wire to knowledge base
5. Push `feature/agents` to remote
