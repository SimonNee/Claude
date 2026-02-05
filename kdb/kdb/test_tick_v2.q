/ Unit Test Harness for Tick Analytics Engine
/ Iteration 1 Tests - Using Reusable Utilities

/ Load utilities and code under test
\l test_utils.q
\l tick.q

/ Initialize test framework
tests:initTests[]

-1 "\n=== Running Iteration 1 Tests (Using Utilities) ===\n";

/ ========================================
/ CATEGORY 1: GENERAL TABLE TESTS
/ ========================================

addTest[`table_exists;
  tableExists[`trade];
  "Trade table should exist in workspace"]

addTest[`has_columns;
  hasColumns[trade;`time`sym`price`size];
  "Should have columns: time,sym,price,size"]

addTest[`column_types;
  checkTypes[trade;`time`sym`price`size!"psfj"];
  "Column types should match schema"]

addTest[`non_empty;
  isNonEmpty[trade];
  "Table should contain at least one row"]

addTest[`no_nulls_in_keys;
  hasNoNulls[trade;`time`sym];
  "Key columns (time,sym) should not contain nulls"]

/ ========================================
/ CATEGORY 2: FINANCIAL TRADE TABLE TESTS
/ ========================================

addTest[`prices_valid;
  validPrices[trade;`price];
  "All prices must be positive and not infinite"]

addTest[`sizes_positive;
  allPositive[trade;`size];
  "All trade sizes must be greater than 0"]

addTest[`symbols_valid;
  hasNoNulls[trade;enlist`sym];
  "All symbols must be non-null"]

addTest[`time_ascending;
  isAscending[trade;`time];
  "Timestamps should be in ascending order"]

addTest[`no_future_times;
  noFutureTimes[trade;`time];
  "No timestamps should be in the future"]

/ ========================================
/ ITERATION 1 SPECIFIC TESTS
/ ========================================

addTest[`exact_row_count;
  1=count trade;
  "Should have exactly 1 row initially"]

addTest[`symbol_is_aapl;
  `AAPL~first trade`sym;
  "First trade symbol should be AAPL"]

addTest[`price_is_150_25;
  150.25~first trade`price;
  "First trade price should be 150.25"]

addTest[`size_is_100;
  100~first trade`size;
  "First trade size should be 100"]

/ ========================================
/ REPORT AND EXIT
/ ========================================

exit reportTests[tests]
