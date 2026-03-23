# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Purpose

This is a private learning repository for experimenting with Claude Code, focusing on projects involving C++, Python, KDB+/q, and AI models from Hugging Face.

## Technology Stack

- **C++**: For performance-critical components
- **Python**: For scripting, data processing, and ML model integration
- **KDB+/q**: For time-series data and high-performance analytics
- **Hugging Face**: For AI/ML models and transformers

## Coding Principles

- **KISS (Keep It Simple, Stupid)**: Always choose the simplest solution that works. Avoid over-engineering.
- **Readable > Clever**: Code should be obvious. Avoid clever tricks or complex one-liners.
- **Standard libraries preferred**: Use built-in functionality before adding dependencies.
- **Explicit over implicit**: Clear, verbose code is better than terse magic.
- **No premature optimization**: Prioritize readability first, optimize only when necessary.
- **Small functions**: Keep functions focused and under 50 lines where reasonable.

## Agent Workflow

This project uses specialized agents (in `.claude/agents/`) to handle different aspects of development:

1. **agentContext** (`agentContext.md`) - SOTA survey + affordance analysis + TRIZ contradiction analysis. Re-runnable at any point. Output goes to the user.
2. **agentArchitect** (`agentArchitect.md`) - Translates agentContext + agentDuality outputs into precise implementation specifications; applies real-world design pitfalls knowledge; produces data models, interface specs, and performance contracts
3. **agentDuality** (`agentDuality.md`) - C++ time/space trade-off analysis; run before structural changes
4. **agentASM** (`agentASM.md`) - Inline assembly pre-flight and implementation
5. **agentC** (`agentC.md`) - C language specialist; enforces no-cast rule via `-Wconversion`; can run in parallel with agentCPP
6. **agentCPP** (`agentCPP.md`) - C++ language specialist; enforces no-cast rule via type system and `-Wconversion`; can run in parallel with agentC
7. **agentTest** (`agentTest.md`) - Expert test and benchmark specialist; knows testing pitfalls, RDTSC discipline, sanitizer discipline, circular test detection
8. **agentQ** (`agentQ.md`) - KDB+/q specialist

### Standard Workflow

For significant features or changes:
1. **agentContext** opens the solution space (SOTA + affordances + TRIZ). Re-run whenever the project hits a wall or changes direction.
2. **Architect** designs the solution
3. **agentDuality** validates structural choices before implementation
4. **Class Creator** implements components
5. **Code Integrator** merges into codebase
6. **Code Reviewer** validates quality
7. **Code Tester** verifies functionality

The user decides what agent output to act on and who to pass it to. There is no prescribed handoff chain.

**agentContext is mandatory before any new project or significant feature begins.** Do not proceed to Architect or any design work until agentContext has run. This rule exists because the frame trap is invisible from inside it — see the orderbook whitepaper, section 6.3.

For simple changes (bug fixes, minor edits), other agents may be skipped as appropriate. agentContext may also be skipped if the user explicitly says so.

## Agent Knowledge Bases

All agent KBs live under `.claude/kb/` and are tracked in the repo. This ensures
they are available to anyone cloning the project.

```
.claude/
  agents/       — agent definition files (.md)
  kb/
    asm/        — agentASM knowledge base (syntax, constraints, patterns, pitfalls)
    duality/    — agentDuality knowledge base (cache, complexity, layout, tradeoffs)
    q/          — agentQ pitfalls (tracked); phrasebook + KX reference are local-only
    initiator/  — agentContext methodology KB (methodology, output-format)
    triz/       — TRIZ principles KB shared by agentContext and agentDuality
```

**Rules:**
- Every agent that requires mandatory pre-reading must have its KB here
- Paths in agent `.md` files must be relative (e.g. `.claude/kb/asm/pitfalls.md`)
- External resources (public web data, local-only copies) are noted in the agent file
  but are not required to be in the repo

## Development Notes

- Large model files (*.pt, *.pth, *.safetensors) are excluded from version control via .gitignore
- Update this file with specific build commands, test procedures, and architecture patterns as the codebase develops
