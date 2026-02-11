/ Tick Data Analytics Engine
/ Iteration 1: Foundation - basic table schema

/ Define the trade table schema
trade:([]
  time:`timestamp$();
  sym:`symbol$();
  price:`float$();
  size:`long$()
  )

/ Insert a single hardcoded trade for testing
`trade insert (.z.p; `AAPL; 150.25; 100)

/ Display the trade table
show trade

/ Example queries to verify structure
show "Trade count: ", string count trade
show "Schema: "
show meta trade
