# Inline Assembly Patterns — C++ to __asm__ Phrasebook

Each entry shows: the C++ idiom, the AT&T inline asm form, the Intel inline asm form,
and notes on why the translation is written the way it is.

---

## Arithmetic

### Integer Add

```cpp
// C++
int result = a + b;

// AT&T
__asm__ ("addl %[b], %[a]"
         : [a] "+r"(a)
         : [b] "r"(b)
         : "cc");
result = a;

// Intel
__asm__ (".intel_syntax noprefix\n\t"
         "add %[a], %[b]\n\t"
         ".att_syntax prefix"
         : [a] "+r"(a)
         : [b] "r"(b)
         : "cc");
result = a;
```

Note: `+r` on `a` because it is both read and written. `"cc"` because add modifies flags.

---

### Integer Subtract

```cpp
// C++
int result = a - b;

// AT&T
__asm__ ("subl %[b], %[a]"
         : [a] "+r"(a)
         : [b] "r"(b)
         : "cc");
result = a;
```

---

### Multiply (32-bit, result fits in 32 bits)

```cpp
// C++
int result = a * b;

// AT&T
__asm__ ("imull %[b], %[a]"
         : [a] "+r"(a)
         : [b] "r"(b)
         : "cc");
result = a;
```

---

### Multiply Wide (64-bit × 64-bit → 128-bit result)

```cpp
// C++
__int128 result = (__int128)a * b;   // both int64_t

// AT&T — imulq uses rdx:rax implicitly
long long lo, hi;
__asm__ ("imulq %[b]"
         : "=a"(lo), "=d"(hi)
         : "a"(a), [b] "r"(b)
         : "cc");
// hi:lo = a * b
```

Note: `imulq` (one-operand form) always reads from `rax` and writes `rdx:rax`.
Must use fixed constraints `"a"` and `"d"`.

---

### Division (signed)

```cpp
// C++
int q = a / b;
int r = a % b;

// AT&T — idivl divides rdx:rax by operand
int quotient, remainder;
__asm__ ("cltd\n\t"             // sign-extend eax into edx:eax
         "idivl %[b]"
         : "=a"(quotient), "=d"(remainder)
         : "a"(a), [b] "r"(b)
         : "cc");
```

Note: `cltd` (AT&T) / `cdq` (Intel) sign-extends `eax` into `edx:eax` before division.
For 64-bit: `cqto` (AT&T) / `cqo` (Intel) extends `rax` into `rdx:rax`.

---

### Increment / Decrement

```cpp
// C++
x++;    // or ++x

// AT&T
__asm__ ("incl %[x]" : [x] "+r"(x) : : "cc");

// C++
x--;

// AT&T
__asm__ ("decl %[x]" : [x] "+r"(x) : : "cc");
```

Note: `inc`/`dec` are slightly smaller than `add/sub $1` and do not modify the carry flag.

---

### Negate

```cpp
// C++
x = -x;

// AT&T
__asm__ ("negl %[x]" : [x] "+r"(x) : : "cc");
```

---

## Bitwise Operations

### AND / OR / XOR

```cpp
// C++
x &= mask;

// AT&T
__asm__ ("andl %[mask], %[x]"
         : [x] "+r"(x)
         : [mask] "r"(mask)
         : "cc");

// C++
x |= mask;
__asm__ ("orl %[mask], %[x]" : [x] "+r"(x) : [mask] "r"(mask) : "cc");

// C++
x ^= mask;
__asm__ ("xorl %[mask], %[x]" : [x] "+r"(x) : [mask] "r"(mask) : "cc");
```

---

### Zero a Register (idiomatic xor)

```cpp
// C++
x = 0;

// AT&T — shorter encoding than mov $0, reg
__asm__ ("xorl %[x], %[x]" : [x] "=r"(x) : : "cc");
```

Note: `xor reg, reg` is the standard idiom for zeroing. It also clears flags.
Use `=r` not `+r` — the initial value of x is irrelevant.

---

### NOT (bitwise complement)

```cpp
// C++
x = ~x;

// AT&T
__asm__ ("notl %[x]" : [x] "+r"(x));
// Note: NOT does not modify flags — no "cc" needed
```

