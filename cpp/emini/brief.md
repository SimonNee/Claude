# E-Mini S&P 500 Limit Order Book — Project Brief

Explore the design space for a limit order book for the CME E-mini S&P 500 futures
contract, to be implemented in both C and C++. The design goal is to drive the
complexity of every operation toward O(1) as an ideal — treat any deviation as a
cost to be justified. High throughput is a first-class requirement. The two
implementations will be benchmarked head-to-head.
