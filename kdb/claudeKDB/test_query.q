/ Test Suite for Query Functions
/ Tests getTradesBySym, getTradesByTime, and getTrades
/ Usage: q test_query.q

/ Load dependencies
\l test_utils.q
\l tick.q
\l query.q

/ Clear the default trade from tick.q and use controlled test data
delete from `trade;

/ Initialize test results
tests:initTests[]

/ ========================================
/ CONTROLLED TEST DATA
/ ========================================

/ Create known test data with predictable timestamps
/ Base time for all tests
baseTime:2024.01.15D09:30:00.000000000;

/ Insert controlled test trades
`trade insert (baseTime+00:00:00.000000000; `AAPL; 150.00; 100);
`trade insert (baseTime+00:01:00.000000000; `MSFT; 380.00; 200);
`trade insert (baseTime+00:02:00.000000000; `GOOG; 140.00; 300);
`trade insert (baseTime+00:03:00.000000000; `AAPL; 151.00; 400);
`trade insert (baseTime+00:04:00.000000000; `MSFT; 381.00; 500);
`trade insert (baseTime+00:05:00.000000000; `AAPL; 152.00; 600);
`trade insert (baseTime+00:06:00.000000000; `GOOG; 141.00; 700);
`trade insert (baseTime+00:07:00.000000000; `AMZN; 175.00; 800);
`trade insert (baseTime+00:08:00.000000000; `TSLA; 250.00; 900);
`trade insert (baseTime+00:09:00.000000000; `AAPL; 153.00; 1000);

/ Verify test data loaded
-1 "Test data: ",string[count trade]," trades loaded";

/ ========================================
/ getTradesBySym TESTS
/ ========================================

-1 "\n=== Testing getTradesBySym ===";

/ Test 1: Single symbol filter
-1 "Running: test_single_symbol";
aaplTrades:getTradesBySym[enlist`AAPL];
addTest[`sym_single_count; 4=count aaplTrades; "AAPL returns 4 trades"];
addTest[`sym_single_all_aapl; all aaplTrades[`sym]=`AAPL; "All results are AAPL"];

/ Test 2: Multiple symbols filter
-1 "Running: test_multiple_symbols";
multiTrades:getTradesBySym[`AAPL`MSFT];
addTest[`sym_multi_count; 6=count multiTrades; "AAPL+MSFT returns 6 trades"];
addTest[`sym_multi_correct; all multiTrades[`sym]in`AAPL`MSFT; "All results are AAPL or MSFT"];

/ Test 3: Empty symbol list returns all trades
-1 "Running: test_empty_symbol_list";
allTrades:getTradesBySym[`symbol$()];
addTest[`sym_empty_all; (count allTrades)=count trade; "Empty list returns all trades"];

/ Test 4: Symbol not in data returns empty
-1 "Running: test_symbol_not_found";
noTrades:getTradesBySym[enlist`NVDA];
addTest[`sym_notfound_empty; 0=count noTrades; "Unknown symbol returns empty"];
addTest[`sym_notfound_schema; `time`sym`price`size~cols noTrades; "Empty result has correct schema"];

/ Test 5: Result is a table
-1 "Running: test_result_is_table";
addTest[`sym_is_table; 98h=type aaplTrades; "Result is a table"];

/ ========================================
/ getTradesByTime TESTS
/ ========================================

-1 "\n=== Testing getTradesByTime ===";

/ Test 6: Time range filter (middle range)
-1 "Running: test_time_range_middle";
midStart:baseTime+00:02:00.000000000;
midEnd:baseTime+00:05:00.000000000;
midTrades:getTradesByTime[midStart;midEnd];
addTest[`time_mid_count; 4=count midTrades; "Time range [2min,5min] returns 4 trades"];

/ Test 7: Time range is inclusive on both bounds
-1 "Running: test_time_inclusive_bounds";
/ Check that exact boundary times are included
addTest[`time_start_included; any midTrades[`time]=midStart; "Start time is inclusive"];
addTest[`time_end_included; any midTrades[`time]=midEnd; "End time is inclusive"];

/ Test 8: Full range returns all trades
-1 "Running: test_time_full_range";
fullTrades:getTradesByTime[baseTime;baseTime+00:10:00.000000000];
addTest[`time_full_all; (count fullTrades)=count trade; "Full range returns all trades"];

/ Test 9: Range before data returns empty
-1 "Running: test_time_before_data";
beforeTrades:getTradesByTime[baseTime-01:00:00;baseTime-00:01:00];
addTest[`time_before_empty; 0=count beforeTrades; "Range before data returns empty"];

/ Test 10: Range after data returns empty
-1 "Running: test_time_after_data";
afterTrades:getTradesByTime[baseTime+01:00:00;baseTime+02:00:00];
addTest[`time_after_empty; 0=count afterTrades; "Range after data returns empty"];

/ Test 11: Single timestamp returns exact match
-1 "Running: test_exact_timestamp";
exactTime:baseTime+00:03:00.000000000;
exactTrades:getTradesByTime[exactTime;exactTime];
addTest[`time_exact_count; 1=count exactTrades; "Exact timestamp returns 1 trade"];
addTest[`time_exact_match; exactTime=first exactTrades`time; "Exact timestamp matches"];

/ Test 12: Inverted range returns empty
-1 "Running: test_inverted_range";
invertedTrades:getTradesByTime[baseTime+00:05:00;baseTime+00:02:00];
addTest[`time_inverted_empty; 0=count invertedTrades; "Inverted range returns empty"];

/ ========================================
/ getTrades COMBINED TESTS
/ ========================================

-1 "\n=== Testing getTrades (combined filter) ===";

/ Test 13: Filter by symbol and time
-1 "Running: test_combined_filter";
combStart:baseTime+00:00:00.000000000;
combEnd:baseTime+00:05:00.000000000;
combTrades:getTrades[enlist`AAPL;combStart;combEnd];
addTest[`comb_count; 3=count combTrades; "AAPL in [0,5min] returns 3 trades"];
addTest[`comb_sym_correct; all combTrades[`sym]=`AAPL; "Combined: all results are AAPL"];
addTest[`comb_time_correct; all combTrades[`time]within(combStart;combEnd); "Combined: all results in time range"];

/ Test 14: Multiple symbols with time range
-1 "Running: test_combined_multi_symbols";
multiCombTrades:getTrades[`AAPL`GOOG;combStart;combEnd];
addTest[`comb_multi_count; 4=count multiCombTrades; "AAPL+GOOG in [0,5min] returns 4 trades"];
addTest[`comb_multi_sym; all multiCombTrades[`sym]in`AAPL`GOOG; "Combined multi: correct symbols"];

/ Test 15: Empty symbol list with time range = all symbols
-1 "Running: test_combined_empty_sym";
allSymTrades:getTrades[`symbol$();combStart;combEnd];
addTest[`comb_empty_sym_count; 6=count allSymTrades; "Empty sym list in [0,5min] returns 6 trades"];

/ Test 16: Valid symbol but out of time range
-1 "Running: test_combined_no_overlap";
noOverlapTrades:getTrades[enlist`AAPL;baseTime+01:00:00;baseTime+02:00:00];
addTest[`comb_no_overlap; 0=count noOverlapTrades; "Valid symbol, out of time = empty"];

/ ========================================
/ EDGE CASE TESTS
/ ========================================

-1 "\n=== Testing edge cases ===";

/ Test 17: Empty table handling
-1 "Running: test_empty_table";
/ Save original data (as a copy)
origTradeRows:select from trade;
delete from `trade;

emptyBySym:getTradesBySym[enlist`AAPL];
emptyByTime:getTradesByTime[baseTime;baseTime+00:10:00];
emptyComb:getTrades[enlist`AAPL;baseTime;baseTime+00:10:00];

addTest[`empty_tbl_sym; 0=count emptyBySym; "Empty table: getTradesBySym returns empty"];
addTest[`empty_tbl_time; 0=count emptyByTime; "Empty table: getTradesByTime returns empty"];
addTest[`empty_tbl_comb; 0=count emptyComb; "Empty table: getTrades returns empty"];
addTest[`empty_tbl_schema; `time`sym`price`size~cols emptyBySym; "Empty result has correct schema"];

/ Restore original data
`trade insert origTradeRows;

/ Test 18: Single row table
-1 "Running: test_single_row";
origTradeRows2:select from trade;
delete from `trade;
`trade insert (baseTime; `AAPL; 150.00; 100);

singleBySym:getTradesBySym[enlist`AAPL];
singleByTime:getTradesByTime[baseTime;baseTime];
singleComb:getTrades[enlist`AAPL;baseTime;baseTime];

addTest[`single_row_sym; 1=count singleBySym; "Single row: getTradesBySym finds it"];
addTest[`single_row_time; 1=count singleByTime; "Single row: getTradesByTime finds it"];
addTest[`single_row_comb; 1=count singleComb; "Single row: getTrades finds it"];

/ Restore original data
delete from `trade;
`trade insert origTradeRows2;

/ ========================================
/ PROPERTY TESTS (independent validation)
/ ========================================

-1 "\n=== Property tests ===";

/ Test 19: Results are always subset of original
-1 "Running: test_subset_property";
addTest[`prop_subset_sym; (count getTradesBySym[enlist`AAPL])<=count trade; "getTradesBySym result <= trade"];
addTest[`prop_subset_time; (count getTradesByTime[baseTime;baseTime+00:05:00])<=count trade; "getTradesByTime result <= trade"];
addTest[`prop_subset_comb; (count getTrades[enlist`AAPL;baseTime;baseTime+00:05:00])<=count trade; "getTrades result <= trade"];

/ Test 20: Symbol filter result contains only requested symbols
-1 "Running: test_symbol_membership_property";
reqSyms:`AAPL`GOOG;
filteredBySyms:getTradesBySym[reqSyms];
addTest[`prop_sym_membership; all filteredBySyms[`sym]in reqSyms; "All results match requested symbols"];

/ Test 21: Time filter result is within bounds
-1 "Running: test_time_bounds_property";
propStart:baseTime+00:01:00.000000000;
propEnd:baseTime+00:06:00.000000000;
filteredByTime:getTradesByTime[propStart;propEnd];
addTest[`prop_time_bounds; all filteredByTime[`time]within(propStart;propEnd); "All results within time bounds"];

/ Test 22: Combined filter satisfies both constraints
-1 "Running: test_combined_property";
cPropSyms:`MSFT`GOOG;
cPropStart:baseTime;
cPropEnd:baseTime+00:07:00;
cPropResult:getTrades[cPropSyms;cPropStart;cPropEnd];
addTest[`prop_comb_sym; all cPropResult[`sym]in cPropSyms; "Combined: symbols match"];
addTest[`prop_comb_time; all cPropResult[`time]within(cPropStart;cPropEnd); "Combined: times in range"];

/ ========================================
/ INDEPENDENCE TESTS
/ ========================================

-1 "\n=== Independence tests ===";

/ Test 23: Verify by direct counting (not using the functions)
-1 "Running: test_independent_count";
/ Count AAPL trades directly with select
directAaplCount:count select from trade where sym=`AAPL;
funcAaplCount:count getTradesBySym[enlist`AAPL];
addTest[`indep_aapl_count; directAaplCount=funcAaplCount; "Direct AAPL count matches function"];

/ Test 24: Independent time range verification
-1 "Running: test_independent_time_count";
indepStart:baseTime+00:02:00.000000000;
indepEnd:baseTime+00:05:00.000000000;
directTimeCount:count select from trade where time within (indepStart;indepEnd);
funcTimeCount:count getTradesByTime[indepStart;indepEnd];
addTest[`indep_time_count; directTimeCount=funcTimeCount; "Direct time count matches function"];

/ Test 25: Independent combined verification
-1 "Running: test_independent_combined_count";
directCombCount:count select from trade where sym in `AAPL`MSFT, time within (baseTime;baseTime+00:05:00);
funcCombCount:count getTrades[`AAPL`MSFT;baseTime;baseTime+00:05:00];
addTest[`indep_comb_count; directCombCount=funcCombCount; "Direct combined count matches function"];

/ ========================================
/ STRESS TEST
/ ========================================

-1 "\n=== Stress test ===";

/ Test 26: Large dataset test
-1 "Running: stress_test_100k";

/ Generate large dataset using genTrades
\l gen.q

/ Save original, clear, and insert generated data
origTradeRows3:select from trade;
delete from `trade;
`trade insert genTrades[100000];

/ Get time bounds of generated data
minTime:min trade`time;
maxTime:max trade`time;
halfDiff:0D00:00:00.000000001*(`long$(maxTime-minTime))div 2;
midTime:minTime+halfDiff;

/ Performance timing
stressStart:.z.p;

/ Run queries
stressBySym:getTradesBySym[`AAPL`MSFT`GOOG];
stressByTime:getTradesByTime[minTime;midTime];
stressComb:getTrades[`AAPL;minTime;midTime];

stressElapsed:`long$(.z.p-stressStart)%1000000;  / ms

-1 "  Query time: ",string[stressElapsed],"ms for 3 queries on 100k rows";

addTest[`stress_sym_valid; all stressBySym[`sym]in`AAPL`MSFT`GOOG; "Stress: symbol filter correct"];
addTest[`stress_time_valid; all stressByTime[`time]within(minTime;midTime); "Stress: time filter correct"];
addTest[`stress_comb_sym; all stressComb[`sym]=`AAPL; "Stress: combined sym correct"];
addTest[`stress_comb_time; all stressComb[`time]within(minTime;midTime); "Stress: combined time correct"];
addTest[`stress_perf; stressElapsed<1000; "Stress: 3 queries under 1 second"];

/ Restore original data
delete from `trade;
`trade insert origTradeRows3;

/ ========================================
/ emptyTrade HELPER TEST
/ ========================================

-1 "\n=== Testing emptyTrade helper ===";

/ Test 27: emptyTrade returns correct schema
-1 "Running: test_empty_trade_helper";
et:emptyTrade[];
addTest[`empty_helper_type; 98h=type et; "emptyTrade returns a table"];
addTest[`empty_helper_schema; `time`sym`price`size~cols et; "emptyTrade has correct schema"];
addTest[`empty_helper_count; 0=count et; "emptyTrade is empty"];

/ ========================================
/ REPORT RESULTS
/ ========================================

-1 "\n=== Query Functions Test Suite Complete ===\n";
exit reportTests[tests]
