# Inline Assembly Pitfalls

Common mistakes when writing GCC inline asm. Read this before every asm block.
This document will grow as real incidents are discovered in this project.

---

## Critical: Operand Constraint Errors

### Missing `+` on Read-Write Operands

```cpp
// WRONG — = says "write only", compiler assumes initial value is dead
int x = 10;
__asm__ ("addl $5, %0" : "=r"(x));   // x may not start with 10!

// CORRECT — + says "read and write"
__asm__ ("addl $5, %0" : "+r"(x));   // x starts as 10, becomes 15
```

**Rule**: Use `=r` only when the asm does not read the operand's initial value.
Use `+r` when the asm both reads and writes the same operand.

---

### Missing Early Clobber `&`

```cpp
// WRONG — compiler might put 'tmp' and 'b' in the same register
int tmp;
__asm__ ("movl %[b], %[tmp]\n\t"
         "addl %[a], %[tmp]"
         : [tmp] "=r"(tmp)
         : [a] "r"(a), [b] "r"(b));

// CORRECT — & tells compiler: tmp is written before all inputs are consumed
__asm__ ("movl %[b], %[tmp]\n\t"
         "addl %[a], %[tmp]"
         : [tmp] "=&r"(tmp)
         : [a] "r"(a), [b] "r"(b));
```

**Rule**: Use `=&r` (early clobber) whenever the output is written before the
asm has finished reading all its inputs.

---

### Missing `"cc"` Clobber

```cpp
// WRONG — addl modifies flags but compiler is not told
__asm__ ("addl %[b], %[a]" : [a] "+r"(a) : [b] "r"(b));

// CORRECT
__asm__ ("addl %[b], %[a]" : [a] "+r"(a) : [b] "r"(b) : "cc");
```

**Rule**: Any instruction that modifies `rflags` (add, sub, and, or, xor, cmp,
test, shift, rotate, inc, dec, neg) requires `"cc"` in the clobber list.
`NOT` is the notable exception — it does not affect flags.

---

### Missing `"memory"` Clobber for Memory Side Effects

```cpp
// WRONG — asm writes to memory via pointer but compiler doesn't know
void store(int* ptr, int val) {
    __asm__ ("movl %[v], (%[p])"
             :
             : [p] "r"(ptr), [v] "r"(val));
}
// Compiler may reorder reads/writes around this block

// CORRECT
void store(int* ptr, int val) {
    __asm__ ("movl %[v], (%[p])"
             :
             : [p] "r"(ptr), [v] "r"(val)
             : "memory");
}
```

**Rule**: Add `"memory"` to the clobber list whenever the asm reads or writes
memory that is not listed as an explicit `"m"` output operand.

---

## AT&T Syntax Direction

### Operand Order Reversed from Intel

AT&T syntax: **source first, destination last**.
Intel syntax: **destination first, source last**.

```cpp
// AT&T: src, dst — this MOVES b INTO a
"movl %[b], %[a]"    // a = b

// Intel: dst, src — same operation
"mov eax, ebx"       // eax = ebx
```

**Rule**: Every time you write or read AT&T asm, consciously re-read the
operand order. This reversal causes more bugs than any other single issue
because it looks correct and compiles without error.

---

### `%%` Required for Literal Register Names in AT&T

```cpp
// WRONG — single % is interpreted as an operand placeholder
__asm__ ("movq %rax, %0" : "=r"(x));   // %r is operand 'r', not register

// CORRECT — %% is a literal % in the output
__asm__ ("movq %%rax, %0" : "=r"(x) : : "rax");
```

**Rule**: In AT&T extended asm, use `%%regname` to refer to a specific register
by name. Single `%` is reserved for operand placeholders (`%0`, `%[name]`).
In Intel syntax this is not an issue (no `%` used).

---

## Volatile Omissions

### Asm Without `volatile` May Be Eliminated or Moved

```cpp
// WRONG — compiler may remove this if 'cycles' is unused, or move it
uint64_t lo, hi;
__asm__ ("rdtsc" : "=a"(lo), "=d"(hi));

// CORRECT
__asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
```

**Rule**: Use `volatile` when:
- The asm has observable side effects beyond its listed outputs
- Ordering relative to surrounding code matters
- The asm must not be duplicated (e.g. RDTSC in a loop)

---

## Register Hazards

### Clobbering a Callee-Saved Register

```cpp
// WRONG — rbx is callee-saved; listing it in clobbers tells the compiler
// it is destroyed, but the compiler generated code that relies on rbx being
// preserved across the function — corruption results
__asm__ volatile ("movq $42, %%rbx"
                  : : : "rbx");   // WRONG

// CORRECT — save and restore manually, do NOT list in clobbers
__asm__ volatile ("pushq %%rbx\n\t"
                  "movq $42, %%rbx\n\t"
                  // ... use rbx ...
                  "popq %%rbx"
                  : : :);         // rbx not in clobbers (we preserved it)
```

