/ Test Suite for VWAP Functions
/ Tests vwap and vwapBySym for correctness
/ Usage: q test_vwap.q

/ Load dependencies
\l test_utils.q
\l tick.q
\l gen.q
\l vwap.q

/ Clear the default trade from tick.q
delete from `trade;

/ Initialize test results
tests:initTests[]

/ ========================================
/ KNOWN VALUE TESTS (Hand-Calculated)
/ ========================================

-1 "\n=== Known Value Tests ===";

/ Test 1: Two trades with known VWAP
/ (10*100 + 20*200) / (100+200) = (1000 + 4000) / 300 = 5000/300 = 16.666...
-1 "Running: test_two_trades_known_value";
t2:([]price:10.0 20.0; size:100 200);
expected1:16.66666666666667;
result1:vwap[t2];
addTest[`two_trades_vwap; (abs result1-expected1)<1e-10; "Two trades: VWAP = 16.666..."];

/ Test 2: Three trades with equal sizes (should equal simple average)
/ (100 + 200 + 300) / 3 = 200
-1 "Running: test_equal_sizes_is_avg";
t3:([]price:100.0 200.0 300.0; size:100 100 100);
expected2:200.0;
result2:vwap[t3];
addTest[`equal_sizes_avg; result2=expected2; "Equal sizes: VWAP = avg(prices) = 200"];

/ Test 3: Single large trade dominates
/ (100*1 + 200*1000) / 1001 ≈ 199.9
-1 "Running: test_large_trade_dominates";
t4:([]price:100.0 200.0; size:1 1000);
result3:vwap[t4];
addTest[`large_dominates; (result3>199.0) and result3<200.0; "Large trade dominates: VWAP ≈ 199.9"];

/ Test 4: Verify exact calculation
/ (50*500 + 100*1000 + 150*500) / 2000 = (25000 + 100000 + 75000) / 2000 = 100
-1 "Running: test_exact_calculation";
t5:([]price:50.0 100.0 150.0; size:500 1000 500);
expected3:100.0;
result4:vwap[t5];
addTest[`exact_calc; result4=expected3; "Exact: VWAP = 100.0"];

/ ========================================
/ EDGE CASE TESTS
/ ========================================

-1 "\n=== Edge Case Tests ===";

/ Test 5: Empty table returns null
-1 "Running: test_empty_table";
emptyT:([]price:`float$(); size:`long$());
addTest[`empty_null; null vwap[emptyT]; "Empty table returns 0n"];

/ Test 6: Single trade returns that price
-1 "Running: test_single_trade";
singleT:([]price:enlist 150.0; size:enlist 1000);
addTest[`single_trade; 150.0=vwap[singleT]; "Single trade returns its price"];

/ Test 7: Zero total volume returns null
-1 "Running: test_zero_volume";
zeroVolT:([]price:100.0 200.0; size:0 0);
addTest[`zero_vol_null; null vwap[zeroVolT]; "Zero volume returns 0n"];

/ Test 8: Mixed zero and non-zero volumes
/ Only non-zero trades count: (100*0 + 200*500) / 500 = 200
-1 "Running: test_mixed_zero_volume";
mixedT:([]price:100.0 200.0; size:0 500);
addTest[`mixed_zero; 200.0=vwap[mixedT]; "Zero-volume trades ignored"];

/ ========================================
/ PROPERTY-BASED TESTS
/ ========================================

-1 "\n=== Property-Based Tests ===";

/ Generate test data
`trade insert genTrades[1000];

/ Test 9: VWAP is within price range
-1 "Running: test_vwap_in_range";
v:vwap[trade];
minP:min trade`price;
maxP:max trade`price;
addTest[`vwap_in_range; (v>=minP) and v<=maxP; "VWAP within [min,max] price range"];

/ Test 10: VWAP is order invariant (shuffling doesn't change result)
-1 "Running: test_order_invariant";
shuffled:trade neg[count trade]?til count trade;  / Random permutation
vOriginal:vwap[trade];
vShuffled:vwap[shuffled];
addTest[`order_invariant; (abs vOriginal-vShuffled)<1e-10; "VWAP unchanged by row order"];

/ Test 11: Uniform sizes equals simple average
-1 "Running: test_uniform_sizes";
uniformT:([]price:100.0 150.0 200.0 250.0; size:100 100 100 100);
uniformVwap:vwap[uniformT];
uniformAvg:avg uniformT`price;
addTest[`uniform_is_avg; uniformVwap=uniformAvg; "Uniform sizes: VWAP = avg(price)"];

/ Test 12: Adding zero-volume trades doesn't change VWAP
-1 "Running: test_zero_trades_no_effect";
baseT:([]price:100.0 200.0; size:100 200);
baseVwap:vwap[baseT];
withZeroT:([]price:100.0 200.0 999.0 888.0; size:100 200 0 0);
withZeroVwap:vwap[withZeroT];
addTest[`zero_no_effect; baseVwap=withZeroVwap; "Zero-volume trades don't affect VWAP"];

/ ========================================
/ INDEPENDENT VALIDATION TESTS
/ ========================================

-1 "\n=== Independent Validation Tests ===";

/ Test 13: Compare vwap function with direct exec
-1 "Running: test_vs_exec";
funcResult:vwap[trade];
execResult:exec size wavg price from trade;
addTest[`vs_exec; funcResult~execResult; "vwap[] matches exec size wavg price"];

