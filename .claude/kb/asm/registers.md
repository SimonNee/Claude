# x86-64 Register Reference

## General-Purpose Registers

| 64-bit | 32-bit | 16-bit | 8-bit high | 8-bit low | Purpose / Notes              |
|--------|--------|--------|-----------|-----------|------------------------------|
| rax    | eax    | ax     | ah        | al        | Return value, accumulator    |
| rbx    | ebx    | bx     | bh        | bl        | Callee-saved                 |
| rcx    | ecx    | cx     | ch        | cl        | 4th arg, loop counter, shift |
| rdx    | edx    | dx     | dh        | dl        | 3rd arg, mul/div high word   |
| rsi    | esi    | si     | —         | sil       | 2nd arg, string source       |
| rdi    | edi    | di     | —         | dil       | 1st arg, string destination  |
| rbp    | ebp    | bp     | —         | bpl       | Frame pointer, callee-saved  |
| rsp    | esp    | sp     | —         | spl       | Stack pointer (never clobber)|
| r8     | r8d    | r8w    | —         | r8b       | 5th arg                      |
| r9     | r9d    | r9w    | —         | r9b       | 6th arg                      |
| r10    | r10d   | r10w   | —         | r10b      | Caller-saved, scratch        |
| r11    | r11d   | r11w   | —         | r11b      | Caller-saved, scratch        |
| r12    | r12d   | r12w   | —         | r12b      | Callee-saved                 |
| r13    | r13d   | r13w   | —         | r13b      | Callee-saved                 |
| r14    | r14d   | r14w   | —         | r14b      | Callee-saved                 |
| r15    | r15d   | r15w   | —         | r15b      | Callee-saved                 |

### Key points

- Writing a 32-bit register (`eax`) **zero-extends** into the 64-bit register (`rax`).
  Writing 16-bit or 8-bit does **not** zero-extend the upper bits.
- `rsp` must never appear as an operand or in the clobber list in user-space inline asm.
- `rbp` can be used but is tricky; GCC may be using it as a frame pointer.

---

## Register Names in Each Syntax

AT&T prefixes all register names with `%`. Intel uses bare names.

| AT&T       | Intel    |
|------------|----------|
| `%rax`     | `rax`    |
| `%eax`     | `eax`    |
| `%ax`      | `ax`     |
| `%al`      | `al`     |
| `%r10`     | `r10`    |
| `%r10d`    | `r10d`   |
| `%xmm0`    | `xmm0`   |
| `%ymm0`    | `ymm0`   |

In extended asm, **operand placeholders** (`%0`, `%[name]`) expand to the
compiler-chosen register in whichever syntax is active — you do not write
`%%rax` unless you are explicitly naming a fixed register.

When you name a specific register (e.g. for a fixed constraint like `"a"` for `rax`),
use `%%rax` in AT&T or `rax` in Intel.

```cpp
// AT&T: explicit rax reference
__asm__ ("movq %%rax, %0" : "=r"(out) : : "rax");

// Intel: explicit rax reference
__asm__ (".intel_syntax noprefix\n\t"
         "mov %[out], rax\n\t"
         ".att_syntax prefix"
         : [out] "=r"(out) : : "rax");
```

---

## Caller-Saved vs Callee-Saved (System V AMD64)

See also [calling-conv.md](calling-conv.md).

### Caller-saved (scratch) — safe to use without preserving

`rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`, `r9`, `r10`, `r11`

These are available as scratch registers inside an asm block as long as you
list them in the clobber list.

### Callee-saved — must preserve if used

`rbx`, `rbp`, `r12`, `r13`, `r14`, `r15`

If your asm needs these, push/pop them explicitly and do **not** list them
in the clobber list (clobbering them would corrupt the caller's state).

```cpp
// Safe: use r10 as scratch (caller-saved)
__asm__ volatile (
    "movq %[a], %%r10\n\t"
    "addq %[b], %%r10\n\t"
    "movq %%r10, %[out]"
    : [out] "=r"(result)
    : [a] "r"(a), [b] "r"(b)
    : "r10"           // declare scratch use in clobber list
);
```

---

## Special-Purpose Registers

| Register | Purpose                                                   |
|----------|-----------------------------------------------------------|
| `rip`    | Instruction pointer — never a general operand             |
| `rflags` | Flags — list `"cc"` in clobbers if flags are modified     |
| `mxcsr`  | SSE control/status register (rounding mode, exceptions)   |
| `x87 st` | x87 FP stack — rarely used in modern x86-64               |

---

## SIMD Registers

| Family    | Registers       | Width    | ISA Extension  |
|-----------|-----------------|----------|----------------|
| XMM       | xmm0 – xmm15   | 128-bit  | SSE / SSE2     |
| YMM       | ymm0 – ymm15   | 256-bit  | AVX            |
| ZMM       | zmm0 – zmm31   | 512-bit  | AVX-512        |
| K (mask)  | k0 – k7        | 64-bit   | AVX-512        |

XMM registers are the lower 128 bits of YMM; YMM are the lower 256 of ZMM.
Writing an XMM register zeros the upper 128 bits of the YMM (AVX behaviour).

Constraint letter for XMM/YMM: `"x"` (see constraints.md).

---

## Quick Lookup: Arg Passing Registers

Function arguments (integers and pointers) in order:

```
1st: rdi
2nd: rsi
3rd: rdx
4th: rcx
5th: r8
6th: r9
7th+: stack
```

Floating-point / SIMD arguments: `xmm0` – `xmm7` in order.

Return values: `rax` (integer/pointer), `xmm0` (float/double).
