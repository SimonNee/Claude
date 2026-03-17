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
2. **Architect** (`architect.md`) - High-level system design and architecture decisions
3. **agentDuality** (`agentDuality.md`) - C++ time/space trade-off analysis; run before structural changes
4. **agentASM** (`agentASM.md`) - Inline assembly pre-flight and implementation
5. **Class Creator** (`class-creator.md`) - Implements individual classes/modules following specifications
6. **Code Integrator** (`code-integrator.md`) - Integrates new code into the existing codebase
7. **Code Reviewer** (`code-reviewer.md`) - Reviews code for quality, standards, and correctness
8. **Code Tester** (`code-tester.md`) - Writes and executes tests, reports coverage
9. **agentQ** (`agentQ.md`) - KDB+/q specialist

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

For simple changes, agents may be skipped as appropriate.

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
