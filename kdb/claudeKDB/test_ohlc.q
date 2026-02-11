/ Test Suite for OHLC Bar Functions
/ Tests ohlc and ohlcAll for correctness
/ Usage: q test_ohlc.q

/ Load dependencies
\l test_utils.q
\l tick.q
\l gen.q
\l ohlc.q

/ Clear the default trade from tick.q
delete from `trade;

/ Initialize test results
tests:initTests[]

/ ========================================
/ KNOWN VALUE TESTS (Hand-Calculated)
/ ========================================

-1 "\n=== Known Value Tests ===";

/ Build controlled test data at explicit minute boundaries.
/ genTrades uses nanosecond spacing so all trades land in the same minute.
/ We construct our own timestamps spanning two distinct minutes.
/ NOTE: bare "/" starts a block comment in q — always use "/ " with trailing space
/ Rows: AAPL 09:30 (100.0/100, 105.0/200), AAPL 09:31 (102.0/150, 98.0/250)
/       MSFT 09:30 (200.0/300, 210.0/100)
/ 1-min ohlc: AAPL 09:30 O=100 H=105 L=100 C=105 V=300 N=2
/             AAPL 09:31 O=102 H=102 L=98  C=98  V=400 N=2
/             MSFT 09:30 O=200 H=210 L=200 C=210 V=400 N=2
/ 5-min ohlc: AAPL 09:30 O=100 H=105 L=98  C=98  V=700 N=4 (all 4 trades merge)
/             MSFT 09:30 O=200 H=210 L=200 C=210 V=400 N=2
/ 1-min ohlcAll: 09:30 O=100 H=210 L=100 C=210 V=700 N=4 (AAPL first, MSFT last)
/                09:31 O=102 H=102 L=98  C=98  V=400 N=2

baseTime:2026.01.01D09:30:00.000000000;
t0930:baseTime;
t0930b:baseTime+0D00:00:30.000000000;
t0931:baseTime+0D00:01:00.000000000;
t0931b:baseTime+0D00:01:30.000000000;

knownTrades:([]
  time:  t0930,  t0930b, t0931,  t0931b, t0930,  t0930b;
  sym:   `AAPL`AAPL`AAPL`AAPL`MSFT`MSFT;
  price: 100.0,  105.0,  102.0,   98.0,  200.0,  210.0;
  size:  100,    200,    150,     250,    300,    100
 );

/ Pre-compute results and flat views for the known-value section
kResult1:ohlc[knownTrades;1];
kFlat1:0!kResult1;

/ ----------------------------------------
/ Test 1: AAPL 09:30 bar — open
/ ----------------------------------------
-1 "Running: test_aapl_0930_open";
aaplBar0930:select from kFlat1 where sym=`AAPL, bar=09:30;
addTest[`aapl_0930_open; 100.0=first exec open from aaplBar0930; "AAPL 09:30 open = 100.0"];

/ ----------------------------------------
/ Test 2: AAPL 09:30 bar — high, low, close, volume, cnt
/ ----------------------------------------
-1 "Running: test_aapl_0930_bar_values";
addTest[`aapl_0930_high;  105.0=first exec high   from aaplBar0930; "AAPL 09:30 high = 105.0"];
addTest[`aapl_0930_low;   100.0=first exec low    from aaplBar0930; "AAPL 09:30 low = 100.0"];
addTest[`aapl_0930_close; 105.0=first exec close  from aaplBar0930; "AAPL 09:30 close = 105.0"];
addTest[`aapl_0930_vol;   300 =first exec volume  from aaplBar0930; "AAPL 09:30 volume = 300"];
addTest[`aapl_0930_cnt;   2   =first exec cnt     from aaplBar0930; "AAPL 09:30 cnt = 2"];

