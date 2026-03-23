/ ETH/USDT Binance spot L2 MBP orderbook event stream generator
/ Uses an Ornstein-Uhlenbeck process for mid-price simulation
/ .
/ OU parameters chosen:
/   mu    = 3000.00  (long-run mean, dollars)
/   theta = 0.005    (mean-reversion speed per step)
/   sigma = 3.00     (per-step diffusion, dollars)
/   drift = 0.0      (per-step directional drift, dollars; 0 = neutral)
/ .
/ Stationary std = sigma / sqrt(2 * theta)
/               = 3.00 / sqrt(0.01)
/               = 3.00 / 0.1
/               = 30.00 dollars = 3,000 ticks
/ .
/ Active range: roughly +/-90 dollars (+/-9,000 ticks) around mu
/ (3 stationary stds covers ~99.7% of the OU distribution)
/ .
/ Prices are converted to absolute integer ticks using scale 100:
/   tick = floor( price * 100 + 0.5 )
/ ETH price $3000.00 maps to tick 300000.
/ ETH tick size is $0.01 (100 ticks per dollar).
/ .
/ Event model: L2 MBP (Market By Price)
/   UPSERT — set a price level's total qty (qty > 0)
/   DELETE — remove a price level            (qty = 0)
/   No individual order identity; no ref_idx column.
/ .
/ Runnable standalone: q generate.q
/ Output: events.csv written to the same directory
/ .
/ CLI overrides (all optional):
/   q generate.q -sigma 10.0 -theta 0.002 -output events_volatile.csv
/   q generate.q -drift 0.50 -output events_trending.csv
/ Supported flags:
/   -sigma F     OU sigma in dollars       (default 3.00)
/   -theta F     OU theta                  (default 0.005)
/   -drift F     per-step drift dollars    (default 0.0)
/   -seed N      random seed               (default 42)
/   -events N    total event count         (default 1000000)
/   -upsert F    upsert fraction 0..1      (default 0.70)
/   -spread N    tick spread for new levels (default 500)
/   -maxlive N   max active levels/side    (default 200)
/   -output S    output filename           (default events.csv)

/ -------------------------------------------------------------------
/ Configuration (compile-time defaults)
/ -------------------------------------------------------------------

TICK_SIZE:0.01                / ETH tick size, dollars ($0.01)
MU:3000.00                    / OU long-run mean
THETA:0.005                   / mean-reversion speed per step
SIGMA:3.00                    / per-step diffusion (dollars)
DRIFT:0.0                     / per-step directional drift (dollars; 0 = neutral)

TOTAL_EVENTS:1000000          / total rows: UPSERT + DELETE
SEED:42                       / random seed for reproducibility
UPSERT_PROB:0.70              / fraction of events that are UPSERTs
UPDATE_PROB:0.50              / within UPSERTs: prob of updating an existing level
SPREAD_TICKS:500              / half-spread in ticks for new level placement
MAX_LIVE:200                  / max active levels per side
OUT_FILE:"events.csv"         / output filename (string; converted to symbol below)

/ -------------------------------------------------------------------
/ CLI parameter parsing
/ .
/ .z.x is a list of strings from the command line, e.g.:
/   ("-sigma";"10.0";"-output";"events_volatile.csv")
/ .
/ Strategy: scan .z.x for strings starting with "-"; the next
/ string (if it exists and does not itself start with "-") is its value.
/ Unknown flags are silently ignored.  A flag with no following value
/ keeps the compile-time default.
/ .
/ Keys are stored as symbols (e.g. `sigma) so the dict has a typed
/ key domain and lookup with in / indexing is unambiguous.
/ -------------------------------------------------------------------

cliArgs:.z.x
cliN:count cliArgs

/ Build a dict: symbol flag-name -> value-string
/ Walk indices; when element i starts with "-", treat element i+1 as value.
cliDict:()!()
cliI:0
while[cliI<cliN;
  tok:cliArgs[cliI];
  $["-"=first tok;
    [
      flagSym:`$1_tok;                          / strip "-", intern as symbol
      $[(cliI+1)<cliN;
        [
          nextTok:cliArgs[cliI+1];
          $["-"=first nextTok;
            [cliDict[flagSym]:""; cliI+:1];    / flag has no value; advance 1
            [cliDict[flagSym]:nextTok; cliI+:2] / store value; advance 2
          ]
        ];
        [cliDict[flagSym]:""; cliI+:1]         / last token, no value
      ]
    ];
    cliI+:1                                    / not a flag token; skip
  ]
 ]

