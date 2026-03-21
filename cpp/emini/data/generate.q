/ E-mini S&P 500 futures order book event stream generator
/ Uses an Ornstein-Uhlenbeck process for mid-price simulation
/ .
/ OU parameters chosen:
/   mu    = 5500.00  (long-run mean, dollars)
/   theta = 0.005    (mean-reversion speed per step)
/   sigma = 1.25     (per-step diffusion, dollars)
/ .
/ Stationary std = sigma / sqrt(2 * theta)
/               = 1.25 / sqrt(0.01)
/               = 1.25 / 0.1
/               = 12.50 dollars = 50 ticks
/ .
/ Active range: +/-200 ticks (+/-$50) around mu = [5450.00, 5550.00]
/ The stationary std is 50 ticks so ~95% of prices land within +/-100 ticks
/ and essentially all prices stay inside the +/-200 tick active range.
/ .
/ Prices are converted to integer ticks using BASE = 4400.0:
/   tick = floor( (price - BASE) * 4 + 0.5 )
/ The OU mid price 5500.0 maps to tick (5500-4400)*4 = 4400.
/ The OU-generated price range (5447-5547) maps to ticks 1788-2588.
/ Valid tick range: [0, 8799]  (covers +/-20% CME price limit)
/ .
/ Runnable standalone: q generate.q
/ Output: orders.csv written to the same directory

/ -------------------------------------------------------------------
/ Configuration
/ -------------------------------------------------------------------

TICK:0.25                    / ES tick size, dollars
MU:5500.00                   / OU long-run mean
THETA:0.005                  / mean-reversion speed per step
SIGMA:1.25                   / per-step diffusion (dollars)
STAT_STD:SIGMA%sqrt 2*THETA  / theoretical stationary std = 12.50 dollars

BASE:4400.0                  / tick floor: CME -20% price limit (dollars)
TICKS_PER_DOLLAR:4           / ES has 4 ticks per dollar ($0.25/tick)
MAX_TICK:8799                / tick ceiling: CME +20% price limit (inclusive)

TOTAL_EVENTS:1000000         / total rows: ADD + CANCEL + MATCH
SEED:42                      / random seed for reproducibility
CANCELS_PER_ADD:10           / cancel events per add event

/ Derived counts
/ Each add block: 1 ADD + CANCELS_PER_ADD CANCELs = 11 events
/ Remaining events (after all blocks) are MATCHes
/ Block + 1 MATCH per block => 12 events per "cycle"
/ ADDS_COUNT = floor(TOTAL / 12)
ADDS_COUNT:floor TOTAL_EVENTS%1+CANCELS_PER_ADD+1
CANCEL_COUNT:CANCELS_PER_ADD*ADDS_COUNT
MATCH_COUNT:TOTAL_EVENTS-ADDS_COUNT+CANCEL_COUNT

/ -------------------------------------------------------------------
/ Random seed
/ -------------------------------------------------------------------

system "S ",string SEED

/ -------------------------------------------------------------------
/ Helper: snap price to nearest ES tick
/ -------------------------------------------------------------------

snapTick:{[p] TICK*floor 0.5+p%TICK}

/ -------------------------------------------------------------------
/ Helper: convert dollar price to integer tick index
/ tick = floor( (price - BASE) * TICKS_PER_DOLLAR + 0.5 )
/ Returns long.
/ -------------------------------------------------------------------

priceToTick:{[p] `long$floor 0.5+(p-BASE)*TICKS_PER_DOLLAR}

/ -------------------------------------------------------------------
/ Helper: generate n standard normal variates via Box-Muller
/ Input: n (long), returns n-element float vector
/ -------------------------------------------------------------------

genNormals:{[n]
  nPairs:ceiling n%2;
  / Clamp u1 away from 0 to avoid log(0)
  u1:1e-10|nPairs?1.0;
  u2:nPairs?1.0;
  piTwo:2*acos -1.0;
  z1:sqrt[-2*log u1]*cos piTwo*u2;
  z2:sqrt[-2*log u1]*sin piTwo*u2;
  / Interleave pairs and return exactly n values
  n#raze z1,'z2
 }

/ -------------------------------------------------------------------
/ OU price path
/ P[t+1] = P[t] + theta*(mu - P[t]) + sigma*Z[t]
/ Implemented with scan (\) seeded at MU.
/ q's seeded scan: seed {f}\ vec returns count[vec] elements.
/ The seed (MU) is NOT included in the result.
/ We snap each step to the nearest valid ES tick.
/ -------------------------------------------------------------------

ouNormals:genNormals ADDS_COUNT
midPrices:snapTick MU {x+(THETA*(MU-x))+(SIGMA*y)}\ ouNormals

/ midPrices has exactly ADDS_COUNT elements — one mid-price per ADD event.

/ -------------------------------------------------------------------
/ ADD event properties
/ Side alternates: even index = BID, odd index = ASK
/ BID tick: mid - 1 tick (one tick below mid)
/ ASK tick: mid + 1 tick (one tick above mid)
/ -------------------------------------------------------------------

addIdx:til ADDS_COUNT
addSides:`BID`ASK addIdx mod 2

/ Offset vector: BID gets -TICK, ASK gets +TICK
sideOffset:(TICK*-1 1) addIdx mod 2
addPrices:snapTick midPrices+sideOffset

/ Convert ADD prices to integer ticks
addTicks:priceToTick addPrices

/ Quantities: 70% small (1-5 contracts), 30% large (6-50 contracts)
genQty:{[n]
  uThresh:n?1.0;
  smQty:1+`long$5*n?1.0;
  lgQty:6+`long$45*n?1.0;
  smQty+`long$(lgQty-smQty)*uThresh>=0.70
 }