/ ----------------------------------------
/ Test 3: AAPL 09:31 bar
/ ----------------------------------------
-1 "Running: test_aapl_0931_bar_values";
aaplBar0931:select from kFlat1 where sym=`AAPL, bar=09:31;
addTest[`aapl_0931_open;  102.0=first exec open   from aaplBar0931; "AAPL 09:31 open = 102.0"];
addTest[`aapl_0931_high;  102.0=first exec high   from aaplBar0931; "AAPL 09:31 high = 102.0"];
addTest[`aapl_0931_low;    98.0=first exec low    from aaplBar0931; "AAPL 09:31 low = 98.0"];
addTest[`aapl_0931_close;  98.0=first exec close  from aaplBar0931; "AAPL 09:31 close = 98.0"];
addTest[`aapl_0931_vol;   400 =first exec volume  from aaplBar0931; "AAPL 09:31 volume = 400"];
addTest[`aapl_0931_cnt;   2   =first exec cnt     from aaplBar0931; "AAPL 09:31 cnt = 2"];

/ ----------------------------------------
/ Test 4: MSFT 09:30 bar
/ ----------------------------------------
-1 "Running: test_msft_0930_bar_values";
msftBar0930:select from kFlat1 where sym=`MSFT, bar=09:30;
addTest[`msft_0930_open;  200.0=first exec open   from msftBar0930; "MSFT 09:30 open = 200.0"];
addTest[`msft_0930_high;  210.0=first exec high   from msftBar0930; "MSFT 09:30 high = 210.0"];
addTest[`msft_0930_low;   200.0=first exec low    from msftBar0930; "MSFT 09:30 low = 200.0"];
addTest[`msft_0930_close; 210.0=first exec close  from msftBar0930; "MSFT 09:30 close = 210.0"];
addTest[`msft_0930_vol;   400 =first exec volume  from msftBar0930; "MSFT 09:30 volume = 400"];
addTest[`msft_0930_cnt;   2   =first exec cnt     from msftBar0930; "MSFT 09:30 cnt = 2"];

/ ----------------------------------------
/ Test 5: 5-minute bar aggregation — AAPL collapses to single bar
/ ----------------------------------------
-1 "Running: test_5min_aapl_bar_aggregation";
kResult5:ohlc[knownTrades;5];
kFlat5:0!kResult5;
aaplBar5:select from kFlat5 where sym=`AAPL, bar=09:30;
addTest[`5min_aapl_open;  100.0=first exec open   from aaplBar5; "5-min AAPL open = 100.0 (first trade)"];
addTest[`5min_aapl_high;  105.0=first exec high   from aaplBar5; "5-min AAPL high = 105.0"];
addTest[`5min_aapl_low;    98.0=first exec low    from aaplBar5; "5-min AAPL low = 98.0 (all 4 trades merged)"];
addTest[`5min_aapl_close;  98.0=first exec close  from aaplBar5; "5-min AAPL close = 98.0 (last trade)"];
addTest[`5min_aapl_vol;   700 =first exec volume  from aaplBar5; "5-min AAPL volume = 700"];
addTest[`5min_aapl_cnt;   4   =first exec cnt     from aaplBar5; "5-min AAPL cnt = 4"];

/ ----------------------------------------
/ Test 6: ohlcAll known values — 1-min bars
/ ----------------------------------------
-1 "Running: test_ohlcall_known_values";
kaResult1:ohlcAll[knownTrades;1];
kaFlat1:0!kaResult1;
bar0930All:select from kaFlat1 where bar=09:30;
/ open = first price across all syms at 09:30; AAPL row comes first in table, price=100.0
addTest[`ohlcall_0930_open;  100.0=first exec open   from bar0930All; "ohlcAll 09:30 open = 100.0 (AAPL first)"];
addTest[`ohlcall_0930_high;  210.0=first exec high   from bar0930All; "ohlcAll 09:30 high = 210.0"];
addTest[`ohlcall_0930_low;   100.0=first exec low    from bar0930All; "ohlcAll 09:30 low = 100.0"];
addTest[`ohlcall_0930_close; 210.0=first exec close  from bar0930All; "ohlcAll 09:30 close = 210.0 (MSFT last)"];
addTest[`ohlcall_0930_vol;   700 =first exec volume  from bar0930All; "ohlcAll 09:30 volume = 700"];
addTest[`ohlcall_0930_cnt;   4   =first exec cnt     from bar0930All; "ohlcAll 09:30 cnt = 4"];

/ ----------------------------------------
/ Test 7: Row counts are correct
/ ----------------------------------------
-1 "Running: test_row_counts";
/ 1-min ohlc: 3 bars (AAPL/09:30, AAPL/09:31, MSFT/09:30)
addTest[`ohlc_1min_rowcount; 3=count kFlat1; "ohlc 1-min has 3 rows"];
/ 1-min ohlcAll: 2 bars (09:30 and 09:31)
addTest[`ohlcall_1min_rowcount; 2=count kaFlat1; "ohlcAll 1-min has 2 rows"];
/ 5-min ohlc: 2 bars (AAPL/09:30, MSFT/09:30)
addTest[`ohlc_5min_rowcount; 2=count kFlat5; "ohlc 5-min has 2 rows"];