/ Test 14: Compare with manual calculation (sum/sum form)
-1 "Running: test_vs_manual";
manualResult:(sum trade[`price]*trade`size)%sum trade`size;
addTest[`vs_manual; (abs funcResult-manualResult)<1e-10; "vwap[] matches manual sum/sum"];

/ ========================================
/ vwapBySym TESTS
/ ========================================

-1 "\n=== vwapBySym Tests ===";

/ Test 15: Returns keyed table
-1 "Running: test_bysym_keyed";
bySymResult:vwapBySym[trade];
addTest[`bysym_is_table; 99h=type bySymResult; "vwapBySym returns keyed table"];

/ Test 16: Has correct columns
-1 "Running: test_bysym_columns";
addTest[`bysym_has_vwap; `vwap in cols value bySymResult; "Result has vwap column"];

/ Test 17: Each symbol's VWAP is in that symbol's price range
-1 "Running: test_bysym_in_range";
allInRange:all {[s]
  symVwap:exec vwap from vwapBySym[trade] where sym=s;
  symPrices:exec price from trade where sym=s;
  (first symVwap)within(min symPrices;max symPrices)
 } each distinct trade`sym;
addTest[`bysym_each_in_range; allInRange; "Each symbol's VWAP in its price range"];

/ Test 18: Compare with filtered vwap
-1 "Running: test_bysym_vs_filtered";
testSym:first distinct trade`sym;
bySymVwap:first exec vwap from vwapBySym[trade] where sym=testSym;
filteredVwap:vwap[select from trade where sym=testSym];
addTest[`bysym_vs_filtered; (abs bySymVwap-filteredVwap)<1e-10; "vwapBySym matches filtered vwap"];

/ ========================================
/ INTEGRATION TESTS (with query functions)
/ ========================================

-1 "\n=== Integration Tests ===";

/ Load query functions
\l query.q

/ Test 19: Works with getTradesBySym
-1 "Running: test_with_getTradesBySym";
symsToTest:`AAPL`MSFT;
queryResult:getTradesBySym[symsToTest];
if[0<count queryResult;
  v:vwap[queryResult];
  addTest[`with_getTradesBySym; (not null v) and v within (min queryResult`price;max queryResult`price);
    "vwap works with getTradesBySym"]
 ];

/ Test 20: Works with getTradesByTime
-1 "Running: test_with_getTradesByTime";
minTime:min trade`time;
maxTime:max trade`time;
midTime:minTime+0D00:00:00.000000001*(`long$(maxTime-minTime))div 2;
timeResult:getTradesByTime[minTime;midTime];
if[0<count timeResult;
  v:vwap[timeResult];
  addTest[`with_getTradesByTime; (not null v) and v within (min timeResult`price;max timeResult`price);
    "vwap works with getTradesByTime"]
 ];

/ Test 21: Works with getTrades (combined filter)
-1 "Running: test_with_getTrades";
combResult:getTrades[enlist`AAPL;minTime;midTime];
if[0<count combResult;
  v:vwap[combResult];
  addTest[`with_getTrades; (not null v) and v within (min combResult`price;max combResult`price);
    "vwap works with getTrades"]
 ];

/ ========================================
/ STRESS TEST
/ ========================================

-1 "\n=== Stress Test ===";

/ Test 22: Large dataset performance
-1 "Running: stress_test_1M";

/ Generate 1M trades
stressData:genTrades[1000000];

/ Time the VWAP calculation
stressStart:.z.p;
stressVwap:vwap[stressData];
stressElapsed:`long$(.z.p-stressStart)%1000000;  / ms

-1 "  VWAP calculation time: ",string[stressElapsed],"ms for 1M rows";

addTest[`stress_not_null; not null stressVwap; "Stress: VWAP computed"];
addTest[`stress_in_range; stressVwap within (min stressData`price;max stressData`price); "Stress: VWAP in range"];
addTest[`stress_perf; stressElapsed<100; "Stress: 1M rows under 100ms"];

/ Test 23: vwapBySym on large dataset
stressStart2:.z.p;
stressBySym:vwapBySym[stressData];
stressElapsed2:`long$(.z.p-stressStart2)%1000000;

-1 "  vwapBySym calculation time: ",string[stressElapsed2],"ms for 1M rows";

addTest[`stress_bysym_count; 5=count stressBySym; "Stress: 5 symbols returned"];
addTest[`stress_bysym_perf; stressElapsed2<200; "Stress: vwapBySym under 200ms"];

/ ========================================
/ REPORT RESULTS
/ ========================================

-1 "\n=== VWAP Test Suite Complete ===\n";
exit reportTests[tests]
