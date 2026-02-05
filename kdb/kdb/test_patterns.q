/ Advanced Testing Patterns from Q Phrasebook
/ Demonstrates idiomatic test idioms

/ ========================================
/ COMPARISON PATTERNS (test.md)
/ ========================================

/ Do ranges match?
/ Usage: Check if two tables have same distinct values in a column
rangesMatch:{[x;y] (~)over('[asc;distinct])each(x;y)}

/ Example:
/ x:1 2 3
/ y:3 1 2 1
/ rangesMatch[x;y]  / 1b

/ Are x and y permutations of each other?
arePermutations:{[x;y] (asc x)~asc y}

/ Example:
/ x:15 16 13 18 14 11 12
/ y:15 16 13 14 18 12 11
/ arePermutations[x;y]  / 1b

/ ========================================
/ SEQUENCE VALIDATION (test.md)
/ ========================================

/ Are items in ascending order?
isAscending:{[x] all(>=)prior x}
/ Alternative: {x~asc x}

/ Are items unique?
isUnique:{[x] x~distinct x}

/ Is x a permutation vector?
/ (contains exactly 0 to n-1)
isPermutation:{[x] x~rank x}

/ ========================================
/ NUMERICAL TESTS (test.md)
/ ========================================

/ Are items integral (no fractional part)?
isIntegral:{[x] x=floor x}

/ Are items even?
isEven:{[x] not x mod 2}

/ Are items in interval [low,high)?
inInterval:{[x;low;high] (</')x<\:low,high}

/ Example:
/ x:19 20 21 39 40 41
/ inInterval[x;20;40]  / 011100b

/ ========================================
/ FLAG OPERATIONS (flag.md)
/ ========================================

/ First 1 in boolean vector
firstOne:{[x] x?1}
/ Alternative: {first where x}

/ Last 1 in boolean vector
lastOne:{[x] last where x}
/ Alternative: {count[x]-1+(reverse x)?1}

/ Lengths of groups of 1s
groupLengths:{[x] deltas sums[x]where 1_(<)prior x,0}

/ Example:
/ x:0 0 1 1 1 0 0 1 1 1 1 0 1
/ groupLengths x  / 3 4 1

/ First 1 in each group of 1s
firstInGroup:{[x] 1_(>)prior 0,x}

/ Last 1 in each group of 1s
lastInGroup:{[x] 1_(<)prior x,0}

/ ========================================
/ TABLE VALIDATION
/ ========================================

/ Check if table schema matches expected
/ Returns: (missingCols; extraCols; wrongTypes)
schemaCheck:{[tbl;expectedSchema]
  actual:meta tbl;
  expected:expectedSchema;

  actualCols:exec c from actual;
  expectedCols:key expected;

  missing:expectedCols except actualCols;
  extra:actualCols except expectedCols;

  / Check types for common columns
  common:actualCols inter expectedCols;
  actualTypes:exec c!t from actual where c in common;
  expectedTypes:expected common;
  wrongTypes:where not expectedTypes~'actualTypes;

  :(missing;extra;wrongTypes)
 }

/ Check referential integrity between tables
/ Usage: checkForeignKey[orders;`custID;customers;`id]
checkForeignKey:{[childTbl;childCol;parentTbl;parentCol]
  childVals:distinct childTbl childCol;
  parentVals:parentTbl parentCol;
  all childVals in parentVals
 }

/ ========================================
/ TEMPORAL VALIDATION (temp.md)
/ ========================================

/ Check if timestamps have no gaps larger than threshold
/ Usage: noLargeGaps[trade`time; 0D00:01:00]  / no gaps > 1 minute
noLargeGaps:{[times;maxGap]
  gaps:deltas times;
  all 1_gaps<=maxGap  / skip first (always 0N)
 }

/ Check if timestamps are within business hours
/ Usage: inBusinessHours[times; 09:30:00; 16:00:00]
inBusinessHours:{[times;openTime;closeTime]
  timeOnly:`time$times;
  all timeOnly within(openTime;closeTime)
 }

/ ========================================
/ STATISTICAL VALIDATION (stat.md concepts)
/ ========================================

/ Check if values are normally distributed (simple test)
/ Using interquartile range method
looksNormal:{[vals]
  / Remove nulls
  v:vals where not null vals;

  / Calculate quartiles
  q1:v iasc[v]floor .25*count v;
  q3:v iasc[v]floor .75*count v;
  iqr:q3-q1;

  / Check if most values within 1.5*IQR of quartiles
  lower:q1-1.5*iqr;
  upper:q3+1.5*iqr;
  outliers:sum not v within(lower;upper);

  / Expect < 5% outliers for normal distribution
  (outliers%count v)<0.05
 }

/ Check for suspicious patterns (e.g., all prices ending in .00)
hasVariedDecimals:{[prices]
  / Get decimal parts
  decimals:prices-floor prices;
  / Should have at least 10% variety
  (count distinct decimals)>0.1*count prices
 }

/ ========================================
/ CROSS-TABLE CONSISTENCY
/ ========================================

/ Check if aggregate matches detail
/ Usage: aggregateMatches[tradeDetail`notional; tradeAgg`totalNotional; `tradeID]
aggregateMatches:{[detailVals;aggVal;groupCol]
  calculated:sum detailVals;
  calculated~aggVal
 }

/ Check if related tables have matching row counts
/ (for 1:1 relationships)
matchingCounts:{[tbl1;tbl2] (count tbl1)=count tbl2}

/ ========================================
/ PROPERTY-BASED TESTING PATTERNS
/ ========================================

/ Idempotence: applying function twice gives same result as once
/ Usage: testIdempotence[asc;1 3 2]
testIdempotence:{[f;data] (f data)~f f data}

/ Commutativity: f[x;y] = f[y;x]
testCommutative:{[f;x;y] (f[x;y])~f[y;x]}

/ Associativity: f[f[x;y];z] = f[x;f[y;z]]
testAssociative:{[f;x;y;z] (f[f[x;y];z])~f[x;f[y;z]]}

/ Identity: f[x;identity] = x
testIdentity:{[f;x;identity] (f[x;identity])~x}

/ ========================================
/ EXAMPLE USAGE
/ ========================================

/ Example 1: Schema validation
/
/ expectedSchema:`time`sym`price`size!"psf j"
/ (missing;extra;wrong):schemaCheck[trade;expectedSchema]
/ all(0=count missing;0=count extra;0=count wrong)

/ Example 2: Data quality
/
/ prices:trade`price
/ all(
/   allPositive[trade;`price];
/   hasVariedDecimals[prices];
/   looksNormal[prices]
/ )

/ Example 3: Temporal validation
/
/ times:trade`time
/ all(
/   isAscending[times];
/   noLargeGaps[times;0D00:01:00];
/   inBusinessHours[times;09:30;16:00]
/ )
