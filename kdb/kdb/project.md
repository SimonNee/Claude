# Time-Series Tick Data Analytics Engine

## Project Goal

Build a self-contained KDB+/q system for time-series tick data analytics to assess Claude's q language capabilities.

## Iteration Roadmap

### Iteration 1: Foundation
- **Objective**: Define tick table schema, create single hardcoded trade, verify structure
- **Deliverable**: A `.q` file with a working `trade` table, can query it
- **Validates**: Basic q syntax, table creation, schema understanding

### Iteration 2: Data Generator
- **Objective**: Function to generate N synthetic trades (random prices/sizes/symbols)
- **Deliverable**: `genTrades[n]` function, can populate table with realistic data
- **Validates**: Q functions, random data generation, temporal sequences

### Iteration 3: Time/Symbol Queries
- **Objective**: Helper functions to slice data by time range and symbol
- **Deliverable**: `getTrades[sym; startTime; endTime]` style functions
- **Validates**: Functional queries, temporal operations, q select syntax

### Iteration 4: VWAP
- **Objective**: Calculate volume-weighted average price
- **Deliverable**: `vwap[trades]` function, demonstrate on sample data
- **Validates**: Aggregation operations, weighted calculations

### Iteration 5: OHLC Bars
- **Objective**: Bar aggregation (1min, 5min configurable)
- **Deliverable**: `ohlc[trades; barSize]` function producing candlestick data
- **Validates**: Temporal bucketing, multi-column aggregations, xbar

### Iteration 6: TWAP
- **Objective**: Time-weighted average price calculation
- **Deliverable**: `twap[trades; interval]` function
- **Validates**: More complex temporal logic

## Design Principles

- Each iteration produces runnable, testable code
- Rapid prototyping with clear incremental progress
- Self-contained (generate synthetic data)
- Demonstrate q's strengths in columnar/temporal operations

## Assessment Results (2025-11-29)

### Outcome: Claude Cannot Autonomously Produce KDB+/q Code

**What was attempted:**
- Iteration 1: Created basic tick table schema (tick.q) - this succeeded
- Created unit test harness (test_tick.q) - this failed repeatedly

**Errors encountered:**

1. **Function definition syntax errors**:
   - Repeated "nyi" (not yet implemented) errors when defining functions with syntax `{[desc;condition] ...}`
   - Could not create even a simple helper function like `assert`
   - Suggests fundamental misunderstanding of q lambda/function syntax

2. **Meta table access failures**:
   - Could not extract type information from `meta trade` result
   - Tried multiple approaches: `schema[`time;`t]`, `schema[`time]`t`, `exec t from schema where c=`time`
   - All failed with type errors
   - Indicates lack of understanding of how q dictionaries and keyed tables work

3. **Pattern-matching vs. comprehension**:
   - Errors show Claude is guessing based on limited patterns from other languages
   - Not demonstrating actual understanding of q idioms or syntax

**Root cause analysis:**

- **Limited training corpus**: KDB+/q has extremely limited public code compared to mainstream languages
- **No online learning**: Claude cannot learn from guidance within a session
- **Domain-specific language limitation**: This problem will persist for any niche/specialized language with small corpus

**Conclusion:**

Claude is **not suitable for autonomous KDB+/q code production**. Attempting to use Claude for q development would result in:
- Buggy, non-idiomatic code
- More time spent debugging than writing code manually
- Fundamental syntax errors that show lack of language comprehension

**Better use cases for Claude with q/KDB+:**
- Documentation research
- Explaining known concepts
- General architecture discussions
- Non-q portions of hybrid systems

**Future retry**: Consider re-assessment in a few months if Claude's training data is updated or if capabilities change significantly.
