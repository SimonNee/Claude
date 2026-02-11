/ TWAP (Time-Weighted Average Price) Functions
/ Load after tick.q: \l tick.q then \l twap.q

/ ========================================
/ TWAP FUNCTIONS
/ ========================================

/ Calculate TWAP for a table of trades
/ Formula: TWAP = sum(price[i] * duration[i]) / sum(duration[i])
/ where duration[i] = time[i+1] - time[i]  (how long price[i] was "active")
/ .
/ Implementation uses deltas and wavg:
/   deltas trades`time  -> first element is the timestamp itself (not a diff)
/   1_                  -> drop that first element, leaving N-1 true durations
/   -1_ trades`price    -> drop last price (no subsequent trade, so duration=0)
/   `long$              -> convert timespan durations to nanoseconds for wavg
/ .
/ Returns: float (or 0n if empty or single-trade input)
/ Usage: twap[trade] or twap[select from trade where sym=`AAPL]
twap:{[trades] (`long$1_ deltas trades`time) wavg -1_ trades`price}

/ Calculate TWAP grouped by symbol
/ Returns: keyed table with sym and twap columns
/ Same duration-weighting logic applied per group via q's automatic group-by
/ Usage: twapBySym[trade]
twapBySym:{[trades]
  select twap:(`long$1_ deltas time) wavg -1_ price by sym from trades
 }

/ ========================================
/ USAGE EXAMPLES
/ ========================================

/ Overall TWAP:
/   twap[trade]
/ .
/ TWAP by symbol:
/   twapBySym[trade]
/ .
/ TWAP for specific symbol:
/   twap[select from trade where sym=`AAPL]
/ .
/ TWAP with time range:
/   twap[getTrades[`AAPL; startTime; endTime]]
/ .
/ Time-bucketed TWAP (direct select, no wrapper needed):
/   select twap:(`long$1_ deltas time) wavg -1_ price by sym, 5 xbar time.minute from trade
