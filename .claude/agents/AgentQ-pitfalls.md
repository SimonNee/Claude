# Q Language Pitfalls for Programmers from Other Languages

**CRITICAL**: Read this document BEFORE writing any q code. It exists because of documented systematic failures.

---

## Table of Contents

1. [Function Application Syntax](#critical-function-application-syntax)
2. [Variable Shadowing in Select Statements](#variable-shadowing-in-select-statements)
3. [Global Assignment Operator (`::`)](#global-assignment-operator-)
4. [Table Manipulation: Clearing vs Deleting](#table-manipulation-clearing-vs-deleting)
5. [Timestamp Arithmetic and Type Preservation](#timestamp-arithmetic-and-type-preservation)
6. [Type System Surprises](#type-system-surprises)
7. [Single-Row Table Column Extractions](#single-row-table-column-extractions)
8. [Testing for Sorted Order](#testing-for-sorted-order)
9. [Circular Testing Anti-Pattern](#circular-testing-anti-pattern)
10. [List vs Atom Context](#list-vs-atom-context)
11. [Block Comment Syntax](#block-comment-syntax)
12. [`.z.ph` Input Format](#zph-input-format)
13. [`@[f;x;h]` Iterates Over Lists](#fxh-iterates-over-lists)
14. [Single Character String is a Char Atom](#single-character-string-is-a-char-atom)
15. [Boolean and Conditional Logic](#boolean-and-conditional-logic)
16. [Precision and Floating Point](#precision-and-floating-point)
17. [Iteration and Vector Operations](#iteration-and-vector-operations)
18. [Table and Column Operations](#table-and-column-operations)
19. [String and Symbol Confusion](#string-and-symbol-confusion)
20. [Within and Range Checks](#within-and-range-checks)
21. [Reserved Keywords as Variable Names](#reserved-keywords-as-variable-names)

---

## Critical: Function Application Syntax

### THE FUNDAMENTAL RULE

**In q, parentheses are for GROUPING, not function calls.**

```q
/ WRONG - This is a syntax error
all(x)          / Tries to apply noun 'all' to expression (x)
sum(vals)       / Tries to apply noun 'sum' to expression (vals)
result:myFunc(arg1, arg2)
count(select from trade)

/ CORRECT - Function application in q
all x           / Function applied with space
all[x]          / Function applied with brackets
sum vals        / Function with space
result:myFunc[arg1; arg2]
result:myFunc arg1        / single arg can omit brackets
count select from trade   / prefix notation
```

### Why This Matters

Coming from Python, JavaScript, Java, C, or C++, you've internalized `f(x)` for function calls. **This pattern does NOT work in q.**

q evaluates right-to-left and uses:
- **Whitespace** for function application: `f x`
- **Square brackets** for explicit application: `f[x]`
- **Parentheses** only for grouping: `(2+3)*4`

### Examples of Correct Usage

```q
/ Single argument
all x                    / all applied to x
all[x]                   / same thing
count trade              / count applied to trade
max prices               / max applied to prices

/ Multiple arguments - MUST use brackets
within[vals; (lo;hi)]    / correct: within with bounds tuple
@[table;idx;:;newval]    / amend: 4 arguments require brackets

/ Composition
all not null x           / right-to-left: null x, then not, then all
all[not null x]          / explicit grouping, same result
```

### Pattern Recognition Trap

You learned `f(x)` through thousands of repetitions. q uses different syntax. When you write test code or validation, your muscle memory defaults to `f(x)`. **Actively fight this habit.**

### Detection

If you see **any** instance of `identifier(`, immediately check if it's a function call that should use brackets `[` instead.

### Debug Checklist

If you get a type or rank error with a function call:
1. Check if you wrote `f(x)` instead of `f x` or `f[x]`
2. Look for any parentheses immediately after a function name
3. Remember: parentheses are ONLY for grouping expressions, never for calling functions

---

## Variable Shadowing in Select Statements

### The Error

Parameter names that shadow column names cause ambiguity in `select` statements.

```q
/ WRONG - parameter 'sym' shadows column 'sym'
getTradesBySym:{[sym]
  select from trade where sym in sym  / which 'sym' is which?
 }

/ CORRECT - rename parameter to avoid shadowing
getTradesBySym:{[syms]
  select from trade where sym in syms  / clear: column 'sym' in parameter 'syms'
 }
```

**Real Incident (Iteration 3)**: Original implementation used parameter name `sym`, which shadowed the `sym` column in the trade table.

### Detection

When writing a function that queries a table:
1. List all column names in the table
2. Check if any parameter names match
3. Rename parameters to avoid conflicts

**Rule**:
- Never use parameter names that match column names in tables you're querying
- Prefer plural forms for list parameters: `syms`, `times`, `prices`
- Use descriptive prefixes: `reqSym`, `targetSym`, `filterSym`

---

## Global Assignment Operator (`::`)

### Multiple Uses of `::`

The `::` operator has several meanings in q:

1. **Global assignment in functions**:
   ```q
   f:{[] x::42}  / assigns 42 to global x
   ```

2. **View definition**:
   ```q
   viewName::select from table where condition  / creates a view
   ```

3. **Generic null**:
   ```q
   ::  / the generic null (type -101h)
   ```

4. **Amend operator** (as right argument):
   ```q
   @[x;i;:;::]  / set x[i] to null
   ```

### The Table Type Bug

```q
/ WRONG - breaks table type
trade::0#trade  / trade becomes type 0h (mixed list), not 98h (table)

/ CORRECT - use delete to clear table
delete from `trade  / preserves table type (98h)

/ ALSO CORRECT - explicit assignment with schema
trade:flip `time`sym`price`size!(`timestamp$();`symbol$();`float$();`long$())
```

**Real Incident (Iteration 3)**: Used `trade::0#trade` to clear the table in tests. This broke the table type - `trade` became `0h` (mixed list) instead of `98h` (table), causing subsequent inserts to fail.

**Type Check**:
```q
q)trade:([]time:`timestamp$(); sym:`symbol$(); price:`float$(); size:`long$())
q)type trade
98h  / table

q)trade::0#trade
q)type trade
0h   / mixed list - BROKEN!

q)delete from `trade
q)type trade
98h  / still a table - CORRECT
```

**Rule**:
- To clear a table: `delete from \`tableName`
- Never use `table::0#table` for clearing - it breaks table type
- `0#table` works fine for local variables, but `::` assignment corrupts it

---

## Table Manipulation: Clearing vs Deleting

### The Error

Confusing table clearing operations leads to type corruption.

```q
/ DANGEROUS - may break table type
trade::0#trade

/ CORRECT - preserves table type
delete from `trade

/ ALSO CORRECT - save/restore pattern for testing
origData:select from trade;  / save as table
delete from `trade;           / clear
/ ... do tests ...
`trade insert origData;       / restore
```

**Real Incident (Iteration 3)**: Test suite tried to clear and restore the `trade` table multiple times. Using `::` assignment broke the table type.

### Operations Comparison

| Operation | Syntax | Preserves Type | Use Case |
|-----------|--------|----------------|----------|
| Clear table | `delete from \`table` | Yes | Production code |
| Save data | `saved:select from table` | Yes | Testing backup |
| Restore data | `` `table insert saved`` | Yes | Testing restore |
| Reinitialize | `table::0#table` | No | AVOID |
| Schema-based clear | `table:0#table` | Yes | If table is in local scope |

### Testing Pattern

```q
/ CORRECT pattern for test isolation
test1:{[]
  / Save original state
  origData:select from trade;

  / Clear table
  delete from `trade;

  / Insert test data
  `trade insert testData;

  / Run tests
  result:someTest[];

  / Restore original state
  delete from `trade;
  `trade insert origData;

  result
 }
```

---

## Timestamp Arithmetic and Type Preservation

### The Error

Arithmetic operations on timestamps can produce unexpected types.

```q
/ WRONG - produces float, not timespan
maxTime:2024.01.15D10:00:00.000000000
minTime:2024.01.15D09:00:00.000000000
halfDiff:(maxTime-minTime)%2  / this is a FLOAT!
midTime:minTime+halfDiff      / type error

/ CORRECT - explicit type conversion
halfDiff:0D00:00:00.000000001*(`long$(maxTime-minTime))div 2
midTime:minTime+halfDiff
```

**Real Incident (Iteration 3)**: When computing time range midpoint, `(maxTime-minTime)%2` returned a float, not a timespan.

### Type Chart

| Operation | Result Type | Example |
|-----------|-------------|---------|
| `timestamp - timestamp` | timespan | `2024.01.15D10:00:00 - 2024.01.15D09:00:00` → `0D01:00:00` |
| `timespan % int` | **float** (not timespan!) | `0D01:00:00 % 2` → `1.8e+12` |
| `timespan div int` | long | `0D01:00:00 div 2` → `1800000000000` |
| `timestamp + timespan` | timestamp | `2024.01.15D09:00:00 + 0D00:30:00` → `2024.01.15D09:30:00` |
| `timestamp + long` | ERROR | Type mismatch |

### Type Checking

```q
q)maxTime:2024.01.15D10:00:00.000000000
q)minTime:2024.01.15D09:00:00.000000000
q)diff:maxTime-minTime
q)type diff
-16h  / timespan

q)type diff%2
-9h   / float - NOT a timespan!

q)type diff div 2
-7h   / long - NOT a timespan!

q)type 0D00:00:00.000000001*diff div 2
-16h  / timespan - CORRECT!
```

### Solution Pattern

```q
/ Convert to nanoseconds (long), divide, convert back to timespan
halfDiff:0D00:00:00.000000001*(`long$(maxTime-minTime))div 2
midTime:minTime+halfDiff
```

---

## Type System Surprises

### Chars vs Symbols

```q
/ Meta table returns CHAR types, not symbols
m:meta trade
m`t                      / type column - returns CHAR vector like "psf"

/ WRONG comparisons
`p=m`t                   / comparing symbol to char - always false
m[`time]`t ~ `p         / comparing char to symbol - false

/ CORRECT comparisons
"p"=first exec t from m where c=`time    / char comparison
"psf"~exec t from m                      / char vector comparison
```

**Rule**: `meta table` returns chars in the `t` column, not symbols. Use `"p"`, `"s"`, `"f"`, `"j"`, not `` `p``, `` `s``, etc.

### Common Type Chars

| Char | Type | Example |
|------|------|---------|
| `"b"` | boolean | `1b 0b` |
| `"x"` | byte | `0x2a` |
| `"h"` | short | `42h` |
| `"i"` | int | `42i` |
| `"j"` | long | `42` or `42j` |
| `"e"` | real | `42e` |
| `"f"` | float | `42f` or `42.0` |
| `"c"` | char | `"a"` |
| `"s"` | symbol | `` `abc`` |
| `"p"` | timestamp | `2024.01.15D09:30:00.000000000` |
| `"m"` | month | `2024.01m` |
| `"d"` | date | `2024.01.15` |
| `"z"` | datetime | `2024.01.15T09:30:00.000` |
| `"n"` | timespan | `0D01:30:00.000000000` |
| `"u"` | minute | `09:30` |
| `"v"` | second | `09:30:00` |
| `"t"` | time | `09:30:00.000` |

### Null Comparisons

```q
/ Nulls don't equal nulls
0N = 0N                  / 0b (false!)
null 0N                  / 1b (correct way)

/ Check for nulls
not null x               / correct
x = null x               / WRONG - returns false for nulls
```

**Rule**: Use `null x` to test for nulls, never `x = null_value`.

---

## Single-Row Table Column Extractions

### The Error

Expecting scalar values from single-row table column extractions.

```q
/ Single-row table column access returns a LIST, not scalar
q)t:([] sym:`AAPL; price:100.0)
q)t`sym
,`AAPL  / this is a LIST, not the scalar `AAPL

q)t[`sym]
,`AAPL  / also a list

q)t`sym ~ `AAPL  / FALSE! List doesn't match scalar
```

In q, table columns are always lists, even if the table has only one row. Developers from SQL/Pandas backgrounds expect scalar results.

### Correct Extraction

```q
/ Extract scalar
q)first t`sym
`AAPL   / scalar symbol

q)first t`sym ~ `AAPL
1b      / TRUE

q)t[0;`sym]
`AAPL   / index into the table directly

/ Multiple items with 'in'
t`sym in `AAPL`MSFT     / returns list of booleans: 1b
all t`sym in `AAPL`MSFT / returns scalar: 1b
```

### Detection Pattern

When you see code like:
```q
x:table`column
if[x=someValue; ...]  / BUG if table has 1 row - comparing list to atom
```

Fix it:
```q
x:first table`column   / or table[0;`column]
if[x=someValue; ...]   / comparing atom to atom
```

**Rule**: Table column indexing always returns lists, even for single rows. Use `first`, `last`, or index directly with `[row;col]` to get a scalar.

---

## Testing for Sorted Order

### The Error

Using `prior` with comparison operators to test if a list is sorted.

```q
/ PROBLEMATIC - edge case with first element
q)x:1 2 3 4 5
q)all(<=)prior x
0b  / FALSE! Because prior x has null for first element

q)prior x
0N 1 2 3 4

q)x>=prior x
0000111b  / first comparison is 1>=0N which is false

/ WRONG: Ascending test using prior
all(<=)prior x           / FALSE for any list! First element always fails.
```

### The Rule

The `prior` keyword shifts the list and fills the first position with null. Comparisons with null produce unexpected results. For sorted checks, compare with `asc x`.

**Don't use**:
```q
all (<=)prior x  / edge case with null
all x<=1_x,0W    / convoluted
```

**Do use**:
```q
(asc x)~x   / ascending
(desc x)~x  / descending
x~asc x     / same as first, alternative form
```

This is more idiomatic, clearer in intent, and has no edge cases.

### Reference

See [Q Phrasebook - Testing](test.md) for more idiomatic testing patterns.

---

## Circular Testing Anti-Pattern

### The Error

Testing an implementation using the same formula as the implementation itself.

```q
/ IMPLEMENTATION
avgPrice:{[trades] avg trades`price}

/ WRONG - circular test
test:{[]
  result:avgPrice[testData];
  expected:avg testData`price;  / SAME FORMULA!
  result~expected  / always true, even if formula is wrong!
 }
```

If the implementation is wrong, the test will still pass because both use the wrong formula.

### Real-World Example: VWAP

```q
/ IMPLEMENTATION
vwap:{[trades] (sum trades[`price]*trades[`size])%sum trades`size}

/ WRONG - circular
test_vwap_wrong:{[]
  result:vwap[testTrades];
  expected:(sum testTrades[`price]*testTrades[`size])%sum testTrades`size;
  result~expected  / always true, even if formula is wrong!
 }

/ CORRECT - independent validation with hand-calculated result
test_vwap_correct:{[]
  trades:([]price:10.0 20.0; size:100 200);
  / Hand calculation: (10*100 + 20*200) / (100+200) = 5000/300 = 16.666...
  expected:16.66666667;
  result:vwap[trades];
  (abs result-expected)<0.00001  / independent check
 }
```

### Good Testing Patterns

**Test with independent properties**, not the same implementation:

```q
/ Pattern 1: Known values (hand-calculated)
test_with_known:{[]
  trades:([]price:100.0; size:10);
  avgPrice[trades]~100.0  / we know the answer
 }

/ Pattern 2: Properties / invariants
test_property:{[]
  result:avgPrice[testTrades];
  / Property: average should be between min and max
  (result>=min testTrades`price) and result<=max testTrades`price
 }

/ Pattern 3: Independent formula (direct select, not the function being tested)
test_independent:{[]
  result:count getTradesBySym[`AAPL];
  expected:count select from trade where sym=`AAPL;
  result~expected
 }

/ Pattern 4: Subset validation
test_subset:{[]
  result:getTradesBySym[`AAPL];
  / Property: result should only contain AAPL
  all result[`sym]=`AAPL
 }
```

### Detection Questions

When writing tests, ask:
- "Am I using the exact same formula as the implementation?"
- "Would this test catch a bug in the formula?"
- "Can I verify this result independently?"

### Edge Cases Checklist

Always test:
- Empty input (0-length)
- Single element
- All same values
- Boundary conditions
- Null values (where applicable)
- Type edge cases (0N, 0W, -0W for numerics)

```q
/ Good edge case tests
genTrades[0]             / empty table
genTrades[1]             / single row
til[10] within (0;9)     / boundaries inclusive
```

**Rule**: Tests must validate properties independently, not reproduce implementation.

---

## List vs Atom Context

### The Error

Confusing when operations return lists vs atoms.

```q
q)x:1 2 3
q)x[0]
1    / atom

q)x[0 1]
1 2  / list

q)x[enlist 0]
,1   / singleton list, not atom!

q)first x
1    / atom

q)1#x
,1   / singleton list
```

### Indexing and Extraction Reference

| Operation | Example | Result Type |
|-----------|---------|-------------|
| `x[i]` where `i` is atom | `x[0]` | Atom |
| `x[i]` where `i` is list | `x[0 1]` | List |
| `x[enlist i]` | `x[enlist 0]` | Singleton list |
| `first x` | `first x` | Atom (first element) |
| `last x` | `last x` | Atom (last element) |
| `1#x` | `1#x` | List (take 1) |
| `1_x` | `1_x` | List (drop 1) |

### When It Matters

```q
/ Atom context - direct comparison
if[x[0]=5; ...]     / OK

/ List context - need 'all' or 'any'
if[x[0 1]=5; ...]   / ERROR - comparing list to atom
if[any x[0 1]=5; ...] / OK

/ Singleton list vs atom
y:x[enlist 0]       / y is ,1 (list)
if[y=1; ...]        / comparing list to atom - unexpected result
if[(first y)=1; ...] / OK - comparing atom to atom
```

---

## Block Comment Syntax

### The Error

A line containing **only** `/` (with no text after it) enters multi-line block comment mode. Every line after it is silently ignored until a line containing only `\` is encountered.

```q
/ WRONG - bare / on its own line triggers block comment mode
calcVWAP:{[t] ...}

/                   ← THIS LINE STARTS A BLOCK COMMENT
                    ← everything below here is silently ignored!

calcTWAP:{[t] ...}  ← this function is NEVER defined
```

This is **completely silent** — no error, no warning. Functions are missing at runtime.

Contrast with safe comment forms:

```q
/ This is a normal comment - safe
/ .                 ← safe separator line
/ ---               ← safe separator line
```

### Why It Happens

In Python (`#`), JavaScript (`//`), and C++ (`//`), a blank comment line is harmless. LLMs generate bare `/` lines as visual separators between documentation blocks, section dividers, or blank comment lines. In q, this is a critical syntax error that silently swallows subsequent code.

### The Correct Pattern

**Always include at least one character after `/`:**

```q
/ .      ← safe
/ ---    ← safe
/        ← DANGEROUS — block comment mode!
```

### Real-World Incidents

**Iteration 5** (`test_ohlc.q`): Bare `/` separator lines silently commented out all subsequent test functions. No error was reported.

**Iteration 6** (`twap.q`): Six bare `/` lines prevented all function definitions from loading. The functions were completely absent at runtime.

### Detection Rule

Scan for bare `/` lines before running any `.q` file:

```bash
grep -n "^/$" file.q
```

To scan all `.q` files in a directory:

```bash
grep -rn "^/$" *.q
```

Any match is a bug. Fix by adding text after the `/`.

### Quick Fix

Find all bare `/` lines and add a dot:

```bash
sed -i 's|^/$|/ .|g' file.q
```

---

## .z.ph Input Format

### The Error

Assuming `.z.ph` receives a raw HTTP request string like `"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"`.

```q
/ WRONG - .z.ph does NOT receive the raw HTTP text
.z.ph:{[x]
  lines:"\r\n" vs x;            / assumes x is "GET / HTTP/1.1\r\n..."
  reqLine:first lines;
  / ... parse method, path, version from reqLine
 }
```

### What Actually Happens

KDB+ parses the HTTP request itself and passes a 2-element list to `.z.ph`:

- `x[0]` — the URL path+query as a string (e.g., `"page?a=1"`)
- `x[1]` — the HTTP headers as a dictionary (type 99h, symbol keys, string values)

For the root path `/`, KDB+ passes an **empty string** `""`, not `"/"`.

```q
/ CORRECT - handle the actual input format
.z.ph:{[x]
  rawPath:$[10h=type x; x; first x];   / extract path+query
  hdrs:$[0h=type x; x 1; (`$())!()];   / extract headers dict
  rawPath:$[0=count rawPath; enlist"/"; rawPath]; / empty = root
  / ... parse path and query string from rawPath
 }
```

**Real Incident (zph Iteration 3)**: Handler received `(""; headerDict)` from the browser. `parseReq` expected a full HTTP request string, failed silently. The 404 handler caught it with an empty path.

**Rule**:
- `.z.ph` receives `(pathQuery; headerDict)`, not raw HTTP text
- Root path is `""` (empty string), not `"/"`
- Always normalise: check `type x` to handle both string-only and list formats

---

## @[f;x;h] Iterates Over Lists

### The Error

Using `@[f;x;handler]` (protected evaluation) where `x` is a general list causes `f` to be applied to each element of `x` individually, not to `x` as a whole.

```q
/ WRONG - @[] iterates over the 2-element list
req:("path"; headerDict);
@[processRequest; req; errorHandler]
/ calls processRequest["path"], then processRequest[headerDict]
/ NOT processRequest[("path"; headerDict)]

/ CORRECT - normalise first, then use @[] with a single value
parsed:buildReq req;
@[dispatch; parsed; errorHandler]

/ ALTERNATIVE - use .[] (dot-apply) with enlist to force single argument
.[processRequest; enlist req; errorHandler]
```

**Real Incident (zph Iteration 3)**: `.z.ph` used `@[{[x] buildReq x; ...}; x; errorHandler]` where `x` was `(""; headerDict)`. This applied the inner function to `""` and then to `headerDict` separately, causing a type error.

**Rule**:
- `@[f;x;h]` iterates when `x` is a general list (type 0h)
- To pass a list as a single argument: normalise before `@`, or use `.[f;enlist x;h]`
- Prefer restructuring so the trapped function receives a dict or atom, not a raw list

---

## Single Character String is a Char Atom

### The Error

A single character in double quotes is a char atom (type -10h), not a 1-element string (type 10h). Functions like `vs` expect strings and throw type errors on char atoms.

```q
/ WRONG - "/" is a char atom, not a string
rawPath:$[isEmpty; "/"; rawPath];
"?" vs rawPath                    / type error! vs expects string (10h)

/ CORRECT - use enlist to create a 1-element string
rawPath:$[isEmpty; enlist"/"; rawPath];
"?" vs rawPath                    / works — rawPath is type 10h
```

### Type Reference

| Expression | Type | Description |
|------------|------|-------------|
| `"a"` | `-10h` | Char atom |
| `"ab"` | `10h` | Char list (string) |
| `enlist "a"` | `10h` | 1-element string |
| `first "abc"` | `-10h` | Char atom |

### When It Matters

Any function that expects a string (type 10h) will fail on a char atom:
- `vs` — `"?" vs charAtom` → type error
- `ss` — `charAtom ss "pattern"` → type error
- `like` — `charAtom like "pattern"` → type error

**Real Incident (zph Iteration 3)**: Empty root path was replaced with `"/"` (char atom). `"?" vs "/"` threw a type error because `vs` requires a string on the right side.

**Rule**: When assigning a single-character string, use `enlist"/"` not `"/"`. Multi-character strings like `"/api"` are already type 10h and don't need this.

---

## Boolean and Conditional Logic

### All/Any with Lists

```q
/ Empty list edge cases
all 0#0b                 / 1b (all of nothing is true)
any 0#0b                 / 0b (any of nothing is false)

/ Single element
all enlist 1b            / 1b
any enlist 0b            / 0b
```

**Rule**: `all` and `any` handle empty lists correctly. Use them, don't write manual reductions.

---

## Precision and Floating Point

### Two-Decimal Check

```q
/ WRONG: Testing formula output equals formula input (circular)
prices:100.25 150.50 200.75
hasTwoDecimals:{all prices=0.01*floor 0.5+100*prices}   / tautology!

/ CORRECT: Independent mathematical test
hasTwoDecimals:{all 1e-9>abs(prices*100)mod 1}         / checks if 100*price is integer
```

**Rule**: Never test code by using the same formula it was created with. Use independent mathematical properties.

### Rounding Idioms

```q
/ Round to 2 decimals
0.01*floor 0.5+100*x

/ Round to nearest integer
floor 0.5+x

/ Round to nearest multiple of y
y*floor 0.5+x%y
```

See `fin.md` (finance section) for rounding patterns.

---

## Iteration and Vector Operations

### No Explicit Loops

```q
/ WRONG: Thinking in loops
result:()
i:0
while[i<count x; result,:f x i; i+:1]

/ CORRECT: Vector operation
result:f each x           / if f is unary
result:f'[x;y]            / if f is binary with paired arguments
```

**Rule**: q is vector-oriented. Almost never write explicit loops. Use `each`, `each-both` (`'`), or native vector operations.

### Each vs Each-Both

```q
/ each: apply to each item
f each x                 / apply f to x[0], x[1], x[2], ...

/ each-both: apply pairwise
x f' y                   / apply f to (x[0];y[0]), (x[1];y[1]), ...
```

---

## Table and Column Operations

### Indexing Multiple Columns

```q
t:([] time:.z.p+til 3; sym:`A`B`C; price:100 101 102)

/ WRONG: Direct indexing returns table (flipped dict)
t `time`sym              / returns 2-column table
not null t `time`sym     / type error! Can't apply null to table

/ CORRECT: Handle table result
cols:`time`sym
flip t cols              / dict of columns
raze flip t cols         / vector of all values
all raze not null flip t cols  / check all non-null
```

**Rule**: `table column_list` returns a sub-table. Use `flip` to get dict, or index columns individually.

### Column Access Patterns

```q
/ Single column
t`price                  / vector of prices

/ Multiple columns (returns table)
t `time`sym             / 2-column table

/ Convert multi-column result to check all values
hasNoNulls:{[tbl;colList] all raze not null flip tbl colList}
```

---

## String and Symbol Confusion

```q
/ Strings are char lists
"hello"                  / char vector
type "hello"             / 10h (char vector)

/ Symbols are atoms
`hello                   / symbol atom
type `hello              / -11h (symbol atom)

/ WRONG: Comparing strings and symbols
"AAPL" = `AAPL          / length error or type mismatch
"AAPL" ~ `AAPL          / 0b (false)

/ Conversion
`$"hello"                / symbol from string
string `hello            / string from symbol
```

**Rule**: Strings (char vectors) and symbols are different types. Convert explicitly.

---

## Within and Range Checks

```q
/ within: inclusive on both ends [lo, hi]
50 within (40;60)        / 1b
40 within (40;60)        / 1b
60 within (40;60)        / 1b

/ Vector version
prices:45 50 55 65
prices within (40;60)    / 1b 1b 1b 0b
all prices within (40;60) / 0b

/ Multiple ranges
x within (lo;hi)         / single range
all x within (lo;hi)     / all values in range
```

**Rule**: `within` takes a 2-element tuple `(lo;hi)` and is inclusive on both boundaries.

---

## Reserved Keywords as Variable Names

### The Error

q has reserved keywords that cannot be used as variable names. Using them silently produces wrong results or cryptic errors.

```q
/ WRONG - lower and upper are q keywords
lower:q1-1.5*iqr
upper:q3+1.5*iqr
outliers:sum not v within(lower;upper)   / unexpected behavior

/ CORRECT - use descriptive non-keyword names
lowerBound:q1-1.5*iqr
upperBound:q3+1.5*iqr
outliers:sum not v within(lowerBound;upperBound)
```

**Real Incident (Iteration 4 post-fix)**: Used `lower` and `upper` as variable names in `looksNormal` function in test patterns. These are q keywords — `lower` lowercases strings, `upper` uppercases strings.

**Common q keywords to avoid as variable names**:
- `lower`, `upper` — case conversion
- `like` — pattern matching
- `in` — membership
- `within` — range check
- `except` — set difference
- `inter` — set intersection
- `union` — set union
- `distinct` — unique values
- `count` — length
- `first`, `last` — element access
- `key`, `value` — dictionary access
- `type` — type check
- `string` — conversion
- `null` — null check
- `sum`, `avg`, `min`, `max` — aggregations

**Rule**: Never use q keywords as variable names. Use descriptive alternatives: `lowerBound`, `upperLimit`, `maxPrice`, `symList`, etc.

---

## Summary of Most Common Errors

1. **Function calls**: Using `f(x)` instead of `f x` or `f[x]` — THIS IS #1
2. **Type confusion**: Comparing chars with symbols (meta table types)
3. **List vs scalar**: Not extracting scalars from single-element lists
4. **Table clearing**: Using `::` assignment instead of `delete from`
5. **Prior edge case**: Using `prior` for sorted checks (first element fails)
6. **Circular tests**: Testing with same formula used in implementation
7. **Multi-column indexing**: Not handling table results from `tbl col_list`
8. **Variable shadowing**: Parameter names matching column names in selects
9. **Timestamp division**: Division produces float, not timespan
10. **Keywords as variables**: Using `lower`, `upper`, etc. as variable names
11. **Bare `/` lines**: A lone `/` line silently activates block comment mode
12. **`.z.ph` input format**: Receives `(pathQuery; headerDict)`, not raw HTTP text; root is `""` not `"/"`
13. **`@[f;x;h]` iterates over lists**: Use `.[f;enlist x;h]` or normalise to dict/atom first
14. **Single char is atom**: `"/"` is type -10h (char atom); use `enlist"/"` for a string (type 10h)

---

## Quick Reference Card

```q
/ FUNCTION APPLICATION
f x              ← CORRECT
f[x]             ← CORRECT
f(x)             ← WRONG! Syntax error

/ TYPE CHECKING
"p"=first exec t from meta tbl   ← CORRECT (char comparison)
`p=meta[tbl]`t                    ← WRONG (char vs symbol)

/ SCALAR EXTRACTION
first t`col      ← get scalar from single-row column
all t`col in L   ← reduce list of bools to scalar bool

/ ASCENDING CHECK
(asc x)~x        ← CORRECT
x~asc x          ← CORRECT
all(<=)prior x   ← WRONG (first element always false)

/ WITHIN
x within (lo;hi)     ← value in [lo,hi]
all x within (lo;hi) ← all values in range

/ NULL CHECK
null x           ← CORRECT
x=0N             ← WRONG (0N=0N is false!)

/ VARIABLE SHADOWING
select from t where sym in sym   ← WRONG (param shadows column)
select from t where sym in syms  ← CORRECT (different names)

/ KEYWORDS AS VARIABLES
lower:x-1                        ← WRONG (lower is a keyword)
lowerBound:x-1                   ← CORRECT (descriptive name)

/ TABLE CLEARING
delete from `trade               ← CORRECT (preserves type 98h)
trade::0#trade                   ← WRONG (becomes type 0h)

/ TIMESTAMP ARITHMETIC
(maxT-minT)%2                    ← WRONG (returns float)
0D00:00:00.000000001*(`long$(maxT-minT))div 2  ← CORRECT (timespan)

/ COMMENT LINES
/ some text       ← CORRECT (single-line comment)
/ .               ← CORRECT (safe separator)
/                 ← WRONG! Bare / starts block comment mode — all code below is silently ignored

/ .z.ph INPUT FORMAT
.z.ph:{[x] first x}         ← x[0] is path+query string, x[1] is header dict
/ root path is "" (empty), not "/"

/ PROTECTED EVAL WITH LISTS
@[f;generalList;h]           ← WRONG! Iterates over list elements
.[f;enlist generalList;h]    ← CORRECT — passes list as single arg
@[f;dictOrAtom;h]            ← CORRECT — dict/atom not iterated

/ SINGLE CHAR VS STRING
"/"                          ← char atom (type -10h)
enlist"/"                    ← 1-element string (type 10h)
"?" vs "/"                   ← WRONG! type error — vs needs string
"?" vs enlist"/"             ← CORRECT
```

---

## Before Writing Q Code

1. **Consult the phrasebook** for the relevant section (arith, find, test, etc.)
2. **Check this pitfalls document** if using patterns from other languages
3. **Remember**: No `f(x)`! Use `f x` or `f[x]`
4. **Check variable names** against the keywords list — no `lower`, `upper`, `type`, `count`, etc.
5. **Test edge cases**: empty, single element, boundaries
6. **Validate independently**: don't test with implementation formula

---

## See Also

- [Execution patterns](exec.md) - conditional execution, case structure
- [Test patterns](test.md) - validation and comparison idioms
- [Find patterns](find.md) - searching and membership
- [Cast patterns](cast.md) - type conversions
- [Phrases collection](phrases.md) - common utilities
- [Q Phrasebook](phrases.md) - idiomatic q patterns
- [KX Reference Card](https://code.kx.com/q/ref/) - official q reference
- [Q for Mortals](https://code.kx.com/q4m3/) - comprehensive q tutorial

---

## Contributing

When you encounter a new systematic error:

1. Document the **wrong pattern** with example
2. Explain **why it happens** (which language pattern causes it)
3. Show the **correct pattern** with example
4. Add a **real-world incident** if applicable
5. Provide a **detection rule** or checklist item

This document is a living reference. Update it as you discover new pitfalls.

---

*This document exists because LLMs (like me) pattern-match from mainstream languages and make systematic errors when writing q code. Study these pitfalls to avoid repeating them.*

---

Last Updated: 2026-02-12