/ ----------------------------------------
/ Test 8: Bar key values are correct minutes
/ ----------------------------------------
-1 "Running: test_bar_key_values";
/ ohlc 1-min bars should have minutes 09:30 and 09:31 for AAPL
aaplBars:(asc exec bar from kFlat1 where sym=`AAPL);
addTest[`bar_keys_correct; aaplBars~09:30 09:31; "AAPL bars are minutes 09:30 and 09:31"];
/ ohlcAll bars should be 09:30 and 09:31
allBars:(asc exec bar from kaFlat1);
addTest[`ohlcall_bar_keys; allBars~09:30 09:31; "ohlcAll bars are minutes 09:30 and 09:31"];

/ ========================================
/ SCHEMA / STRUCTURE TESTS
/ ========================================

-1 "\n=== Schema and Structure Tests ===";

-1 "Running: test_ohlc_returns_keyed_table";
addTest[`ohlc_type_99h; 99h=type kResult1; "ohlc returns keyed table (type 99h)"];

-1 "Running: test_ohlcall_returns_keyed_table";
addTest[`ohlcall_type_99h; 99h=type kaResult1; "ohlcAll returns keyed table (type 99h)"];

-1 "Running: test_ohlc_key_columns";
/ Key table of ohlc should have sym and bar columns
addTest[`ohlc_key_cols; `sym`bar~cols key kResult1; "ohlc key columns are sym and bar"];

-1 "Running: test_ohlcall_key_columns";
/ Key table of ohlcAll should have only bar column
addTest[`ohlcall_key_cols; (enlist`bar)~cols key kaResult1; "ohlcAll key column is bar only"];

