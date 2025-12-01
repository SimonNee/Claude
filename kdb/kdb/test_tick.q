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
/ Helper function to safely check column type with precondition validation
checkColType:{[tbl;colName;expectedType]
  / Protected evaluation: get type from meta
  / Use a lambda that unpacks the args tuple
  / Note: use 'col' not 'c' to avoid collision with meta table's 'c' column
  typeResult: @[{[args] t:args 0; col:args 1; exec t from meta[t] where c=col}; (tbl;colName); {(`error;x)}];

  / Precondition 1: Check if we got an error
  if[`error ~ first typeResult; :(`error; "Error accessing meta: ", last typeResult)];

  / Precondition 2: Check we got exactly one result
  if[1 <> count typeResult; :(`error; "Expected 1 type result, got ", string count typeResult)];

  / Extract actual type
  actualType: first typeResult;

  / Compare with expected
  :$[expectedType ~ actualType; (`pass; actualType); (`fail; actualType)]
 };

/ Test time column type
result: checkColType[`trade; `time; "p"];
if[`pass ~ first result; passed+:1; -1 "PASS: time is timestamp type"];
if[`fail ~ first result; failed+:1; -1 "FAIL: time is timestamp type, expected 'p', got '", string last result, "'"];
if[`error ~ first result; failed+:1; -1 "FAIL: time type check - ", last result];

/ Test sym column type
result: checkColType[`trade; `sym; "s"];
if[`pass ~ first result; passed+:1; -1 "PASS: sym is symbol type"];
if[`fail ~ first result; failed+:1; -1 "FAIL: sym is symbol type, expected 's', got '", string last result, "'"];
if[`error ~ first result; failed+:1; -1 "FAIL: sym type check - ", last result];

/ Test price column type
result: checkColType[`trade; `price; "f"];
if[`pass ~ first result; passed+:1; -1 "PASS: price is float type"];
if[`fail ~ first result; failed+:1; -1 "FAIL: price is float type, expected 'f', got '", string last result, "'"];
if[`error ~ first result; failed+:1; -1 "FAIL: price type check - ", last result];

/ Test size column type
result: checkColType[`trade; `size; "j"];
if[`pass ~ first result; passed+:1; -1 "PASS: size is long type"];
if[`fail ~ first result; failed+:1; -1 "FAIL: size is long type, expected 'j', got '", string last result, "'"];
if[`error ~ first result; failed+:1; -1 "FAIL: size type check - ", last result];

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