addQtys:genQty ADDS_COUNT

/ -------------------------------------------------------------------
/ Book state and CANCEL ref generation
/ .
/ We process ADD blocks sequentially because CANCELs must reference
/ live orders (ADDs not yet cancelled). State: live order lists per side.
/ .
/ Row layout in the final CSV:
/   block i contains rows [i*BLOCK .. i*BLOCK+BLOCK-1]
/   row i*BLOCK is the ADD; rows i*BLOCK+1 .. i*BLOCK+BLOCK-1 are CANCELs
/   MATCH rows follow after all blocks.
/ .
/ CANCEL ref_idx = the CSV row index of the ADD being cancelled.
/ .
/ Each ADD block must produce exactly CANCELS_PER_ADD cancel refs.
/ When fewer than CANCELS_PER_ADD live orders exist (early bootstrap),
/ we pick with replacement from the available live list, including
/ the ADD we just registered (which is immediately live after registration).
/ This means some early CANCEL events may reference the same ADD twice,
/ which is acceptable for synthetic benchmark data.
/ -------------------------------------------------------------------

BLOCK:1+CANCELS_PER_ADD      / rows per block: 1 ADD + 10 CANCELs

bidLive:0#0j                 / row indices of live BID ADDs
askLive:0#0j                 / row indices of live ASK ADDs
cancelRefs:0#0j              / accumulated cancel ref_idx values

