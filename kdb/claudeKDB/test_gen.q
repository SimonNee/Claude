/ Test Suite for genTrades function
/ Tests trade data generation for correctness and schema validation
/ Usage: q test_gen.q

/ Load dependencies
\l test_utils.q
\l gen.q

/ Initialize test results
tests:initTests[]

/ Define constants
VALID_SYMS:`AAPL`MSFT`GOOG`AMZN`TSLA
MIN_PRICE:50.0
MAX_PRICE:500.0
MIN_SIZE:100
MAX_SIZE:10000
ROUND_LOT:100

/ ========================================
/ HELPER FUNCTIONS
/ ========================================

/ Check if all prices have at most 2 decimal places
/ A price has <=2 decimals if 100*price is an integer (within float tolerance)
hasTwoDecimals:{[prices]
  all 1e-9>abs(prices*100)mod 1
 }

/ Check if all values are multiples of x
isMultipleOf:{[vals;x] all 0=vals mod x}

/ Check if all values are within bounds (inclusive)
inRange:{[vals;lo;hi] all vals within (lo;hi)}

/ ========================================
/ TEST CASES
/ ========================================

/ Test 1: Returns a table with correct schema
-1 "Running: test_returns_table_with_schema";
t10:genTrades[10];
addTest[`returns_table; 98h=type t10; "genTrades returns a table"];
addTest[`has_correct_columns; `time`sym`price`size~cols t10; "Table has columns: time, sym, price, size"];

/ Test 2: Returns exactly n rows
-1 "Running: test_returns_n_rows";
addTest[`returns_10_rows; 10=count genTrades[10]; "genTrades[10] returns 10 rows"];
addTest[`returns_100_rows; 100=count genTrades[100]; "genTrades[100] returns 100 rows"];
addTest[`returns_5_rows; 5=count genTrades[5]; "genTrades[5] returns 5 rows"];