/ Helper: look up a symbol flag, return default if absent or empty string
/ fSym: symbol key, defVal: default value, convFn: string->type converter
cliGet:{[fSym;defVal;convFn]
  $[fSym in key cliDict;
    [v:cliDict fSym; $[0<count v; convFn v; defVal]];
    defVal
  ]
 }

/ Apply overrides (mutate globals so everything downstream picks them up)
/ NOTE: use "J"$ and "F"$ (uppercase) to parse strings as numbers.
/ Backtick casts (`long$, `float$) convert char-by-char -- they do NOT parse.
SIGMA        :cliGet[`sigma;   SIGMA;        "F"$]
THETA        :cliGet[`theta;   THETA;        "F"$]
DRIFT        :cliGet[`drift;   DRIFT;        "F"$]
SEED         :cliGet[`seed;    SEED;         "J"$]
TOTAL_EVENTS :cliGet[`events;  TOTAL_EVENTS; "J"$]
UPSERT_PROB  :cliGet[`upsert;  UPSERT_PROB;  "F"$]
SPREAD_TICKS :cliGet[`spread;  SPREAD_TICKS; "J"$]
MAX_LIVE     :cliGet[`maxlive; MAX_LIVE;     "J"$]
OUT_FILE     :cliGet[`output;  OUT_FILE;     {x}]   / keep as string

/ -------------------------------------------------------------------
/ Derived OU statistic (must be recomputed after possible THETA/SIGMA override)
/ -------------------------------------------------------------------

STAT_STD:SIGMA%sqrt 2*THETA

/ -------------------------------------------------------------------
/ Random seed
/ -------------------------------------------------------------------

system "S ",string SEED

/ -------------------------------------------------------------------
/ Helper: snap price to nearest ETH tick ($0.01)
/ -------------------------------------------------------------------

snapTick:{[p] TICK_SIZE*floor 0.5+p%TICK_SIZE}

/ -------------------------------------------------------------------
/ Helper: convert dollar price to absolute integer tick
/ tick = floor( price * 100 + 0.5 )
/ Returns long.  Price $3000.00 -> tick 300000.
/ -------------------------------------------------------------------

priceToTick:{[p] `long$floor 0.5+p*100}

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
/ P[t+1] = P[t] + theta*(mu - P[t]) + drift + sigma*Z[t]
/ Implemented with scan (\) seeded at MU.
/ q's seeded scan: seed {f}\ vec returns count[vec] elements.
/ The seed (MU) is NOT included in the result.
/ We snap each step to the nearest valid ETH tick.
/ DRIFT is 0.0 in the default workload (neutral OU).
/ A non-zero DRIFT (e.g. +0.50) biases the walk upward each step.
/ -------------------------------------------------------------------

ouNormals:genNormals TOTAL_EVENTS
midPrices:snapTick MU {x+(THETA*(MU-x))+DRIFT+(SIGMA*y)}\ ouNormals

/ Clamp mid-prices to a plausible range: MU +/- 50% (very conservative)
pMin:MU*0.5    / $1500.00
pMax:MU*1.5    / $4500.00
midPrices:pMin|pMax&midPrices

/ Convert all mid-prices to integer ticks up front
midTicks:priceToTick midPrices

/ -------------------------------------------------------------------
/ Helper: generate n qty values for UPSERT events
/ 70% small: 0.01-1.0 ETH   -> 1,000,000  to 100,000,000 (1e6 to 1e8)
/ 30% large: 1.0-50.0 ETH   -> 100,000,000 to 5,000,000,000 (1e8 to 5e9)
/ All quantities are long integers, scale = 10^8 (1 ETH = 100000000).
/ -------------------------------------------------------------------