Callee-saved registers: `rbx`, `rbp`, `r12`, `r13`, `r14`, `r15`.

**Rule**: Never list a callee-saved register in the clobber list unless you
are absolutely certain the compiler did not allocate it across the asm block.
When in doubt, push/pop it yourself.

---

### Using `rsp` as an Operand

`rsp` must never appear as a constraint operand or clobber in user-space
inline asm. The compiler manages the stack pointer. Altering `rsp` without
restoring it exactly will corrupt the stack frame.

---

## Division Pitfalls

### Forgetting `cdq`/`cqo` Before `idiv`

```cpp
// WRONG — idivl divides edx:eax by operand, but edx is not sign-extended
int q, r;
__asm__ ("idivl %[b]"
         : "=a"(q), "=d"(r)
         : "a"(a), [b] "r"(b));   // edx may contain garbage

// CORRECT — cltd sign-extends eax into edx:eax
__asm__ ("cltd\n\t"
         "idivl %[b]"
         : "=a"(q), "=d"(r)
         : "a"(a), [b] "r"(b)
         : "cc");
```

**Rule**: Always precede `idivl` with `cltd` (32-bit) or `idivq` with `cqto` (64-bit).
Omitting this produces wrong results on negative dividends.

---

## Labels in Inlined or Unrolled Blocks

### Duplicate Labels from Inlining

```cpp
// WRONG — if this function is inlined twice, .Lloop appears twice
__asm__ volatile (
    ".Lloop:\n\t"
    "decl %[n]\n\t"
    "jnz .Lloop"
    : [n] "+r"(n));

// CORRECT — %=  expands to a unique number per asm block instantiation
__asm__ volatile (
    ".Lloop%=:\n\t"
    "decl %[n]\n\t"
    "jnz .Lloop%="
    : [n] "+r"(n));
```

**Rule**: Always use `%=` suffix on local labels in inline asm.

---

## Intel Syntax Leakage

### Forgetting to Restore `.att_syntax prefix`

```cpp
// WRONG — Intel syntax is left active, corrupting subsequent compiler-generated asm
__asm__ (".intel_syntax noprefix\n\t"
         "mov rax, 42");

// CORRECT — always close the Intel syntax block
__asm__ (".intel_syntax noprefix\n\t"
         "mov rax, 42\n\t"
         ".att_syntax prefix");
```

**Rule**: Every `.intel_syntax noprefix` must be closed with `.att_syntax prefix`
before the asm block ends. Failure to do so corrupts all subsequent assembly
output from the compiler.

---

## Constraint Letter Mistakes

### Using `"r"` When a Fixed Register Is Required

```cpp
// WRONG — imulq (one-operand) requires operand in rax, result in rdx:rax
long lo, hi;
__asm__ ("imulq %[b]"
         : "=r"(lo), "=r"(hi)          // wrong: lo/hi may not be rax/rdx
         : "r"(a), [b] "r"(b));

// CORRECT
__asm__ ("imulq %[b]"
         : "=a"(lo), "=d"(hi)          // fixed to rax and rdx
         : "a"(a), [b] "r"(b)
         : "cc");
```

**Rule**: Consult the instruction reference for implicit register requirements.
`mul`, `div`, `imul` (one-operand), string ops, and `cpuid` all use fixed registers.

---

## Summary: Pre-Flight Checklist

Before submitting any inline asm block, verify:

1. **Operand direction**: In AT&T, is source on the left and dest on the right?
2. **Read-write operands**: Used `+r` (not `=r`) for operands that are both read and written?
3. **Early clobber**: Used `=&r` when output is written before all inputs consumed?
4. **`"cc"` clobber**: Listed `"cc"` for every arithmetic/logical instruction?
5. **`"memory"` clobber**: Listed `"memory"` for any untracked memory access?
6. **`volatile`**: Added where ordering or side effects matter?
7. **`%%` prefix**: Used `%%rax` (not `%rax`) for literal register names in AT&T?
8. **Labels**: Used `%=` suffix on all local labels?
9. **Intel syntax**: Closed with `.att_syntax prefix` if opened with `.intel_syntax noprefix`?
10. **Division**: Preceded `idiv`/`div` with `cltd`/`cqto` for correct sign extension?
11. **Callee-saved registers**: Not listed in clobbers unless pushed/popped manually?
12. **Fixed constraints**: Used `"a"`, `"d"`, `"c"` etc. for instructions requiring specific registers?

---

*This document will be updated with real incidents discovered during the agentASM project.*
