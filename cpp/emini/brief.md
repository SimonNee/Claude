# E-Mini S&P 500 Limit Order Book — Project Brief

Explore the design space for a limit order book for the CME E-mini S&P 500 futures
contract, implemented in C++ with a completed C reference implementation benchmarked
head-to-head. The design goal is to drive the complexity of every operation toward
O(1) as an ideal — treat any deviation as a cost to be justified. High throughput
is a first-class requirement.

The C implementation is complete and preserved at tag `version-0.3-C-remove`.
Active development continues on the C++ implementation only.
