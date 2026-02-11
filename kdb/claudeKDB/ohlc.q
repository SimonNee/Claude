/ OHLC (Open/High/Low/Close) Bar Aggregation Functions
/ Load after tick.q: \l tick.q then \l ohlc.q

/ ========================================
/ OHLC FUNCTIONS
/ ========================================

/ Calculate OHLC bars grouped by symbol and time bucket
/ barMins: bar width in minutes (e.g. 1, 5, 15, 30, 60)
/ bar column type is minute ("u") from time.minute accessor
/ cnt uses count i (idiomatic row count in grouped select)
/ Returns: keyed table (type 99h) keyed on sym,bar
/ Usage: ohlc[trade; 5]
ohlc:{[trades;barMins]
  select open:first price, high:max price, low:min price, close:last price,
         volume:sum size, cnt:count i
  by sym, bar:barMins xbar time.minute from trades
 }

/ Calculate OHLC bars across all symbols (no sym grouping)
/ Key: bar only; aggregates all syms into a single price series
/ Returns: keyed table (type 99h) keyed on bar
/ Usage: ohlcAll[trade; 5]
ohlcAll:{[trades;barMins]
  select open:first price, high:max price, low:min price, close:last price,
         volume:sum size, cnt:count i
  by bar:barMins xbar time.minute from trades
 }

/ ========================================
/ USAGE EXAMPLES
/ ========================================

/ 5-minute OHLC by symbol:
/   ohlc[trade; 5]
/
/ 1-minute OHLC by symbol:
/   ohlc[trade; 1]
/
/ 15-minute OHLC across all symbols:
/   ohlcAll[trade; 15]
/
/ OHLC for a single symbol:
/   ohlc[select from trade where sym=`AAPL; 5]
/
/ OHLC with time range filter:
/   ohlc[select from trade where sym=`AAPL, time within(startTime;endTime); 5]
