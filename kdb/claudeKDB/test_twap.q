/ Test Suite for TWAP Functions
/ Tests twap and twapBySym for correctness
/ Usage: q test_twap.q

/ Load dependencies
\l test_utils.q
\l tick.q
\l gen.q
\l twap.q

/ Clear the default trade from tick.q
delete from `trade;

/ Initialize test results
tests:initTests[]

/ Base timestamp used throughout known-value tests
baseTime:2026.01.01D09:30:00.000000000;

/ ========================================
/ KNOWN VALUE TESTS (Hand-Calculated)
/ ========================================

-1 "\n=== Known Value Tests ===";

/ Test 1: 3 trades with known TWAP
/ times 0, 2, 3 ns from base
/ deltas: (0, 2, 1)  ->  1_ gives (2, 1)  ->  durations
/ prices: 100, 200, 300  ->  -1_ gives (100, 200)
/ TWAP = (2*100 + 1*200) / 3 = 400/3 = 133.3333...
-1 "Running: three_trades_twap";
t3:([] time:baseTime+0 2 3; sym:3#enlist`AAPL; price:100.0 200.0 300.0; size:100 100 100);
result:twap[t3];
addTest[`three_trades_twap; (abs result-133.33333333333334)<1e-8; "3 trades: TWAP = 133.33"];

/ Test 2: 2 trades -> first price
/ times 0, 5 ns  ->  durations: (5,)  ;  prices: -1_ gives (100,)
/ TWAP = 100 * 5 / 5 = 100.0
-1 "Running: two_trades_twap";
t2:([] time:baseTime+0 5; sym:2#enlist`AAPL; price:100.0 200.0; size:100 100);
addTest[`two_trades_twap; 100.0=twap[t2]; "2 trades: TWAP = first price"];

/ Test 3: Equal spacing (4 trades)
/ times 0, 1, 2, 3  ->  durations (1, 1, 1)  ;  prices (100, 200, 300)
/ All weights equal: TWAP = avg(100, 200, 300) = 200.0
-1 "Running: equal_spacing_twap";
t4:([] time:baseTime+0 1 2 3; sym:4#enlist`AAPL; price:100.0 200.0 300.0 400.0; size:100 100 100 100);
addTest[`equal_spacing_twap; 200.0=twap[t4]; "Equal spacing: TWAP = avg of first N-1 prices"];

/ Test 4: Large gap dominates
/ times 0, 1000, 1001  ->  durations (1000, 1)  ;  prices (50, 200)
/ TWAP = (50*1000 + 200*1) / 1001 = 50200/1001 = 50.14985014985015
-1 "Running: large_gap_dominates";
tGap:([] time:baseTime+0 1000 1001; sym:3#enlist`AAPL; price:50.0 200.0 999.0; size:100 100 100);
gapResult:twap[tGap];
addTest[`large_gap_dominates; (abs gapResult-50.14985014985015)<1e-8; "Large gap: TWAP = 50.14985"];

/ Test 5 & 6: Multi-symbol via twapBySym
/ AAPL: equal-spacing times, prices 100 200 300 400  ->  TWAP = 200.0
/ MSFT: equal-spacing times, prices 500 600 700 800  ->  TWAP = 600.0
-1 "Running: multi_sym_aapl";
-1 "Running: multi_sym_msft";
tMulti:([] time:baseTime+0 1 2 3 0 1 2 3;
           sym:`AAPL`AAPL`AAPL`AAPL`MSFT`MSFT`MSFT`MSFT;
           price:100.0 200.0 300.0 400.0 500.0 600.0 700.0 800.0;
           size:100 100 100 100 100 100 100 100);
multiResult:twapBySym[tMulti];
aaplTwap:first exec twap from multiResult where sym=`AAPL;
msftTwap:first exec twap from multiResult where sym=`MSFT;
addTest[`multi_sym_aapl; 200.0=aaplTwap; "twapBySym: AAPL TWAP = 200.0"];
addTest[`multi_sym_msft; 600.0=msftTwap; "twapBySym: MSFT TWAP = 600.0"];

/ ========================================
/ SCHEMA AND STRUCTURE TESTS
/ ========================================

-1 "\n=== Schema and Structure Tests ===";

/ Test 7: twapBySym returns a keyed table (type 99h)
-1 "Running: bysym_is_keyed";
addTest[`bysym_is_keyed; 99h=type multiResult; "twapBySym returns keyed table"];

