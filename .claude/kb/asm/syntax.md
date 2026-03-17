# GCC Inline Assembly Syntax

## The Two Forms

### 1. Basic (no operands)

```cpp
__asm__("nop");
__asm__("cli");          // disable interrupts (kernel only)
```

No input/output operands. Compiler treats this as a black box — it cannot reason about
what registers are read or written. Rarely correct for replacing function bodies.

### 2. Extended (always prefer this)

```cpp
__asm__ volatile (
    "template"
    : outputs
    : inputs
    : clobbers
);
```

All four parts are optional but the colons are positional.

```cpp
__asm__("addl %1, %0" : "+r"(x) : "r"(y));          // output + input
__asm__("cpuid"       : : : "eax","ebx","ecx","edx"); // clobbers only
__asm__("nop"         : : :);                          // no operands, no clobbers
```

---

## The Template String

Instructions go here as a string literal. Multiple instructions are separated by `\n\t`:

```cpp
__asm__ volatile (
    "movq %1, %0\n\t"
    "addq $1, %0\n\t"
    "shlq $2, %0"
    : "=r"(result)
    : "r"(value)
);
```

`\n` produces a newline in the assembly output; `\t` aligns operands in the listing.
Both are cosmetic but conventional — always include them between instructions.

### Operand References

Two styles — **choose one per asm block**, do not mix:

**Positional** (numbered):
```cpp
"addq %1, %0"   // %0 = first operand, %1 = second
```

**Named** (preferred for readability):
```cpp
"addq %[src], %[dst]"
: [dst] "+r"(x)
: [src] "r"(y)
```

Named references eliminate off-by-one errors when adding or removing operands.

---

## AT&T vs Intel Syntax

GCC defaults to **AT&T syntax**. Intel syntax requires a pragma or directive.

### AT&T (GCC default)
- Source comes **last**: `addq %src, %dst` means `dst = dst + src`
- Register names prefixed with `%`: `%rax`, `%eax`
- Immediates prefixed with `$`: `$42`, `$0xFF`
- Operand size suffixed to mnemonic: `movq` (64), `movl` (32), `movw` (16), `movb` (8)

```cpp
__asm__ ("movq %1, %0\n\t"
         "addq $1, %0"
         : "=r"(result)
         : "r"(value));
```

### Intel Syntax (via pragma)
- Destination comes **first**: `add rax, rbx` means `rax = rax + rbx`
- No `%` prefix on registers, no `$` prefix on immediates
- No size suffix — use `qword ptr`, `dword ptr` etc. for ambiguous memory refs

```cpp
__asm__ (
    ".intel_syntax noprefix\n\t"
    "mov rax, %[src]\n\t"
    "add rax, 1\n\t"
    "mov %[dst], rax\n\t"
    ".att_syntax prefix"
    : [dst] "=r"(result)
    : [src] "r"(value)
    : "rax"
);
```

**Always restore `.att_syntax prefix` at the end.** Leaving intel syntax active
corrupts the compiler's own subsequent assembly output.

### Choosing a Syntax

| Situation                            | Prefer      |
|--------------------------------------|-------------|
| Short blocks, standard operations    | AT&T        |
| SIMD / SSE / AVX intrinsic-style     | Intel       |
| Porting Intel manual pseudocode      | Intel       |
| Team familiarity is AT&T             | AT&T        |
| Mixing with existing AT&T code       | AT&T        |

---

## The `volatile` Keyword

```cpp
__asm__ volatile ("...");
```

Without `volatile`, the compiler may:
- Move the block (reorder it relative to other code)
- Eliminate it entirely if outputs appear unused
- Duplicate it (e.g. in loop unrolling)

**Use `volatile` when the asm has side effects** beyond its explicit outputs:
- Modifying memory not listed in outputs
- I/O port access
- Serialising instruction ordering (e.g. MFENCE, LFENCE)
- RDTSC timing

If the asm is a pure computation (outputs depend only on inputs, no side effects),
omit `volatile` so the compiler can optimise freely.

---

## Labels and Local Jumps

GCC local labels use `%=` to generate a unique number per asm block instance:

```cpp
__asm__ volatile (
    "testq %[val], %[val]\n\t"
    "jz .Lskip%=\n\t"
    "addq $1, %[val]\n\t"
    ".Lskip%=:\n\t"
    : [val] "+r"(x)
);
```

Do **not** use plain `.Lskip` labels — if the asm block appears in an inlined or
unrolled context, duplicate label definitions cause assembler errors.

---

## Output-Only vs Read-Write Operands

```cpp
// Output only (=): compiler assumes NOT read, writes result
: "=r"(out)

// Read-write (+): compiler reads initial value AND writes result
: "+r"(inout)

// Early clobber (&): written before all inputs are consumed
: "=&r"(tmp)   // safe when tmp and inputs share no register
```

---

## Full Template

```cpp
ReturnType func(ParamType param) {
    ReturnType result;
    __asm__ volatile (
        // instructions
        "instruction1 %[in], %[out]\n\t"
        "instruction2 %[out], %[out]"
        // outputs
        : [out] "=r"(result)    // write-only output
        // inputs
        : [in]  "r"(param)      // read-only input
        // clobbers
        : "cc"                  // flags register modified
    );
    return result;
}
```
