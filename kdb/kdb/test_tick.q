/ Unit Test Harness for Tick Analytics Engine
/ Iteration 1 Tests - Idiomatic Q Version

/ Load the code under test
\l tick.q

/ Test framework using a table to track results
tests:([] name:`symbol$(); result:`boolean$(); msg:())

/ Helper: Add test result
addTest:{[nm;res;msg] `tests insert (nm;res;msg)}

-1 "\n=== Running Iteration 1 Tests ===\n";

/ ========================================
/ CATEGORY 1: GENERAL TABLE TESTS
/ Reusable tests for any q table
/ ========================================

/ Test 1: Table exists
addTest[`table_exists; `trade in tables[]; "Trade table should exist in workspace"]

/ Test 2: Has expected columns (exact match)
expectedCols:`time`sym`price`size
addTest[`has_columns; expectedCols~cols trade; "Should have columns: time,sym,price,size"]

/ Test 3: Column types match specification
/ Expected types: p=timestamp, s=symbol, f=float, j=long
expectedTypes:`time`sym`price`size!"psf j"
actualTypes:exec c!t from meta trade
addTest[`column_types; expectedTypes~actualTypes; "Column types should match schema"]

/ Test 4: Table is non-empty
addTest[`non_empty; 0<count trade; "Table should contain at least one row"]

/ Test 5: No nulls in key columns
/ Key columns for trade: time, sym
keyNullCheck:all not null trade`time`sym
addTest[`no_nulls_in_keys; keyNullCheck; "Key columns (time,sym) should not contain nulls"]

/ ========================================
/ CATEGORY 2: FINANCIAL TRADE TABLE TESTS
/ Domain-specific validation for tick data
/ ========================================

/ Test 6: All prices are positive
addTest[`prices_positive; all 0<trade`price; "All prices must be greater than 0"]

/ Test 7: All sizes are positive
addTest[`sizes_positive; all 0<trade`size; "All trade sizes must be greater than 0"]

/ Test 8: All symbols are valid (non-null)
addTest[`symbols_valid; all not null trade`sym; "All symbols must be non-null"]

/ Test 9: Timestamps in ascending order (for sorted table)
/ Using (<=) with prior to check each timestamp >= previous
addTest[`time_ascending; all(<=)prior trade`time; "Timestamps should be in ascending order"]

/ Test 10: No future timestamps
addTest[`no_future_times; all trade[`time]<=.z.p; "No timestamps should be in the future"]

/ Test 11: No invalid types (NaN, infinities for float columns)
priceValid:all trade[`price] within (-0w;0w)
addTest[`price_valid_range; priceValid; "Prices should not be infinite"]

/ ========================================
/ ITERATION 1 SPECIFIC TESTS
/ Tests for the hardcoded initial data
/ ========================================

/ Test 12: Exactly 1 row in initial data
addTest[`exact_row_count; 1=count trade; "Should have exactly 1 row initially"]

/ Test 13: First trade is AAPL
addTest[`symbol_is_aapl; `AAPL~first trade`sym; "First trade symbol should be AAPL"]

/ Test 14: First trade price is 150.25
addTest[`price_is_150_25; 150.25~first trade`price; "First trade price should be 150.25"]

/ Test 15: First trade size is 100
addTest[`size_is_100; 100~first trade`size; "First trade size should be 100"]

/ ========================================
/ REPORT RESULTS
/ ========================================

-1 "\n=== Test Results ===\n";

/ Calculate pass/fail counts using vector operations
passed:sum tests`result
failed:sum not tests`result

/ Show failed tests with details
if[0<failed;
  -1 "FAILED TESTS:";
  failedTests:select from tests where not result;
  -1 .Q.s failedTests;
  -1 ""
 ];

/ Summary
-1 "Passed: ",string passed;
-1 "Failed: ",string failed;
-1 "Total:  ",string count tests;
-1 "Pass rate: ",string[100*passed%count tests],"%\n";

/ Exit with appropriate code
exit $[failed>0;1;0]
