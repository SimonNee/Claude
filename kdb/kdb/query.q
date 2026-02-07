/ Query Functions for Trade Data
/ Modular functions to slice trade data by time range and symbol
/ Load after tick.q: \l tick.q then \l query.q

/ ========================================
/ QUERY FUNCTIONS
/ ========================================

/ Filter trades by symbol(s)
/ Returns all trades for the given symbol(s), all times
/ Empty symbol list returns all trades
/ Usage: getTradesBySym[`AAPL] or getTradesBySym[`AAPL`MSFT]
getTradesBySym:{[syms]
  $[0=count syms;
    trade;
    select from trade where sym in syms]
 }

/ Filter trades by time range (inclusive)
/ Returns all trades within the time range, all symbols
/ Usage: getTradesByTime[startTime; endTime]
getTradesByTime:{[startTime;endTime]
  select from trade where time within (startTime;endTime)
 }

/ Combined filter: symbol(s) and time range
/ Empty symbol list means all symbols
/ Time range is inclusive on both bounds
/ Usage: getTrades[`AAPL; .z.p-1h; .z.p]
getTrades:{[syms;startTime;endTime]
  / First filter by time
  byTime:select from trade where time within (startTime;endTime);
  / Then filter by symbol if specified
  $[0=count syms;
    byTime;
    select from byTime where sym in syms]
 }

/ ========================================
/ EMPTY RESULT HELPERS
/ ========================================

/ Return empty table with trade schema
/ Useful for consistent return types
emptyTrade:{[] 0#trade}
