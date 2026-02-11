# Q Language Pitfalls for Programmers from Other Languages

## Critical: Function Application Syntax

### THE FUNDAMENTAL RULE

**In q, parentheses are for GROUPING, not function calls.**

```q
/ WRONG - This is a syntax error
all(x)          / Tries to apply noun 'all' to expression (x)
sum(vals)       / Tries to apply noun 'sum' to expression (vals)

/ CORRECT - Function application in q
all x           / Function applied with space
all[x]          / Function applied with brackets
sum vals        / Function with space
sum[vals]       / Function with brackets
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
sum[x;y]                 / wrong: sum takes one argument
within[vals; (lo;hi)]    / correct: within with bounds tuple
@[table;idx;:;newval]    / amend: 4 arguments require brackets

/ Composition
all not null x           / right-to-left: null x, then not, then all
all[not null x]          / explicit grouping, same result
```

### Pattern Recognition Trap

You learned `f(x)` through thousands of repetitions. q uses different syntax. When you write test code or validation, your muscle memory defaults to `f(x)`. **Actively fight this habit.**

### Debug Checklist

If you get a type or rank error with a function call:
1. Check if you wrote `f(x)` instead of `f x` or `f[x]`
2. Look for any parentheses immediately after a function name
3. Remember: parentheses are ONLY for grouping expressions, never for calling functions

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

### List vs Scalar Confusion

```q
/ Single-row table column access returns a LIST, not scalar
t:([] sym:`AAPL; price:100.0)
t`sym                    / returns ,`AAPL  (1-item list)
t`sym ~ `AAPL           / FALSE! List doesn't match scalar

/ CORRECT: Extract scalar
first t`sym              / `AAPL (scalar symbol)
first t`sym ~ `AAPL     / TRUE

/ Multiple items with 'in'
t`sym in `AAPL`MSFT     / returns list of booleans: 1b
all t`sym in `AAPL`MSFT / returns scalar: 1b
```

**Rule**: Table column indexing always returns lists, even for single rows. Use `first`, `last`, or `all`/`any` to convert to scalars.

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

### Prior and Edge Cases

```q
/ Prior compares with null for first element
x:1 2 3 4 5
(<=)prior x              / 0b 1b 1b 1b 1b (first is 1<=0N → 0b!)

/ WRONG: Ascending test using prior
all(<=)prior x           / FALSE for any list! First element always fails.

/ CORRECT: Ascending test
(asc x)~x                / TRUE if sorted
x~asc x                  / same thing
```

**Rule**: `prior` operates on pairs `(x[i-1];x[i])`. The first element pairs with null, often giving unexpected results. For sorted checks, compare with `asc x`.

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

## Testing Best Practices

### Independent Validation

**WRONG**: Use same formula as implementation
```q
/ Generator: prices:0.01*floor 0.5+100*base+n?range
/ Test: all prices=0.01*floor 0.5+100*prices   ← circular!
```

**CORRECT**: Use independent mathematical property
```q
/ Test: all 1e-9>abs(prices*100)mod 1  ← checks integer property
```

**Rule**: Tests must validate properties independently, not reproduce implementation.

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

### The Table Type Bug

```q
/ WRONG - breaks table type
trade::0#trade  / trade becomes type 0h (mixed list), not 98h (table)

/ CORRECT - use delete to clear table
delete from `trade  / preserves table type (98h)
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

**Type Chart**:

| Operation | Result Type |
|-----------|-------------|
| `timestamp - timestamp` | timespan |
| `timespan % int` | **float** (not timespan!) |
| `timespan div int` | long |
| `timestamp + timespan` | timestamp |

**Solution Pattern**:
```q
/ Convert to nanoseconds (long), divide, convert back to timespan
halfDiff:0D00:00:00.000000001*(`long$(maxTime-minTime))div 2
midTime:minTime+halfDiff
```

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

1. **Function calls**: Using `f(x)` instead of `f x` or `f[x]` ← THIS IS #1
2. **Type confusion**: Comparing chars with symbols (meta table types)
3. **List vs scalar**: Not extracting scalars from single-element lists
4. **Prior edge case**: Using `prior` for sorted checks (first element fails)
5. **Circular tests**: Testing with same formula used in implementation
6. **Multi-column indexing**: Not handling table results from `tbl col_list`
7. **Variable shadowing**: Parameter names matching column names in selects
8. **Table clearing**: Using `::` assignment instead of `delete from`
9. **Timestamp division**: Division produces float, not timespan
10. **Keywords as variables**: Using `lower`, `upper`, etc. as variable names

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

---

*This document exists because LLMs (like me) pattern-match from mainstream languages and make systematic errors when writing q code. Study these pitfalls to avoid repeating them.*
