/ Trade Data Generator
/ Load after tick.q to generate synthetic trade data

/ Generate N synthetic trades with randomized data
/ Returns a table matching the trade schema
genTrades:{[n]
  syms:`AAPL`MSFT`GOOG`AMZN`TSLA;           / Fixed symbol list
  times:.z.p+sums n?100;                       / Sequential timestamps, 0-99ns apart
  symbols:n?syms;                            / Random symbols with replacement
  prices:0.01*floor 0.5+100*50.0+n?450.0;   / Random prices 50-500, 2 decimals
  sizes:100*1+n?100;                         / Round lots 100-10000
  ([]time:times;sym:symbols;price:prices;size:sizes)
  }

/ Usage examples:
/ t:genTrades[10]          / Generate 10 trades
/ `trade insert genTrades[100]  / Insert 100 trades into trade table
/ show genTrades[5]        / Display 5 sample trades