i:0
while[i<ADDS_COUNT;
  addRow:`long$i*BLOCK;
  / Register this ADD as live
  $[addSides[i]=`BID;
    bidLive,:enlist addRow;
    askLive,:enlist addRow
  ];
  / Pick CANCELS_PER_ADD refs from the live list on this side.
  / We pick with replacement when live count < CANCELS_PER_ADD
  / (replacement sampling: positive N in ? operator).
  liveList:$[addSides[i]=`BID; bidLive; askLive];
  nLive:count liveList;
  $[nLive>=CANCELS_PER_ADD;
    [
      / Enough live orders: pick without replacement
      pickPos:neg[CANCELS_PER_ADD]?nLive;
      picked:liveList pickPos;
      cancelRefs,:picked;
      / Remove cancelled orders from live list
      remaining:liveList except picked;
      $[addSides[i]=`BID; bidLive:remaining; askLive:remaining]
    ];
    [
      / Bootstrap phase: pick with replacement from available live orders
      pickPos:CANCELS_PER_ADD?nLive;
      picked:liveList pickPos;
      cancelRefs,:picked;
      / Remove the distinct set of cancelled orders from live list
      remaining:liveList except distinct picked;
      $[addSides[i]=`BID; bidLive:remaining; askLive:remaining]
    ]
  ];
  i+:1
 ]

/ CANCEL sides: each ADD at index i spawns CANCELS_PER_ADD cancels on same side.
/ Expand addSides so each entry repeats CANCELS_PER_ADD times.
cancelSides:raze CANCELS_PER_ADD#/:addSides

/ -------------------------------------------------------------------
/ MATCH events
/ Alternate BID/ASK aggressor.
/ BID aggressor buys: crosses the spread, tick = mid + 1 tick (hits resting ask)
/ ASK aggressor sells: crosses the spread, tick = mid - 1 tick (hits resting bid)
/ -------------------------------------------------------------------

matchSides:`BID`ASK (til MATCH_COUNT) mod 2
matchNormals:genNormals MATCH_COUNT
matchMids:snapTick MU+SIGMA*matchNormals
matchOffsets:(TICK*1 -1) matchSides=`BID
matchPrices:snapTick matchMids+matchOffsets

/ Convert MATCH prices to integer ticks
matchTicks:priceToTick matchPrices

/ Match quantities: 1-20 contracts
matchQtys:1+`long$19*MATCH_COUNT?1.0

/ -------------------------------------------------------------------
/ Assemble interleaved ADD+CANCEL section
/ Pre-allocate nInterleaved rows, fill by index position.
/ ADD positions: i*BLOCK for i in 0..ADDS_COUNT-1
/ CANCEL positions: all other positions in 0..nInterleaved-1
/ -------------------------------------------------------------------

nInterleaved:`long$BLOCK*ADDS_COUNT
addRowsIL:`long$BLOCK*til ADDS_COUNT

/ Boolean mask: true at ADD positions
isAddMask:nInterleaved#0b
@[`isAddMask; addRowsIL; :; 1b];
cancelRowsIL:where not isAddMask

/ Pre-allocate interleaved columns (values will all be overwritten)
/ Tick column is long integer: ADD rows get computed ticks, CANCEL rows get 0j
iEvtType :nInterleaved#enlist`ADD
iEvtSide :nInterleaved#`BID
iEvtTick :nInterleaved#0j
iEvtQty  :nInterleaved#0j
iEvtRef  :nInterleaved#0j

/ Fill ADD rows
@[`iEvtType;  addRowsIL; :; ADDS_COUNT#enlist`ADD];
@[`iEvtSide;  addRowsIL; :; addSides];
@[`iEvtTick;  addRowsIL; :; addTicks];
@[`iEvtQty;   addRowsIL; :; addQtys];
@[`iEvtRef;   addRowsIL; :; ADDS_COUNT#0j];

/ Fill CANCEL rows: tick is 0j (integer zero), qty is 0j
@[`iEvtType;  cancelRowsIL; :; CANCEL_COUNT#enlist`CANCEL];
@[`iEvtSide;  cancelRowsIL; :; cancelSides];
@[`iEvtTick;  cancelRowsIL; :; CANCEL_COUNT#0j];
@[`iEvtQty;   cancelRowsIL; :; CANCEL_COUNT#0j];
@[`iEvtRef;   cancelRowsIL; :; cancelRefs];

/ Append MATCH events after the interleaved section
allEvtType :iEvtType  ,MATCH_COUNT#enlist`MATCH
allEvtSide :iEvtSide  ,matchSides
allEvtTick :iEvtTick  ,matchTicks
allEvtQty  :iEvtQty   ,matchQtys
allEvtRef  :iEvtRef   ,MATCH_COUNT#0j

/ -------------------------------------------------------------------
/ Validation: check all non-zero ticks are within [0, MAX_TICK]
/ CANCEL rows have tick=0j; ADD and MATCH rows must be in [0, MAX_TICK].
/ -------------------------------------------------------------------

nonZeroTicks:allEvtTick where allEvtTick>0j
outOfRange:nonZeroTicks where not nonZeroTicks within (0j;`long$MAX_TICK)
$[0<count outOfRange;
  -2 "WARNING: ",( string count outOfRange)," ticks out of range [0,",( string MAX_TICK),"]: min=",( string min outOfRange)," max=",( string max outOfRange);
  -1 "Tick range validation passed: all non-zero ticks within [0,",( string MAX_TICK),"]"
 ]

/ -------------------------------------------------------------------
/ Write CSV
/ Tick column is long; string converts to plain integer (e.g. "4391").
/ No float formatting needed.
/ -------------------------------------------------------------------

hdr:"event_type,side,tick,quantity,ref_idx"

colEvtType:string allEvtType
colSide   :string allEvtSide
colTick   :string allEvtTick
colQty    :string allEvtQty
colRef    :string allEvtRef

rows:colEvtType,'",",'colSide,'",",'colTick,'",",'colQty,'",",'colRef

outPath:`:orders.csv
outPath 0: enlist[hdr],rows

/ -------------------------------------------------------------------
/ Summary statistics
/ -------------------------------------------------------------------

totalRows:count rows
addCnt   :sum allEvtType=`ADD
cancelCnt:sum allEvtType=`CANCEL
matchCnt :sum allEvtType=`MATCH

/ Tick stats: exclude CANCEL rows (tick=0j)
nonZeroTk:allEvtTick where allEvtTick>0j
tkMin :min nonZeroTk
tkMax :max nonZeroTk
tkMean:avg nonZeroTk

/ Observed OU std from ADD mid-prices (still in dollars for reference)
ouObsStd:dev midPrices

actualRatio:cancelCnt%addCnt

-1 "";
-1 "=== E-mini Order Book Generator Summary ===";
-1 "Total rows written    : ",string totalRows;
-1 "  ADD events          : ",string addCnt;
-1 "  CANCEL events       : ",string cancelCnt;
-1 "  MATCH events        : ",string matchCnt;
-1 "Cancel-to-add ratio   : ",.Q.f[2] actualRatio;
-1 "Tick min (non-zero)   : ",string tkMin;
-1 "Tick max              : ",string tkMax;
-1 "Tick mean             : ",.Q.f[2] tkMean;
-1 "OU stat std (theory)  : ",(.Q.f[2] STAT_STD)," dollars / ",(string`long$STAT_STD%TICK)," ticks";
-1 "OU stat std (observed): ",(.Q.f[2] ouObsStd)," dollars / ",(string`long$ouObsStd%TICK)," ticks";
-1 "Output               : orders.csv";
-1 "";

exit 0