genQty:{[n]
  uThresh:n?1.0;
  smQty:`long$1000000+99000000*n?1.0;    / 1e6 to 1e8
  lgQty:`long$100000000+4900000000*n?1.0; / 1e8 to 5e9
  smQty+`long$(lgQty-smQty)*uThresh>=0.70
 }

/ -------------------------------------------------------------------
/ Sequential event loop
/ .
/ State: bidLive and askLive hold the set of currently-live tick values.
/ For each event step we decide UPSERT or DELETE, alternate sides, then:
/   UPSERT: pick or add a tick, generate a qty > 0.
/   DELETE: remove a random live tick, qty = 0.
/ .
/ Because DELETE events depend on which ticks are currently live, this
/ loop cannot be vectorised.  For 1,000,000 events it runs in q time.
/ .
/ Output columns are accumulated into four growing lists.
/ -------------------------------------------------------------------

bidLive:0#0j    / currently active bid ticks (long list)
askLive:0#0j    / currently active ask ticks (long list)

/ Pre-allocate output column vectors (avoids repeated list growth)
evtTypes:TOTAL_EVENTS#enlist`UPSERT
evtSides:TOTAL_EVENTS#`BID
evtTicks:TOTAL_EVENTS#0j
evtQtys :TOTAL_EVENTS#0j

/ Pre-draw all random values outside the loop for speed
uRandEvt:TOTAL_EVENTS?1.0    / decides UPSERT vs DELETE
uRandUpd:TOTAL_EVENTS?1.0    / decides new level vs update existing
uRandOff:TOTAL_EVENTS?1.0    / offset within spread for new level tick

i:0
while[i<TOTAL_EVENTS;
  midTk:midTicks[i];
  uEvt:uRandEvt[i];
  isBid:0=i mod 2;
  liveSet:$[isBid; bidLive; askLive];
  nLive:count liveSet;

  / --- Decide event type ---
  / If live set is empty and we drew DELETE: downgrade to UPSERT
  doUpsert:$[(uEvt<UPSERT_PROB) or (0=nLive); 1b; 0b];

  evtSides[i]:$[isBid; `BID; `ASK];

  $[doUpsert;
    [
      / UPSERT branch
      evtTypes[i]:`UPSERT;
      / Decide: add new level or update an existing one.
      / Always add new if live set is empty.
      / If at capacity (nLive >= MAX_LIVE): always update existing.
      / Otherwise: uRandUpd < UPDATE_PROB -> update existing; else -> add new.
      doNew:$[0=nLive; 1b; $[nLive>=MAX_LIVE; 0b; uRandUpd[i]>=UPDATE_PROB]];
      $[doNew;
        [
          / New level: draw offset in [1, SPREAD_TICKS] from mid
          / BID: mid - offset (below mid), ASK: mid + offset (above mid)
          rawOff:`long$1+floor SPREAD_TICKS*uRandOff[i];
          newTk:$[isBid; midTk-rawOff; midTk+rawOff];
          evtTicks[i]:newTk;
          / Add to live set only if not already present
          $[newTk in liveSet;
            0b;     / already live: treat as update (no structural change)
            $[isBid;
              bidLive,:enlist newTk;
              askLive,:enlist newTk
            ]
          ]
        ];
        [
          / Update existing level: pick random index from live set
          pickIdx:rand nLive;
          evtTicks[i]:liveSet[pickIdx]
        ]
      ];
      evtQtys[i]:first genQty 1
    ];
    [
      / DELETE branch
      evtTypes[i]:`DELETE;
      / Pick a random level to delete
      pickIdx:rand nLive;
      delTk:liveSet[pickIdx];
      evtTicks[i]:delTk;
      evtQtys[i]:0j;
      / Remove from live set
      remaining:liveSet except enlist delTk;
      $[isBid;
        bidLive:remaining;
        askLive:remaining
      ]
    ]
  ];
  i+:1
 ]

/ -------------------------------------------------------------------
/ Validation
/ -------------------------------------------------------------------

/ 1. All UPSERT rows must have qty > 0
isUpsert:evtTypes=`UPSERT
badUpserts:sum isUpsert and evtQtys=0j
$[0<badUpserts;
  -2 "WARNING: ",( string badUpserts)," UPSERT rows have qty=0";
  -1 "Validation passed: all UPSERT rows have qty > 0"
 ]

/ 2. All DELETE rows must have qty = 0
isDelete:evtTypes=`DELETE
badDeletes:sum isDelete and evtQtys<>0j
$[0<badDeletes;
  -2 "WARNING: ",( string badDeletes)," DELETE rows have qty <> 0";
  -1 "Validation passed: all DELETE rows have qty = 0"
 ]

/ 3. All ticks within plausible range: MU*100 +/- 50% = [150000, 450000]
tickLo:`long$150000
tickHi:`long$450000
outOfRange:evtTicks where not evtTicks within (tickLo;tickHi)
$[0<count outOfRange;
  -2 "WARNING: ",( string count outOfRange)," ticks out of plausible range [",( string tickLo),",",( string tickHi),"]: min=",( string min outOfRange)," max=",( string max outOfRange);
  -1 "Tick range validation passed: all ticks within [",( string tickLo),",",( string tickHi),"]"
 ]

/ 4. Optional: check no duplicates in live sets at end of run
dupBid:count[bidLive]-count distinct bidLive
dupAsk:count[askLive]-count distinct askLive
$[(0<dupBid) or 0<dupAsk;
  -2 "WARNING: duplicate ticks in live sets at end of run (bid=",( string dupBid)," ask=",( string dupAsk),")";
  -1 "Live set sanity check passed: no duplicate ticks"
 ]

/ -------------------------------------------------------------------
/ Write CSV
/ Tick column is long; string converts to plain integer (e.g. "300123").
/ qty column is long; DELETE rows write "0".
/ No float formatting needed.
/ outPath is built from OUT_FILE so the -output flag controls it.
/ -------------------------------------------------------------------

hdr:"event_type,side,tick,qty"

colEvtType:string evtTypes
colSide   :string evtSides
colTick   :string evtTicks
colQty    :string evtQtys

rows:colEvtType,'",",'colSide,'",",'colTick,'",",'colQty

outPath:`$":",OUT_FILE
outPath 0: enlist[hdr],rows

/ -------------------------------------------------------------------
/ Summary statistics
/ -------------------------------------------------------------------

totalRows:count rows
upsertCnt:sum evtTypes=`UPSERT
deleteCnt:sum evtTypes=`DELETE
delFrac:deleteCnt%totalRows

tkMin :min evtTicks
tkMax :max evtTicks
tkMean:avg evtTicks

/ Observed OU std from mid-prices (in dollars)
ouObsStd:dev midPrices

-1 "";
-1 "=== ETH/USDT L2 MBP Event Stream Generator Summary ===";
-1 "Parameters used:";
-1 "  theta              : ",.Q.f[6] THETA;
-1 "  sigma              : ",.Q.f[4] SIGMA;
-1 "  drift              : ",.Q.f[4] DRIFT;
-1 "  seed               : ",string SEED;
-1 "  upsert_prob        : ",.Q.f[4] UPSERT_PROB;
-1 "  spread_ticks       : ",string SPREAD_TICKS;
-1 "  max_live           : ",string MAX_LIVE;
-1 "  total_events       : ",string TOTAL_EVENTS;
-1 "Total rows written   : ",string totalRows;
-1 "  UPSERT events      : ",string upsertCnt;
-1 "  DELETE events      : ",string deleteCnt;
-1 "Delete fraction      : ",.Q.f[4] delFrac;
-1 "Tick min             : ",string tkMin;
-1 "Tick max             : ",string tkMax;
-1 "Tick mean            : ",.Q.f[2] tkMean;
-1 "OU stat std (theory) : ",(.Q.f[2] STAT_STD)," dollars / ",(string`long$STAT_STD%TICK_SIZE)," ticks";
-1 "OU stat std (observed): ",(.Q.f[2] ouObsStd)," dollars";
-1 "Output               : ",OUT_FILE;
-1 "";

exit 0