-1 "Running: test_ohlc_value_columns";
/ Value table columns
addTest[`ohlc_val_cols; `open`high`low`close`volume`cnt~cols value kResult1; "ohlc value columns are open high low close volume cnt"];

-1 "Running: test_ohlc_column_types";
/ Check types of value columns: open/high/low/close=float("f"), volume=long("j"), cnt=long("j")
vtMeta:exec c!t from meta value kResult1;
addTest[`ohlc_open_type;   "f"=vtMeta`open;   "open column type is float"];
addTest[`ohlc_high_type;   "f"=vtMeta`high;   "high column type is float"];
addTest[`ohlc_low_type;    "f"=vtMeta`low;    "low column type is float"];
addTest[`ohlc_close_type;  "f"=vtMeta`close;  "close column type is float"];
addTest[`ohlc_volume_type; "j"=vtMeta`volume; "volume column type is long"];
addTest[`ohlc_cnt_type;    "j"=vtMeta`cnt;    "cnt column type is long"];

-1 "Running: test_ohlc_key_column_types";
/ Key columns: sym=symbol("s"), bar=minute("u")
ktMeta:exec c!t from meta key kResult1;
addTest[`ohlc_sym_type; "s"=ktMeta`sym; "sym key column type is symbol"];
addTest[`ohlc_bar_type; "u"=ktMeta`bar; "bar key column type is minute"];

/ ========================================
/ EDGE CASE TESTS
/ ========================================

-1 "\n=== Edge Case Tests ===";

/ ----------------------------------------
/ Test: Empty table input
/ ----------------------------------------
-1 "Running: test_empty_table";
emptyTrades:([] time:`timestamp$(); sym:`symbol$(); price:`float$(); size:`long$());
emptyResult:ohlc[emptyTrades;1];
addTest[`empty_is_keyed; 99h=type emptyResult; "Empty input: ohlc returns keyed table"];
addTest[`empty_no_rows;  0=count 0!emptyResult; "Empty input: result has 0 rows"];

-1 "Running: test_empty_ohlcall";
emptyAllResult:ohlcAll[emptyTrades;1];
addTest[`empty_all_keyed; 99h=type emptyAllResult; "Empty input: ohlcAll returns keyed table"];
addTest[`empty_all_rows;  0=count 0!emptyAllResult; "Empty input: ohlcAll result has 0 rows"];

/ ----------------------------------------
/ Test: Single trade — open=high=low=close=price, volume=size, cnt=1
/ ----------------------------------------
-1 "Running: test_single_trade";
singleTrade:([]
  time: enlist 2026.01.01D10:00:00.000000000;
  sym:  enlist`AAPL;
  price:enlist 150.0;
  size: enlist 200
 );
singleResult:0!ohlc[singleTrade;1];
addTest[`single_open_eq_price;  150.0=first exec open  from singleResult; "Single trade: open=price"];
addTest[`single_high_eq_price;  150.0=first exec high  from singleResult; "Single trade: high=price"];
addTest[`single_low_eq_price;   150.0=first exec low   from singleResult; "Single trade: low=price"];
addTest[`single_close_eq_price; 150.0=first exec close from singleResult; "Single trade: close=price"];
addTest[`single_volume;         200  =first exec volume from singleResult; "Single trade: volume=size"];
addTest[`single_cnt_is_1;       1    =first exec cnt    from singleResult; "Single trade: cnt=1"];

/ ----------------------------------------
/ Test: All same price — open=high=low=close
/ ----------------------------------------
-1 "Running: test_all_same_price";
samePriceTrades:([]
  time:  2026.01.01D09:30:00.000000000+0D00:00:01.000000000*til 5;
  sym:   5#enlist`AAPL;
  price: 5#50.0;
  size:  100 100 100 100 100
 );
samePriceResult:0!ohlc[samePriceTrades;1];
addTest[`same_price_ohlc_equal;
  all (first exec open from samePriceResult)=(first exec high from samePriceResult),
      (first exec low from samePriceResult),
      (first exec close from samePriceResult);
  "All same price: open=high=low=close"];

/ ----------------------------------------
/ Test: Single symbol only (no MSFT)
/ ----------------------------------------
-1 "Running: test_single_symbol_only";
aaplOnlyTrades:select from knownTrades where sym=`AAPL;
aaplOnlyResult:0!ohlc[aaplOnlyTrades;1];
/ Result should have only AAPL rows
addTest[`single_sym_only_aapl; all`AAPL=exec sym from aaplOnlyResult; "Single symbol: only AAPL in result"];
addTest[`single_sym_no_msft;   not`MSFT in exec sym from aaplOnlyResult; "Single symbol: no MSFT in result"];

/ ----------------------------------------
/ Test: Two adjacent minute bars produce exactly 2 bars per symbol
/ ----------------------------------------
-1 "Running: test_two_adjacent_bars";
aaplResult1:select from 0!ohlc[aaplOnlyTrades;1] where sym=`AAPL;
addTest[`two_adjacent_bars; 2=count aaplResult1; "AAPL with 2 minutes produces exactly 2 bars"];

