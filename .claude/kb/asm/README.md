# ASM Knowledge Base

Reference material for agentASM. Read in this order:

## Mandatory Reading Order

1. **[pitfalls.md](pitfalls.md)** — Read first, every time. Common errors when writing inline asm.
2. **[syntax.md](syntax.md)** — GCC extended `__asm__` syntax and template rules.
3. **[registers.md](registers.md)** — Register names, sizes, purposes, AT&T vs Intel naming.
4. **[constraints.md](constraints.md)** — Operand constraint letters and clobber list.
5. **[patterns.md](patterns.md)** — Idiomatic C++ → inline asm translations. The phrasebook.
6. **[calling-conv.md](calling-conv.md)** — System V AMD64 ABI: which registers to preserve.

## Navigation by Task

| Task                              | File                  |
|-----------------------------------|-----------------------|
| Writing a new asm block           | syntax.md             |
| Choosing `=r`, `+m`, `&r` etc.   | constraints.md        |
| Which register name to use        | registers.md          |
| Translating loops, arithmetic     | patterns.md           |
| Preserving caller registers       | calling-conv.md       |
| Avoiding common mistakes          | pitfalls.md           |

## Scope

This knowledge base covers **x86-64 Linux** (System V AMD64 ABI) with **GCC/Clang** inline assembly.
Intel syntax (`.intel_syntax noprefix`) and AT&T syntax are both documented throughout.
