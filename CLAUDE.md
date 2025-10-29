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

1. **Architect** (`architect.md`) - High-level system design and architecture decisions
2. **Class Creator** (`class-creator.md`) - Implements individual classes/modules following specifications
3. **Code Integrator** (`code-integrator.md`) - Integrates new code into the existing codebase
4. **Code Reviewer** (`code-reviewer.md`) - Reviews code for quality, standards, and correctness
5. **Code Tester** (`code-tester.md`) - Writes and executes tests, reports coverage

### Standard Workflow

For significant features or changes:
1. **Architect** designs the solution
2. **Class Creator** implements components
3. **Code Integrator** merges into codebase
4. **Code Reviewer** validates quality
5. **Code Tester** verifies functionality

For simple changes, agents may be skipped as appropriate.

## Development Notes

- Large model files (*.pt, *.pth, *.safetensors) are excluded from version control via .gitignore
- Update this file with specific build commands, test procedures, and architecture patterns as the codebase develops
