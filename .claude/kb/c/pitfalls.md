# agentC — C Pitfalls

Systematic errors in C, especially in performance-critical systems code. Sources: SEI CERT C, C17 standard, Linux kernel coding style, GCC documentation.

---

## Pitfall 1 — Integer Promotion and Implicit Conversion

C promotes integer types smaller than `int` to `int` (or `unsigned int`) in all arithmetic expressions. This is silent and pervasive.

```c
uint8_t a = 200, b = 100;
uint8_t result = a + b;   // a and b promoted to int; sum is 300; truncated on assignment
```

**The rule**: every arithmetic expression involving types smaller than `int` is computed in `int` or `unsigned int`. The result is only narrowed on assignment — silently, with no warning by default.

**Detection**: `-Wconversion` and `-Wsign-conversion` catch narrowing assignments. They do not catch all promotion surprises — they only flag where the narrowed result is stored.

**In hot-path code**: every intermediate result should be the intended type. Assign to the correct type immediately; do not rely on implicit narrowing.

---

## Pitfall 2 — Signed/Unsigned Comparison

Comparing a signed and unsigned integer is undefined or implementation-defined when the signed value is negative. Even when not negative, the comparison may behave unexpectedly because the signed operand is converted to unsigned.

```c
int i = -1;
size_t n = 10;
if (i < n) { ... }   // i converted to size_t: becomes very large; condition is FALSE
```

**Detection**: `-Wsign-compare` (included in `-Wall`).

**Fix**: use consistent types. In systems code, prefer `int32_t`/`uint32_t` explicitly over mixing `int` and `size_t`. Never compare loop indices (often `int`) against container sizes (often `size_t`) without casting intentionally.

---

## Pitfall 3 — Undefined Behaviour in Integer Arithmetic

Signed integer overflow is undefined behaviour in C. The compiler is permitted to assume it never happens — and will optimise accordingly, eliminating overflow checks.

```c
int x = INT_MAX;
if (x + 1 < 0) { handle_overflow(); }  // UB: compiler may delete this branch entirely
```

Unsigned overflow is defined (wraps modulo 2^N) but can still produce surprises when mixed with signed types.

**Shift rules**:
- Shifting by a negative amount or by ≥ width of the type: undefined behaviour
- Left-shifting a negative signed value: undefined behaviour
- Left-shifting into the sign bit: undefined behaviour (until C++14; implementation-defined in C)

**Fix**: use unsigned types for bit manipulation. Use `UINT32_MAX` bounds checks before arithmetic. Compile with `-fsanitize=undefined` during development.

---

## Pitfall 4 — Strict Aliasing Violations

The C strict aliasing rule: the compiler assumes that pointers of different types do not alias. Violating this allows the compiler to reorder memory accesses in ways that break correctness.

```c
float f = 3.14f;
uint32_t *p = (uint32_t *)&f;   // strict aliasing violation
uint32_t bits = *p;              // compiler may not see this as reading f's memory
```

**Safe exception**: `char *` and `unsigned char *` may alias any type. Use `memcpy` for type-punning.

```c
uint32_t bits;
memcpy(&bits, &f, sizeof(bits));  // defined; compiler will optimise to a register move
```

**Detection**: `-fstrict-aliasing -Wstrict-aliasing=2` (enabled by `-O2` by default).

---

## Pitfall 5 — `sizeof` on Pointers vs Arrays

`sizeof` on an array name gives the array size in bytes. `sizeof` on a pointer gives the pointer size (8 bytes on 64-bit), not the array it points to.

```c
int arr[100];
void f(int *p) {
    size_t n = sizeof(p) / sizeof(p[0]);  // 8/4 = 2, not 100
}
```

**Fix**: pass the count explicitly. Never use `sizeof(ptr) / sizeof(ptr[0])` for a parameter. The idiom is only valid on a locally declared array in the same scope.

---

## Pitfall 6 — Struct Padding and Alignment Surprises

The compiler inserts padding between struct fields to satisfy alignment requirements. Field order determines padding amount. Unexpected padding inflates struct size and wastes cache lines.

