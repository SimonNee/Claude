/ VWAP (Volume-Weighted Average Price) Functions
/ Load after tick.q: \l tick.q then \l vwap.q

/ ========================================
/ VWAP FUNCTIONS
/ ========================================

/ Calculate VWAP for a table of trades
/ Formula: VWAP = sum(price * size) / sum(size)
/ Uses idiomatic q 'wavg' operator: size wavg price
/ Returns: float (or 0n if empty/zero volume)
/ Usage: vwap[trade] or vwap[select from trade where sym=`AAPL]
vwap:{[trades] trades[`size] wavg trades`price}

/ Calculate VWAP grouped by symbol
/ Returns: keyed table with sym and vwap columns
/ Usage: vwapBySym[trade]
vwapBySym:{[trades]
  select vwap:size wavg price by sym from trades
 }

/ ========================================
/ USAGE EXAMPLES
/ ========================================

/ Overall VWAP:
/   vwap[trade]
/
/ VWAP by symbol:
/   vwapBySym[trade]
/
/ VWAP for specific symbol:
/   vwap[select from trade where sym=`AAPL]
/
/ VWAP with time range:
/   vwap[getTrades[`AAPL; startTime; endTime]]
/
/ Time-bucketed VWAP (direct select, no wrapper needed):
/   select vwap:size wavg price by sym, 5 xbar time.minute from trade
