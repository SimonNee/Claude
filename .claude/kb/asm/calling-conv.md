# Calling Conventions — System V AMD64 ABI

This is the ABI used on Linux x86-64, FreeBSD, macOS, and most Unix-like systems.
Relevant when replacing C++ function bodies with inline asm — you must honour
the same register preservation rules the compiler expects.

---

## Argument Passing

### Integer / Pointer Arguments (in order)

```
rdi  rsi  rdx  rcx  r8   r9   [stack...]
 1    2    3    4    5    6      7+
```

Arguments beyond the 6th are pushed on the stack in right-to-left order
before the call. The caller cleans the stack.

### Floating-Point / SSE Arguments (in order)

```
xmm0  xmm1  xmm2  xmm3  xmm4  xmm5  xmm6  xmm7
  1     2     3     4     5     6     7     8
```

Up to 8 float/double arguments pass in XMM registers. Beyond 8: stack.

### Variadic Functions

`al` (the low byte of `rax`) must contain the number of XMM registers used
for variadic arguments before calling a variadic function.

---

## Return Values

| Type                  | Register(s)       |
|-----------------------|-------------------|
| int, long, pointer    | rax               |
| 128-bit integer       | rdx:rax           |
| float, double         | xmm0              |
| long double (x87)     | st0               |
| struct ≤ 16 bytes     | rax + rdx         |
| struct > 16 bytes     | caller allocates, rdi points to it |

---

## Register Preservation

### Caller-Saved (Scratch) — not preserved across calls

The caller must save these before a call if it needs them afterward.
Inside inline asm, you may freely use these as scratch **if you list them in clobbers**.

```
rax  rcx  rdx  rsi  rdi  r8   r9   r10  r11
xmm0 – xmm15  (all XMM/YMM caller-saved)
```

### Callee-Saved — must be preserved across calls

If your inline asm uses these, save them first (push/pop) and do **not**
list them as clobbers (listing them as clobbers tells the compiler they are
destroyed, which violates the ABI).

```
rbx  rbp  r12  r13  r14  r15
```

```cpp
// Correct: preserve rbx explicitly when needed
__asm__ volatile (
    "pushq %%rbx\n\t"
    "movq  %[in], %%rbx\n\t"
    "addq  $1, %%rbx\n\t"
    "movq  %%rbx, %[out]\n\t"
    "popq  %%rbx"
    : [out] "=r"(result)
    : [in]  "r"(value)
    // rbx NOT in clobbers — we preserved it ourselves
);
```

### Stack Pointer

`rsp` must always be 16-byte aligned before a `call` instruction.
Never modify `rsp` in inline asm unless you restore it exactly.

---

## Red Zone

The 128 bytes **below** `rsp` (i.e. `rsp - 128` to `rsp - 1`) are the red zone.
Leaf functions (those that make no calls) may use this space for temporaries
without adjusting `rsp`. Signal handlers and interrupts do not clobber it.

In inline asm inside a non-leaf function, the red zone is **not** available —
calls from the compiler may clobber it. If you need scratch memory, allocate
it on the stack properly (`subq $N, %rsp` / `addq $N, %rsp`).

---

## Direction Flag

The direction flag (`DF` in rflags) must be clear (`cld` = 0) on function entry
and exit. String instructions (`rep movsb`, etc.) behave incorrectly if DF is set.
If your asm uses string instructions that might set DF, clear it before returning.

---

## Summary Table

```
ARGUMENT REGISTERS (integer/ptr)     ARGUMENT REGISTERS (float)
  1: rdi                               1: xmm0
  2: rsi                               2: xmm1
  3: rdx                               3: xmm2
  4: rcx                               4: xmm3
  5: r8                                5: xmm4
  6: r9                                6: xmm5
  7+: stack                          7+: xmm6, xmm7, then stack

RETURN VALUES
  integer/ptr: rax
  float/double: xmm0

CALLER-SAVED (safe to clobber):
  rax rcx rdx rsi rdi r8 r9 r10 r11
  xmm0–xmm15

CALLEE-SAVED (must preserve):
  rbx rbp r12 r13 r14 r15
```