---

### Shift Left / Right

```cpp
// C++
x <<= 3;

// AT&T — immediate shift
__asm__ ("shll $3, %[x]" : [x] "+r"(x) : : "cc");

// C++
x >>= n;   // unsigned right shift

// AT&T — variable shift (count must be in cl)
__asm__ ("shrl %%cl, %[x]"
         : [x] "+r"(x)
         : "c"(n)
         : "cc");

// C++
x >>= n;   // signed (arithmetic) right shift
__asm__ ("sarl %%cl, %[x]"
         : [x] "+r"(x)
         : "c"(n)
         : "cc");
```

Note: Variable shifts require the count in `cl` (low byte of `rcx`).
Use `"c"(n)` to fix `n` into that register.

---

### Rotate Left / Right

```cpp
// C++  (no native operator — use asm or __builtin_rotateleft32)
uint32_t result = (x << n) | (x >> (32 - n));

// AT&T — rotate left by immediate
__asm__ ("roll $1, %[x]" : [x] "+r"(x) : : "cc");

// AT&T — rotate left by variable amount (cl)
__asm__ ("roll %%cl, %[x]"
         : [x] "+r"(x)
         : "c"(n)
         : "cc");
```

---

### Bit Test / Set / Reset (BT/BTS/BTR)

```cpp
// C++
bool bit = (x >> n) & 1;

// AT&T — bt sets carry flag to tested bit
int carry;
__asm__ ("btl %[n], %[x]\n\t"
         "setc %b[carry]"
         : [carry] "=r"(carry)
         : [x] "r"(x), [n] "r"(n)
         : "cc");

// bts: bit test and set, btr: bit test and reset
__asm__ ("btsl %[n], %[x]" : [x] "+r"(x) : [n] "r"(n) : "cc");
__asm__ ("btrl %[n], %[x]" : [x] "+r"(x) : [n] "r"(n) : "cc");
```

---

### Count Leading Zeros (BSR / LZCNT)

```cpp
// C++
int leading = __builtin_clz(x);

// AT&T — bsr: bit scan reverse (finds highest set bit, result is 31-clz)
int pos;
__asm__ ("bsrl %[src], %[dst]"
         : [dst] "=r"(pos)
         : [src] "r"(x)
         : "cc");
int leading_zeros = 31 - pos;

// AT&T — lzcnt (requires LZCNT feature flag: -mlzcnt or check cpuid)
__asm__ ("lzcntl %[src], %[dst]"
         : [dst] "=r"(leading_zeros)
         : [src] "r"(x)
         : "cc");
```

---

### Count Trailing Zeros (BSF / TZCNT)

```cpp
// C++
int trailing = __builtin_ctz(x);

// AT&T — bsf: bit scan forward (position of lowest set bit)
int pos;
__asm__ ("bsfl %[src], %[dst]"
         : [dst] "=r"(pos)
         : [src] "r"(x)
         : "cc");

// AT&T — tzcnt (BMI1, preferred when available)
__asm__ ("tzcntl %[src], %[dst]"
         : [dst] "=r"(trailing)
         : [src] "r"(x)
         : "cc");
```

---

### Population Count (POPCNT)

```cpp
// C++
int n = __builtin_popcount(x);

// AT&T
__asm__ ("popcntl %[src], %[dst]"
         : [dst] "=r"(n)
         : [src] "r"(x)
         : "cc");
```

---

## Memory Operations

### Load from Address

```cpp
// C++
int val = *ptr;

// AT&T — use "m" constraint for memory operand
int val;
__asm__ ("movl %[src], %[dst]"
         : [dst] "=r"(val)
         : [src] "m"(*ptr));
```

---

### Store to Address

```cpp
// C++
*ptr = val;

// AT&T
__asm__ ("movl %[src], %[dst]"
         : [dst] "=m"(*ptr)
         : [src] "r"(val));
```

---

### Prefetch