/ ----------------------------------------
/ Test: barMins=1 produces more bars than barMins=60
/ ----------------------------------------
-1 "Running: test_wider_bar_fewer_rows";
result1min:ohlc[knownTrades;1];
result60min:ohlc[knownTrades;60];
addTest[`wider_bar_fewer_rows; (count result1min)>(count result60min); "1-min bars produce more rows than 60-min bars"];

/ ========================================
/ PROPERTY-BASED TESTS
/ ========================================

-1 "\n=== Property-Based Tests ===";

/ Generate test data for property tests
`trade insert genTrades[1000];
propResult:ohlc[trade;1];
propFlat:0!propResult;

/ ----------------------------------------
/ Test: low <= high (fundamental OHLC invariant)
/ ----------------------------------------
-1 "Running: test_low_le_high";
addTest[`low_le_high; all propFlat[`low]<=propFlat`high; "low <= high for all bars"];

/ ----------------------------------------
/ Test: low <= open for every bar
/ ----------------------------------------
-1 "Running: test_low_le_open";
addTest[`low_le_open; all propFlat[`low]<=propFlat`open; "low <= open for all bars"];

/ ----------------------------------------
/ Test: low <= close for every bar
/ ----------------------------------------
-1 "Running: test_low_le_close";
addTest[`low_le_close; all propFlat[`low]<=propFlat`close; "low <= close for all bars"];

/ ----------------------------------------
/ Test: open <= high for every bar
/ ----------------------------------------
-1 "Running: test_open_le_high";
addTest[`open_le_high; all propFlat[`open]<=propFlat`high; "open <= high for all bars"];

/ ----------------------------------------
/ Test: close <= high for every bar
/ ----------------------------------------
-1 "Running: test_close_le_high";
addTest[`close_le_high; all propFlat[`close]<=propFlat`high; "close <= high for all bars"];

/ ----------------------------------------
/ Test: Volume conservation — sum of bar volumes = sum of input sizes
/ ----------------------------------------
-1 "Running: test_volume_conservation";
addTest[`volume_conservation; (sum trade`size)=sum propFlat`volume; "sum(bar volumes) = sum(input sizes)"];

/ ----------------------------------------
/ Test: Count conservation — sum of bar cnts = number of input rows
/ ----------------------------------------
-1 "Running: test_count_conservation";
addTest[`count_conservation; (count trade)=sum propFlat`cnt; "sum(bar cnts) = count(input rows)"];

/ ----------------------------------------
/ Test: All OHLC prices within global [min price; max price]
/ ----------------------------------------
-1 "Running: test_all_prices_in_global_range";
globalMin:min trade`price;
globalMax:max trade`price;
allOhlcPrices:raze propFlat[`open],propFlat[`high],propFlat[`low],propFlat`close;
addTest[`prices_in_global_range; all allOhlcPrices within(globalMin;globalMax); "All OHLC prices within global price range"];

/ ========================================
/ INTEGRATION TESTS
/ ========================================

-1 "\n=== Integration Tests ===";

/ Load query functions
\l query.q

/ ----------------------------------------
/ Test: Works with genTrades output
/ All genTrades trades land in the same minute (nanosecond spacing)
/ so each symbol should produce exactly 1 bar
/ ----------------------------------------
-1 "Running: test_with_gentrades_output";
genData:genTrades[500];
genResult:0!ohlc[genData;1];
/ Each symbol should appear at most once per minute bar
/ Property: volume conservation holds
addTest[`gen_volume_conservation; (sum genData`size)=sum genResult`volume; "ohlc on genTrades: volume conserved"];
addTest[`gen_count_conservation; (count genData)=sum genResult`cnt; "ohlc on genTrades: cnt conserved"];

/ ----------------------------------------
/ Test: Works with getTradesBySym filter
/ ----------------------------------------
-1 "Running: test_with_getTradesBySym";
aaplFiltered:getTradesBySym[enlist`AAPL];
if[0<count aaplFiltered;
  filteredResult:0!ohlc[aaplFiltered;1];
  addTest[`with_getTradesBySym; all`AAPL=exec sym from filteredResult; "ohlc on getTradesBySym[AAPL]: only AAPL bars"]
 ];

/ ----------------------------------------
/ Test: ohlcAll total volume = ohlc total volume for same data
/ ----------------------------------------
-1 "Running: test_ohlcall_vs_ohlc_volume";
ohlcVol:sum (0!ohlc[trade;1])`volume;
ohlcAllVol:sum (0!ohlcAll[trade;1])`volume;
addTest[`ohlcall_vol_eq_ohlc; ohlcVol=ohlcAllVol; "ohlcAll total volume = ohlc total volume"];

/ ----------------------------------------
/ Test: ohlcAll total cnt = ohlc total cnt for same data
/ ----------------------------------------
-1 "Running: test_ohlcall_vs_ohlc_cnt";
ohlcCnt:sum (0!ohlc[trade;1])`cnt;
ohlcAllCnt:sum (0!ohlcAll[trade;1])`cnt;
addTest[`ohlcall_cnt_eq_ohlc; ohlcCnt=ohlcAllCnt; "ohlcAll total cnt = ohlc total cnt"];

/ ----------------------------------------
/ Test: Single-bar VWAP is within [low, high] range
/ genTrades nanosecond spacing means all trades in one bar per sym
/ ----------------------------------------
-1 "Running: test_vwap_within_bar_range";
\l vwap.q
vwapResult:0!vwapBySym[trade];
ohlcFor1bar:0!ohlc[trade;1];
/ For each symbol, check that its VWAP falls within its bar's [low, high]
testSym2:first distinct trade`sym;
symVwap:first exec vwap from vwapResult where sym=testSym2;
symOhlcRow:select from ohlcFor1bar where sym=testSym2;
symLow:first exec low from symOhlcRow;
symHigh:first exec high from symOhlcRow;
addTest[`vwap_within_bar; symVwap within(symLow;symHigh); "VWAP is within [low, high] for tested symbol"];

/ ========================================
/ STRESS TESTS
/ ========================================

-1 "\n=== Stress Tests ===";

/ ----------------------------------------
/ Test: 1M rows — ohlc performance < 200ms
/ ----------------------------------------
-1 "Running: stress_ohlc_1M";
stressData:genTrades[1000000];

stressStart:.z.p;
stressResult:ohlc[stressData;1];
stressElapsed:`long$(.z.p-stressStart)%1000000;

-1 "  ohlc 1-min calculation time: ",string[stressElapsed],"ms for 1M rows";

addTest[`stress_ohlc_not_empty; 0<count stressResult; "Stress: ohlc produced results"];
addTest[`stress_ohlc_perf; stressElapsed<200; "Stress: ohlc 1M rows under 200ms"];

/ ----------------------------------------
/ Test: 1M rows — ohlcAll performance < 200ms
/ ----------------------------------------
-1 "Running: stress_ohlcAll_1M";

stressStart2:.z.p;
stressAllResult:ohlcAll[stressData;1];
stressElapsed2:`long$(.z.p-stressStart2)%1000000;

-1 "  ohlcAll 1-min calculation time: ",string[stressElapsed2],"ms for 1M rows";

addTest[`stress_ohlcall_not_empty; 0<count stressAllResult; "Stress: ohlcAll produced results"];
addTest[`stress_ohlcall_perf; stressElapsed2<200; "Stress: ohlcAll 1M rows under 200ms"];

/ ----------------------------------------
/ Test: Property invariants hold at scale
/ ----------------------------------------
-1 "Running: stress_property_invariants";
stressFlat:0!stressResult;
addTest[`stress_low_le_high; all stressFlat[`low]<=stressFlat`high; "Stress: low <= high holds for all 1M-row bars"];
addTest[`stress_volume_conservation; (sum stressData`size)=sum stressFlat`volume; "Stress: volume conservation holds at 1M rows"];

/ ========================================
/ REPORT RESULTS
/ ========================================

-1 "\n=== OHLC Test Suite Complete ===\n";
exit reportTests[tests]