/ Test 8: Result contains the twap column
-1 "Running: bysym_has_twap";
addTest[`bysym_has_twap; `twap in cols value multiResult; "Result has twap column"];

/ Test 9: twap column has float type (char "f" from meta)
-1 "Running: twap_col_type_f";
vtMeta:exec c!t from meta value multiResult;
addTest[`twap_col_type_f; "f"=vtMeta`twap; "twap column type is float"];

/ ========================================
/ EDGE CASE TESTS
/ ========================================

-1 "\n=== Edge Case Tests ===";

/ Test 10: Empty table returns null
/ wavg on empty lists -> 0n
-1 "Running: empty_null";
emptyT:([] time:`timestamp$(); sym:`symbol$(); price:`float$(); size:`long$());
addTest[`empty_null; null twap[emptyT]; "Empty table returns null"];

/ Test 11: Single trade returns null
/ After 1_ and -1_, both lists are empty -> wavg returns 0n
-1 "Running: single_null";
singleT:([] time:enlist baseTime; sym:enlist`AAPL; price:enlist 150.0; size:enlist 100);
addTest[`single_null; null twap[singleT]; "Single trade returns null"];

/ Test 12: Two trades -> first price (explicit edge case)
/ Only one duration, one price: TWAP = that price
-1 "Running: two_trades_first_price";
t2edge:([] time:baseTime+0 10; sym:2#enlist`AAPL; price:42.0 999.0; size:100 100);
addTest[`two_trades_first_price; 42.0=twap[t2edge]; "Two trades: TWAP = first price"];

/ Test 13: All same timestamp -> null (all durations are zero)
/ Zero weights cause wavg to return 0n
-1 "Running: same_time_null";
tSameTime:([] time:4#enlist baseTime; sym:4#enlist`AAPL; price:100.0 200.0 300.0 400.0; size:100 100 100 100);
addTest[`same_time_null; null twap[tSameTime]; "All same timestamp: TWAP = null"];

/ Test 14: All same price -> that price regardless of timing
/ wavg of identical values is that value (when weights are non-zero)
-1 "Running: same_price_is_price";
tSamePrice:([] time:baseTime+0 1 2 3; sym:4#enlist`AAPL; price:4#75.0; size:100 100 100 100);
addTest[`same_price_is_price; 75.0=twap[tSamePrice]; "All same price: TWAP = that price"];

/ Test 15: Large time gap -> TWAP pulled strongly toward the long-held price
/ gapResult was computed above: ~50.15, which is close to 50 (the dominant price)
-1 "Running: large_gap_near_first";
addTest[`large_gap_near_first; gapResult<51.0; "Large gap: TWAP close to long-held price"];

/ ========================================
/ PROPERTY-BASED TESTS
/ ========================================

-1 "\n=== Property-Based Tests ===";

/ Generate 1000 random trades for property checks
`trade insert genTrades[1000];

/ Test 16: TWAP lies within [min price, max price] of input
-1 "Running: twap_in_range";
tw:twap[trade];
minP:min trade`price;
maxP:max trade`price;
addTest[`twap_in_range; (null tw) or tw within (minP;maxP); "TWAP within [min,max] price range"];

/ Test 17: Each symbol's TWAP lies within its own price range
-1 "Running: bysym_each_in_range";
twBySym:twapBySym[trade];
allInRange:all {[s]
  symTwap:first exec twap from twBySym where sym=s;
  symPrices:exec price from trade where sym=s;
  (null symTwap) or symTwap within (min symPrices;max symPrices)
 } each distinct trade`sym;
addTest[`bysym_each_in_range; allInRange; "Each symbol's TWAP in its price range"];

