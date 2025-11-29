/ Unit Test Harness for Tick Analytics Engine
/ Iteration 1 Tests

/ Load the code under test
\l tick.q

/ Test framework
passed:0
failed:0

-1 "\n=== Running Iteration 1 Tests ===\n";

/ Test 1: Trade table exists
if[`trade in tables[]; passed+:1; -1 "PASS: Trade table exists"];
if[not `trade in tables[]; failed+:1; -1 "FAIL: Trade table exists"];

/ Test 2: Trade table has correct columns
if[`time in cols trade; passed+:1; -1 "PASS: Has time column"];
if[not `time in cols trade; failed+:1; -1 "FAIL: Has time column"];

if[`sym in cols trade; passed+:1; -1 "PASS: Has sym column"];
if[not `sym in cols trade; failed+:1; -1 "FAIL: Has sym column"];

if[`price in cols trade; passed+:1; -1 "PASS: Has price column"];
if[not `price in cols trade; failed+:1; -1 "FAIL: Has price column"];

if[`size in cols trade; passed+:1; -1 "PASS: Has size column"];
if[not `size in cols trade; failed+:1; -1 "FAIL: Has size column"];

/ Test 3: Schema types are correct
schema:meta trade;
if[`p = exec t from schema where c=`time; passed+:1; -1 "PASS: time is timestamp type"];
if[not `p = exec t from schema where c=`time; failed+:1; -1 "FAIL: time is timestamp type"];

if[`s = exec t from schema where c=`sym; passed+:1; -1 "PASS: sym is symbol type"];
if[not `s = exec t from schema where c=`sym; failed+:1; -1 "FAIL: sym is symbol type"];

if[`f = exec t from schema where c=`price; passed+:1; -1 "PASS: price is float type"];
if[not `f = exec t from schema where c=`price; failed+:1; -1 "FAIL: price is float type"];

if[`j = exec t from schema where c=`size; passed+:1; -1 "PASS: size is long type"];
if[not `j = exec t from schema where c=`size; failed+:1; -1 "FAIL: size is long type"];

/ Test 4: Initial data exists
if[0 < count trade; passed+:1; -1 "PASS: Trade table has data"];
if[not 0 < count trade; failed+:1; -1 "FAIL: Trade table has data"];

if[1 = count trade; passed+:1; -1 "PASS: Trade table has exactly 1 row"];
if[not 1 = count trade; failed+:1; -1 "FAIL: Trade table has exactly 1 row"];

/ Test 5: Initial trade data is correct
if[`AAPL = first trade`sym; passed+:1; -1 "PASS: First trade is AAPL"];
if[not `AAPL = first trade`sym; failed+:1; -1 "FAIL: First trade is AAPL"];

if[150.25 = first trade`price; passed+:1; -1 "PASS: First trade price is 150.25"];
if[not 150.25 = first trade`price; failed+:1; -1 "FAIL: First trade price is 150.25"];

if[100 = first trade`size; passed+:1; -1 "PASS: First trade size is 100"];
if[not 100 = first trade`size; failed+:1; -1 "FAIL: First trade size is 100"];

/ Report results
-1 "\n=== Test Results ===";
-1 "Passed: ", string passed;
-1 "Failed: ", string failed;
-1 "Total:  ", string passed + failed;

/ Exit with appropriate code
exit $[failed > 0; 1; 0]