```cpp
// AT&T — hint: bring data into L1 cache
__asm__ ("prefetcht0 (%[addr])" : : [addr] "r"(ptr));
__asm__ ("prefetcht1 (%[addr])" : : [addr] "r"(ptr));   // L2
__asm__ ("prefetcht2 (%[addr])" : : [addr] "r"(ptr));   // L3
__asm__ ("prefetchnta (%[addr])" : : [addr] "r"(ptr));  // non-temporal (streaming)
```

---

### Memory Fence / Barrier

```cpp
// Full memory barrier (store + load)
__asm__ volatile ("mfence" : : : "memory");

// Store fence (ordering stores)
__asm__ volatile ("sfence" : : : "memory");

// Load fence (ordering loads)
__asm__ volatile ("lfence" : : : "memory");

// Compiler-only barrier (no hardware fence)
__asm__ volatile ("" : : : "memory");
```

---

## Control Flow

### Conditional Set (SETcc)

```cpp
// C++
int result = (a > b) ? 1 : 0;

// AT&T
__asm__ ("cmpl %[b], %[a]\n\t"   // sets flags: a - b
         "setg %b[out]"           // set byte if greater (signed)
         : [out] "=r"(result)
         : [a] "r"(a), [b] "r"(b)
         : "cc");
```

Common `SETcc` suffixes:
| Suffix | Condition            | Signed/Unsigned |
|--------|----------------------|-----------------|
| `sete` | equal (ZF=1)         | both            |
| `setne`| not equal            | both            |
| `setg` | greater              | signed          |
| `setge`| greater or equal     | signed          |
| `setl` | less                 | signed          |
| `setle`| less or equal        | signed          |
| `seta` | above                | unsigned        |
| `setae`| above or equal       | unsigned        |
| `setb` | below                | unsigned        |
| `setbe`| below or equal       | unsigned        |
| `sets` | sign flag set        | —               |
| `seto` | overflow flag set    | —               |

Note: `%b[out]` forces the byte-size register name (e.g. `al`, `r10b`).
The upper bytes of `result` are not cleared by `setcc` — zero-extend if needed:
`movzbl %b[out], %[out]`.

---

### Conditional Move (CMOVcc) — branchless select

```cpp
// C++
int result = (a > b) ? a : b;   // max

// AT&T — cmovg: move if greater (signed)
__asm__ ("cmpl %[b], %[a]\n\t"
         "cmovgl %[a], %[result]"
         : [result] "+r"(b)      // result starts as b
         : [a] "r"(a), [b] "r"(b)
         : "cc");
result = b;
```

CMOVcc has the same suffixes as SETcc. Use it to avoid branch misprediction
on data-dependent conditions.

---

### Loop with Counter

```cpp
// C++
for (int i = n; i > 0; i--) { /* body */ }

// AT&T — rcx/ecx as counter, LOOP decrements and jumps if nonzero
__asm__ volatile (
    "movl %[n], %%ecx\n\t"
    ".Lloop%=:\n\t"
    // ... loop body using other registers ...
    "decl %%ecx\n\t"
    "jnz .Lloop%=\n\t"
    :
    : [n] "r"(n)
    : "ecx", "cc"
);
```

Note: The `loop` instruction exists but is slow on modern CPUs — prefer explicit
`dec` + `jnz`. Use `%=` suffix on labels to avoid duplication in inlined code.

---

## Timing and Serialisation

### RDTSC — Read Timestamp Counter

```cpp
// Returns CPU cycle count (not wall time; not serialised)
uint64_t cycles;
uint32_t lo, hi;
__asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
cycles = ((uint64_t)hi << 32) | lo;
```

### RDTSCP — Serialising Timestamp Read

```cpp
// Serialised: all prior instructions complete before reading counter
uint32_t lo, hi, aux;
__asm__ volatile ("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
uint64_t cycles = ((uint64_t)hi << 32) | lo;
// aux contains IA32_TSC_AUX (processor ID on Linux)
```

### CPUID — CPU Feature Detection

```cpp
uint32_t eax, ebx, ecx, edx;
uint32_t leaf = 1;   // leaf 1: feature flags
__asm__ volatile ("cpuid"
                  : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                  : "a"(leaf)
                  : );
// ecx bit 23: POPCNT, bit 28: AVX
// edx bit 26: SSE2
```

