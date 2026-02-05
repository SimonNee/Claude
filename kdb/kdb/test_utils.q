/ Reusable Test Utilities for KDB+/Q
/ Based on Q Phrasebook idioms

/ ========================================
/ GENERAL TABLE VALIDATORS
/ These can be reused across any q table
/ ========================================

/ Check if table exists in workspace
tableExists:{[tblName] tblName in tables[]}

/ Check if table has expected columns (exact match)
hasColumns:{[tbl;expectedCols] expectedCols~cols tbl}

/ Check if table columns match expected types
/ Usage: checkTypes[trade; `time`sym`price!"psf"]
checkTypes:{[tbl;typeDict] typeDict~exec c!t from meta tbl}

/ Check if table is non-empty
isNonEmpty:{[tbl] 0<count tbl}

/ Check for nulls in specified columns
/ Returns boolean - 1b if no nulls found
hasNoNulls:{[tbl;colList] all raze not null flip tbl colList}

/ Check if all values in column are positive
allPositive:{[tbl;col] all 0<tbl col}

/ Check if column values are in ascending order
isAscending:{[tbl;col] (asc tbl col)~tbl col}

/ Check if column values are unique
isUnique:{[tbl;col] (count tbl)=count distinct tbl col}

/ ========================================
/ FINANCIAL DATA VALIDATORS
/ Domain-specific for trade/quote data
/ ========================================

/ Check if all prices are valid (positive, not infinite)
validPrices:{[tbl;priceCol] all(0<tbl[priceCol])&tbl[priceCol]within(-0w;0w)}

/ Check if all sizes are valid (positive integers)
validSizes:{[tbl;sizeCol] all 0<tbl sizeCol}

/ Check if timestamps are not in the future
noFutureTimes:{[tbl;timeCol] all tbl[timeCol]<=.z.p}

/ Check if timestamps are within a reasonable range
/ Usage: timesInRange[trade; `time; 2020.01.01D00:00:00; .z.p]
timesInRange:{[tbl;timeCol;startTime;endTime]
  all tbl[timeCol]within(startTime;endTime)}

/ Check for duplicate trades (by time and symbol)
noDuplicates:{[tbl;keyCols]
  (count tbl)=count distinct tbl keyCols}

/ ========================================
/ STATISTICAL VALIDATORS
/ Based on stat.md patterns
/ ========================================

/ Check if values are within N standard deviations of mean
withinStdDevs:{[vals;n]
  mu:avg vals;
  sigma:dev vals;
  all vals within(mu-n*sigma;mu+n*sigma)
 }

/ Check if values are within specified bounds
withinBounds:{[vals;lower;upper] all vals within(lower;upper)}

/ ========================================
/ TEST FRAMEWORK HELPERS
/ ========================================

/ Initialize test results table
initTests:{[] ([] name:`symbol$(); result:`boolean$(); msg:())}

/ Add test result to global tests table
/ Usage: addTest[`test_name; 1b; "Description"]
addTest:{[nm;res;msg] `tests insert (nm;res;msg)}

/ Report test results
reportTests:{[testTable]
  passed:sum testTable`result;
  failed:sum not testTable`result;

  -1 "\n=== Test Results ===\n";

  / Show failed tests if any
  if[0<failed;
    -1 "FAILED TESTS:";
    -1 .Q.s select from testTable where not result;
    -1 ""
   ];

  / Summary statistics
  -1 "Passed: ",string passed;
  -1 "Failed: ",string failed;
  -1 "Total:  ",string count testTable;
  -1 "Pass rate: ",string[100*passed%count testTable],"%\n";

  / Return exit code
  :$[failed>0;1;0]
 }

/ ========================================
/ EXAMPLE USAGE
/ ========================================

/ Example: Testing a trade table
/
/ tests:initTests[]
/ addTest[`exists; tableExists[`trade]; "Trade table exists"]
/ addTest[`positive_prices; allPositive[trade;`price]; "Prices > 0"]
/ addTest[`no_nulls; hasNoNulls[trade;`time`sym]; "No nulls in keys"]
/ addTest[`ascending; isAscending[trade;`time]; "Time ascending"]
/ exit reportTests[tests]