/ Test 3: All timestamps are sequential (ascending order)
-1 "Running: test_timestamps_ascending";
t50:genTrades[50];
addTest[`time_ascending; isAscending[t50;`time]; "Timestamps are in ascending order"];
addTest[`time_matches_asc; (asc t50`time)~t50`time; "Timestamps match sorted order"];

/ Test 4: All timestamps are not in the future
/ Allow small tolerance for clock skew (1 second)
-1 "Running: test_timestamps_not_future";
t20:genTrades[20];
tolerance:.z.p+0D00:00:01;  / 1 second tolerance
addTest[`no_future_times; all t20[`time]<=tolerance; "No timestamps in future"];

/ Test 5: All symbols are from the valid list
-1 "Running: test_valid_symbols";
t100:genTrades[100];
addTest[`symbols_valid; all t100[`sym]in VALID_SYMS; "All symbols in valid list"];
addTest[`symbols_subset; all(distinct t100`sym)in VALID_SYMS; "Distinct symbols are subset of valid list"];

/ Test 6: All prices are in range 50.0 to 500.0
-1 "Running: test_price_range";
addTest[`prices_in_range; inRange[t100`price;MIN_PRICE;MAX_PRICE]; "Prices in range [50.0, 500.0]"];
addTest[`prices_gte_min; all t100[`price]>=MIN_PRICE; "All prices >= 50.0"];
addTest[`prices_lte_max; all t100[`price]<=MAX_PRICE; "All prices <= 500.0"];

/ Test 7: All prices have at most 2 decimal places
-1 "Running: test_price_decimals";
addTest[`prices_two_decimals; hasTwoDecimals[t100`price]; "Prices have at most 2 decimal places"];

/ Test 8: All sizes are positive
-1 "Running: test_sizes_positive";
addTest[`sizes_positive; all 0<t100`size; "All sizes are positive"];

/ Test 9: All sizes are round lots (multiples of 100)
-1 "Running: test_sizes_round_lots";
addTest[`sizes_round_lots; isMultipleOf[t100`size;ROUND_LOT]; "All sizes are multiples of 100"];

/ Test 10: All sizes are in range 100 to 10000
-1 "Running: test_size_range";
addTest[`sizes_in_range; inRange[t100`size;MIN_SIZE;MAX_SIZE]; "Sizes in range [100, 10000]"];
addTest[`sizes_gte_min; all t100[`size]>=MIN_SIZE; "All sizes >= 100"];
addTest[`sizes_lte_max; all t100[`size]<=MAX_SIZE; "All sizes <= 10000"];

/ Test 11: Edge case - genTrades[0] returns empty table with correct schema
-1 "Running: test_edge_case_zero";
t0:genTrades[0];
addTest[`zero_returns_table; 98h=type t0; "genTrades[0] returns a table"];
addTest[`zero_has_schema; `time`sym`price`size~cols t0; "Empty table has correct schema"];
addTest[`zero_is_empty; 0=count t0; "genTrades[0] returns 0 rows"];

/ Test 12: Edge case - genTrades[1] returns single row
-1 "Running: test_edge_case_one";
t1:genTrades[1];
addTest[`one_returns_table; 98h=type t1; "genTrades[1] returns a table"];
addTest[`one_returns_one_row; 1=count t1; "genTrades[1] returns 1 row"];
addTest[`one_valid_symbol; all t1[`sym]in VALID_SYMS; "Single row has valid symbol"];
addTest[`one_valid_price; inRange[t1`price;MIN_PRICE;MAX_PRICE]; "Single row has valid price"];
addTest[`one_valid_size; inRange[t1`size;MIN_SIZE;MAX_SIZE]; "Single row has valid size"];

/ Test 13: Timestamp spacing validation
/ Timestamps should be 0-99ns apart based on implementation
-1 "Running: test_timestamp_spacing";
t30:genTrades[30];
if[1<count t30;
  diffs:1_ deltas t30`time;  / First differences (spacing between consecutive timestamps)
  addTest[`time_spacing_nonneg; all diffs>=0; "All timestamp intervals are non-negative"];
  addTest[`time_spacing_reasonable; all diffs<100; "Timestamp spacing < 100ns"];
 ];

/ ========================================
/ ADDITIONAL VALIDATION TESTS
/ ========================================

/ Test 14: No null values in any column
-1 "Running: test_no_nulls";
addTest[`no_null_time; all not null t100`time; "No null timestamps"];
addTest[`no_null_sym; all not null t100`sym; "No null symbols"];
addTest[`no_null_price; all not null t100`price; "No null prices"];
addTest[`no_null_size; all not null t100`size; "No null sizes"];

/ Test 15: Column types are correct
-1 "Running: test_column_types";
m:meta t100;
addTest[`time_is_timestamp; "p"~first exec t from m where c=`time; "time column is timestamp (p)"];
addTest[`sym_is_symbol; "s"~first exec t from m where c=`sym; "sym column is symbol (s)"];
addTest[`price_is_float; "f"~first exec t from m where c=`price; "price column is float (f)"];
addTest[`size_is_long; "j"~first exec t from m where c=`size; "size column is long (j)"];

/ Test 16: Randomness check - different calls should produce different results
/ (with high probability for reasonable sample sizes)
-1 "Running: test_randomness";
t_a:genTrades[50];
t_b:genTrades[50];
/ Tables should be different (not identical)
addTest[`different_prices; not(t_a`price)~t_b`price; "Different calls produce different prices"];
addTest[`different_symbols; not(t_a`sym)~t_b`sym; "Different calls produce different symbols"];

/ Test 17: Large dataset test
-1 "Running: test_large_dataset";
t1000:genTrades[1000];
addTest[`large_count; 1000=count t1000; "genTrades[1000] returns 1000 rows"];
addTest[`large_ascending; isAscending[t1000;`time]; "Large dataset has ascending timestamps"];
addTest[`large_valid_prices; inRange[t1000`price;MIN_PRICE;MAX_PRICE]; "Large dataset has valid prices"];
addTest[`large_valid_sizes; inRange[t1000`size;MIN_SIZE;MAX_SIZE]; "Large dataset has valid sizes"];

/ ========================================
/ STRESS TEST: 10 MILLION ROWS
/ ========================================

-1 "Running: stress_test_10M";
-1 "Generating 10M trades (this validates kdb performance)...";

/ Time the generation
start:.z.p;
t10M:genTrades[10000000];
elapsed:`long$.z.p-start;
elapsed_ms:elapsed%1000000;

-1 "  Time: ",string[elapsed_ms],"ms";
-1 "  Throughput: ",string[floor 10000000000%elapsed],"M rows/sec";
-1 "  Memory: ",string[floor(-22!t10M)%1000000],"MB";

/ Correctness checks on 10M rows
addTest[`stress_count; 10000000=count t10M; "10M rows generated"];
addTest[`stress_schema; `time`sym`price`size~cols t10M; "Schema correct at 10M scale"];
addTest[`stress_ascending; (asc t10M`time)~t10M`time; "Timestamps ascending at 10M scale"];
addTest[`stress_symbols; all t10M[`sym]in VALID_SYMS; "All symbols valid at 10M scale"];
addTest[`stress_prices; all t10M[`price]within(MIN_PRICE;MAX_PRICE); "Prices in range at 10M scale"];
addTest[`stress_roundlots; all 0=t10M[`size]mod ROUND_LOT; "Round lots at 10M scale"];
addTest[`stress_sizes; all t10M[`size]within(MIN_SIZE;MAX_SIZE); "Sizes in range at 10M scale"];
addTest[`stress_no_nulls; all not any null flip t10M; "No nulls at 10M scale"];

/ Performance threshold (should complete in under 5 seconds)
addTest[`stress_performance; elapsed_ms<5000; "10M generation under 5 seconds"];

/ Clean up memory
delete t10M from `.;

/ ========================================
/ REPORT RESULTS
/ ========================================

-1 "\n=== genTrades Test Suite Complete ===\n";
exit reportTests[tests]