CPUID also acts as a full serialising instruction — useful for fence before RDTSC.

---

## Atomic Operations

### Atomic Exchange

```cpp
// C++
int old = std::atomic_exchange(&x, val);

// AT&T — xchg is implicitly locked (no lock prefix needed)
int old;
__asm__ volatile ("xchgl %[val], %[mem]"
                  : [val] "+r"(val), [mem] "+m"(x)
                  : : "memory");
old = val;
```

### Atomic Compare-and-Swap

```cpp
// C++
bool ok = std::atomic_compare_exchange_strong(&x, &expected, desired);

// AT&T — cmpxchg: if [mem]==eax, write desired; else load [mem] into eax
int expected = old_val;
char success;
__asm__ volatile ("lock cmpxchgl %[desired], %[mem]\n\t"
                  "sete %[ok]"
                  : [mem] "+m"(x), "=a"(expected), [ok] "=r"(success)
                  : "a"(expected), [desired] "r"(desired)
                  : "cc", "memory");
```

### Atomic Add (fetch-and-add)

```cpp
// C++
int old = std::atomic_fetch_add(&x, val);

// AT&T — lock xaddl: atomically exchanges and adds
int old;
__asm__ volatile ("lock xaddl %[val], %[mem]"
                  : [val] "+r"(val), [mem] "+m"(x)
                  : : "cc", "memory");
old = val;   // val holds the old value after xadd
```

---

## SSE / SIMD Basics

### Load / Store Aligned 128-bit

```cpp
// C++
__m128 a = _mm_load_ps(ptr);   // ptr must be 16-byte aligned

// AT&T
__asm__ ("movaps (%[ptr]), %[dst]"
         : [dst] "=x"(a)
         : [ptr] "r"(ptr));

// Store
__asm__ ("movaps %[src], (%[ptr])"
         :
         : [src] "x"(a), [ptr] "r"(ptr)
         : "memory");
```

### Add Packed Floats

```cpp
// C++
__m128 c = _mm_add_ps(a, b);

// AT&T
__asm__ ("addps %[b], %[a]"
         : [a] "+x"(a)
         : [b] "x"(b));
c = a;
```

---

## Utility

### NOP / Pause

```cpp
// Compiler barrier with no hardware effect
__asm__ volatile ("" : : : "memory");

// Hardware NOP
__asm__ volatile ("nop");

// Pause (spin-wait hint — reduces power in spin loops)
__asm__ volatile ("pause");
```

### Byte Swap (BSWAP — endian conversion)

```cpp
// C++
uint32_t result = __builtin_bswap32(x);

// AT&T
__asm__ ("bswapl %[x]" : [x] "+r"(x));
result = x;
```

### Load Effective Address (LEA — fast multiply/add)

```cpp
// C++
int result = a * 3;   // 3 = 2 + 1

// AT&T — lea: computes address, uses addressing modes for arithmetic
__asm__ ("leal (%[a], %[a], 2), %[out]"  // out = a + a*2 = 3*a
         : [out] "=r"(result)
         : [a] "r"(a));

// C++
int result = a * 5;   // 5 = 4 + 1
__asm__ ("leal (%[a], %[a], 4), %[out]"  // out = a + a*4 = 5*a
         : [out] "=r"(result)
         : [a] "r"(a));
```

LEA addressing modes: `(base, index, scale)` where scale ∈ {1,2,4,8}.
Result = base + index × scale. Base and index may be the same register.

---

## Size Suffix Reference (AT&T)

| Suffix | C type          | Bits | Instruction example |
|--------|-----------------|------|---------------------|
| `b`    | char / uint8_t  | 8    | `movb`, `addb`      |
| `w`    | short / uint16_t| 16   | `movw`, `addw`      |
| `l`    | int / uint32_t  | 32   | `movl`, `addl`      |
| `q`    | long / uint64_t | 64   | `movq`, `addq`      |

In Intel syntax there is no suffix; operand size is inferred from register names
or explicit `byte ptr`, `word ptr`, `dword ptr`, `qword ptr` memory qualifiers.