/ Test 18: Uniform price -> TWAP equals that price
/ Equal weights or any weights: wavg of identical values is that value
-1 "Running: uniform_price_twap";
uniformPriceT:([] time:baseTime+til 10; sym:10#enlist`AAPL; price:10#99.0; size:10#100);
addTest[`uniform_price_twap; 99.0=twap[uniformPriceT]; "Uniform price: TWAP = that price"];

/ Test 19: twapBySym result for one symbol matches twap of filtered data
/ Independent check: group-by aggregation must equal per-symbol function call
-1 "Running: bysym_vs_filtered";
testSym:first distinct trade`sym;
bySymVal:first exec twap from twBySym where sym=testSym;
filteredVal:twap[select from trade where sym=testSym];
addTest[`bysym_vs_filtered; (abs bySymVal-filteredVal)<1e-10; "twapBySym matches filtered twap"];

/ Test 20: Equal-spacing -> TWAP equals avg of all-but-last prices
/ With unit durations the weighted avg reduces to arithmetic avg
-1 "Running: equal_spacing_avg";
eqSpacingT:([] time:baseTime+til 6; sym:6#enlist`AAPL; price:10.0 20.0 30.0 40.0 50.0 60.0; size:6#100);
eqResult:twap[eqSpacingT];
eqExpected:avg -1_ eqSpacingT`price;
addTest[`equal_spacing_avg; eqResult=eqExpected; "Equal spacing: TWAP = avg of all-but-last prices"];

/ Test 21: Both TWAP and VWAP return non-null for same data
/ Confirms both metrics are computable on the same dataset
-1 "Running: twap_vs_vwap_both_valid";
\l vwap.q
vw:vwap[trade];
addTest[`twap_vs_vwap_both_valid; (not null tw) and not null vw; "Both TWAP and VWAP are non-null"];

/ ========================================
/ INTEGRATION TESTS (with query functions)
/ ========================================

-1 "\n=== Integration Tests ===";

\l query.q

/ Test 22: twap works with genTrades output
-1 "Running: with_gentrades";
genData:genTrades[500];
genTwap:twap[genData];
addTest[`with_gentrades; (null genTwap) or genTwap within (min genData`price;max genData`price); "twap works with genTrades"];

/ Test 23: twap works with getTradesBySym result
-1 "Running: with_getTradesBySym";
symsToTest:`AAPL`MSFT;
queryResult:getTradesBySym[symsToTest];
if[1<count queryResult;
  qTwap:twap[queryResult];
  addTest[`with_getTradesBySym; (null qTwap) or qTwap within (min queryResult`price;max queryResult`price);
    "twap works with getTradesBySym"]
 ];

/ Test 24: twap works with getTrades (combined symbol + time filter)
-1 "Running: with_getTrades";
minTime:min trade`time;
maxTime:max trade`time;
midTime:minTime+0D00:00:00.000000001*(`long$(maxTime-minTime))div 2;
combResult:getTrades[enlist`AAPL;minTime;midTime];
if[1<count combResult;
  cTwap:twap[combResult];
  addTest[`with_getTrades; (null cTwap) or cTwap within (min combResult`price;max combResult`price);
    "twap works with getTrades"]
 ];

/ Test 25: Both TWAP and VWAP lie within the overall price range
-1 "Running: twap_vwap_in_range";
addTest[`twap_vwap_in_range; (tw within (minP;maxP)) and vw within (minP;maxP); "Both TWAP and VWAP within price range"];

/ ========================================
/ STRESS TESTS
/ ========================================

-1 "\n=== Stress Tests ===";

-1 "Running: stress_test_1M";
stressData:genTrades[1000000];

/ Test 26: twap on 1M rows completes under 100ms
stressStart:.z.p;
stressTwap:twap[stressData];
stressElapsed:`long$(.z.p-stressStart)%1000000;
-1 "  TWAP calculation time: ",string[stressElapsed],"ms for 1M rows";
addTest[`stress_not_null; not null stressTwap; "Stress: TWAP computed"];
addTest[`stress_in_range; stressTwap within (min stressData`price;max stressData`price); "Stress: TWAP in range"];
addTest[`stress_perf; stressElapsed<100; "Stress: 1M rows under 100ms"];

/ Test 27: twapBySym on 1M rows completes under 200ms
stressStart2:.z.p;
stressBySym:twapBySym[stressData];
stressElapsed2:`long$(.z.p-stressStart2)%1000000;
-1 "  twapBySym calculation time: ",string[stressElapsed2],"ms for 1M rows";
addTest[`stress_bysym_count; 5=count stressBySym; "Stress: 5 symbols returned"];
addTest[`stress_bysym_perf; stressElapsed2<200; "Stress: twapBySym under 200ms"];

/ ========================================
/ REPORT RESULTS
/ ========================================

-1 "\n=== TWAP Test Suite Complete ===\n";
exit reportTests[tests]
