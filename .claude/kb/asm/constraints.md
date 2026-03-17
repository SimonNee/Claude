# Operand Constraints Reference

Constraints tell the compiler what kind of location an operand may occupy
and how it is used. Getting them wrong is the most common source of inline
asm bugs.

---

## Constraint Structure

```
[modifier] [letter(s)]
```

```cpp
: "=r"(out)    // modifier=, letter r
: "+m"(mem)    // modifier+, letter m
: "=&r"(tmp)   // modifiers= and &, letter r
```

---

## Output Modifiers

| Modifier | Meaning                                                        |
|----------|----------------------------------------------------------------|
| `=`      | Write-only. Compiler assumes value on entry is dead.           |
| `+`      | Read-write. Compiler reads the value in AND writes the result. |
| `=&`     | Write-only, early clobber. Written before inputs are consumed. |
| `+&`     | Read-write, early clobber.                                     |

### When to use `&` (early clobber)

Use `&` when your asm writes an output **before** it has finished reading
all inputs. Without it, the compiler may legally assign the output to the
same register as an input, causing a data hazard.

```cpp
// WRONG: without &, compiler might put 'tmp' in the same reg as 'b'
__asm__ ("movq %[b], %[tmp]\n\t"
         "addq %[a], %[tmp]"
         : [tmp] "=r"(tmp)       // <- should be =&r
         : [a] "r"(a), [b] "r"(b));

// CORRECT
__asm__ ("movq %[b], %[tmp]\n\t"
         "addq %[a], %[tmp]"
         : [tmp] "=&r"(tmp)
         : [a] "r"(a), [b] "r"(b));
```

---

## Constraint Letters

### General

| Letter | Meaning                                        | Example             |
|--------|------------------------------------------------|---------------------|
| `r`    | Any general-purpose register                   | `"r"(x)`            |
| `m`    | Memory location (address in any mode)          | `"m"(arr[i])`       |
| `i`    | Immediate integer (compile-time constant)      | `"i"(42)`           |
| `n`    | Immediate integer, not symbolic               | `"n"(255)`          |
| `g`    | Register, memory, or immediate (most flexible) | `"g"(x)`            |
| `p`    | A valid memory address                         | `"p"(ptr)`          |
| `0`–`9`| Matching constraint (same location as operand N)| `"0"(x)`           |

### Fixed Register Constraints (x86-64)

| Letter | Register | Common Use                         |
|--------|----------|------------------------------------|
| `a`    | rax/eax  | mul, div, cpuid, return values     |
| `b`    | rbx/ebx  | Base register, cpuid (ebx result)  |
| `c`    | rcx/ecx  | Counter, shift amount, 4th arg     |
| `d`    | rdx/edx  | mul/div high word, 3rd arg         |
| `S`    | rsi/esi  | String source, 2nd arg             |
| `D`    | rdi/edi  | String dest, 1st arg               |
| `A`    | rdx:rax  | 64-bit pair for 128-bit mul result |
| `x`    | xmm/ymm  | SSE/AVX register                   |

### Memory and Addressing

| Letter | Meaning                                          |
|--------|--------------------------------------------------|
| `m`    | Arbitrary memory reference                       |
| `o`    | Offsettable memory (can add small offset)        |
| `Q`    | Register usable as byte (al, bl, cl, dl)         |
| `R`    | Register usable as legacy memory base            |

---

## Matching Constraints

A matching constraint (`"0"`, `"1"`, etc.) means "same location as output operand N".
Used when you want an input initialised from the same register as an output.

```cpp
// result starts with value of 'x', asm modifies it in place
int result;
__asm__ ("addl %[y], %[x]"
         : [x] "=r"(result)
         : [y] "r"(y), "0"(x));   // input '0' matches output 0 -> result starts = x
```

This is equivalent to using `+r` when input and output are the same C variable:
```cpp
__asm__ ("addl %[y], %[x]"
         : [x] "+r"(x)
         : [y] "r"(y));
```

---

## The Clobber List

Registers and resources modified by the asm that are not listed as outputs.

```cpp
: : : "rax", "rcx", "memory", "cc"
```

### Register clobbers

List any register written by the asm that is not already an output operand.
Use the full 64-bit name (`"rax"` not `"eax"`) — GCC aliases automatically.

```cpp
__asm__ ("cpuid"
         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
         : "a"(leaf));
// All four fixed regs are outputs, nothing in clobber needed
```

```cpp
__asm__ ("imulq %2, %1\n\t"   // uses rdx implicitly for high word
         "movq  %%rdx, %0"
         : "=r"(hi), "+r"(lo)
         : "r"(mul)
         : "rdx");             // rdx is used but not listed as output
```

### `"cc"` — condition codes

List `"cc"` whenever the asm modifies the flags register (`rflags`).
Arithmetic and logical instructions all modify flags.
If your asm includes any `add`, `sub`, `and`, `or`, `xor`, `cmp`, `test`,
`shl`, etc., include `"cc"` in the clobbers.

```cpp
__asm__ ("addq %1, %0" : "+r"(x) : "r"(y) : "cc");
```

### `"memory"` — memory barrier

List `"memory"` when the asm reads or writes memory that is not listed
as an explicit operand.

Effect: the compiler flushes all cached register values to memory before
the asm, and reloads them after. This acts as a compiler memory barrier
(not a hardware barrier — add `mfence`/`lfence`/`sfence` for that).

```cpp
__asm__ volatile ("mfence" : : : "memory");
__asm__ volatile ("" : : : "memory");  // compiler barrier only (no-op)
```

Use `"memory"` when:
- The asm dereferences a pointer operand and writes to memory
- The asm calls into a function or changes memory via side channels
- You need to prevent the compiler reordering memory accesses around the block

---

## Common Constraint Combinations

| Pattern | Constraints | When to use |
|---------|-------------|-------------|
| Pure output | `"=r"(out)` | Result written, initial value irrelevant |
| In-place modify | `"+r"(x)` | x is both read and written |
| Scratch register | `"=&r"(tmp)` | Temporary, written before inputs consumed |
| Fixed reg, single | `"=a"(rax_out)` | Must be in rax (e.g. after mul) |
| 128-bit mul result | `"=A"(wide)` | rdx:rax pair |
| Memory operand | `"=m"(mem)` | Write directly to memory |
| Immediate constant | `"i"(N)` | Compile-time constant, becomes literal in asm |
| Same reg as output 0 | `"0"(init)` | Read-write alias without `+` |

---

## Quick Reference Card

```
OUTPUT MODIFIERS
  =       write-only (most common)
  +       read-write
  =&      write-only, early clobber (output written before all inputs read)
  +&      read-write, early clobber

CONSTRAINT LETTERS
  r       any GPR
  m       memory
  i       immediate constant
  g       register, memory, or immediate
  a       rax
  b       rbx
  c       rcx
  d       rdx
  S       rsi
  D       rdi
  x       xmm/ymm register
  0-9     match Nth output operand

CLOBBERS
  "rax"   specific register (use 64-bit name)
  "cc"    flags register (always list for arithmetic/logic ops)
  "memory" compiler memory barrier (global memory side effects)
```