```c
struct bad {
    char   a;     // 1 byte
    // 7 bytes padding
    double b;     // 8 bytes
    char   c;     // 1 byte
    // 7 bytes padding
};  // sizeof == 24

struct good {
    double b;     // 8 bytes
    char   a;     // 1 byte
    char   c;     // 1 byte
    // 6 bytes padding
};  // sizeof == 16
```

**Rule**: order fields largest to smallest. Verify with `static_assert(sizeof(struct T) == N, "layout changed")`.

**Never use `__attribute__((packed))`** on performance-critical structs — unaligned loads are slow or illegal on some architectures.

---

## Pitfall 7 — `volatile` Misuse in Performance Code

`volatile` prevents the compiler from caching a variable in a register — it forces every access to go to memory. It does NOT provide atomicity, does NOT provide memory ordering, and does NOT prevent the CPU from reordering accesses.

`volatile` is correct for: memory-mapped hardware registers, `setjmp`/`longjmp` interaction, signal handlers.

`volatile` is incorrect for: inter-thread synchronisation (use `_Atomic`), preventing optimisation of benchmarks (use a proper sink pattern), ensuring writes are visible to other cores (use memory barriers).

---

## Pitfall 8 — String and Memory Function Misuse

- `strcpy`/`strcat`: no bounds checking — use `strlcpy`/`strlcat` or `snprintf`
- `strncpy`: does not null-terminate if source is longer than n — not a safe replacement for `strcpy`
- `memcmp` for struct comparison: padding bytes are uninitialised — use field-by-field comparison or `memset` the struct to zero before use
- `sizeof` on a `char[]` parameter: gives pointer size, not string length — use `strlen`

---

## Pitfall 9 — Unintended Function-Like Macro Expansion

Macros expand textually. Side effects in macro arguments are evaluated multiple times.

```c
#define MAX(a, b) ((a) > (b) ? (a) : (b))
int x = MAX(i++, j++);  // i++ or j++ evaluated twice
```

**Fix**: use `static inline` functions instead of function-like macros for all performance-critical code. `static inline` gives the same zero-overhead guarantee with correct semantics.

---

## Pitfall 10 — Omitting `const` on Read-Only Pointer Parameters

Without `const`, the compiler cannot place the pointed-to data in read-only memory, cannot cache it across calls, and cannot tell callers the data will not be modified. This forecloses optimisations.

```c
void process(const uint32_t *data, size_t n);  // correct
void process(uint32_t *data, size_t n);         // compiler assumes data may be modified
```

**Rule**: all pointer parameters that are not written through must be `const`. This is enforced by `-Wwrite-strings` and good practice.

---

## Pitfall 11 — Ignoring Return Values

C functions commonly return error codes or lengths. Ignoring them silently discards error information.

```c
fwrite(buf, 1, n, f);   // return value (bytes written) ignored — short write undetected
```

**Detection**: `__attribute__((warn_unused_result))` or `[[nodiscard]]` (C23). Compile with `-Werror=unused-result` for critical I/O paths.

---

## Pitfall 12 — Compiler Flag Discipline

Minimum flags for performance-critical C:

```
-std=c17
-O2 -march=native
-Wall -Wextra
-Wconversion -Wsign-conversion -Wsign-compare
-Wstrict-aliasing=2
-fsanitize=undefined   (development only, remove for benchmarks)
```

`-Wconversion` is not included in `-Wall` or `-Wextra` — it must be added explicitly. Without it, the no-cast rule cannot be mechanically enforced.

`-march=native` enables `TZCNT`/`LZCNT` (replacing `BSF`/`BSR` + zero-guard), AVX2, and other ISA-specific instructions. Required for `__builtin_ctzll` to compile to a single instruction.

---

## Pitfall 13 — VLAs (Variable Length Arrays)

VLAs (`int arr[n]` where n is a runtime value) allocate on the stack with no bounds checking. Stack overflow is silent. GCC 14+ warns with `-Wvla`. Avoid in systems code — use arena allocation or a fixed-size array with a `static_assert` on the maximum.
