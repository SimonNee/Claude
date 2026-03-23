#!/bin/bash
# Generate all three ETH/USDT L2 MBP orderbook workloads
cd "$(dirname "$0")"
q generate.q
q generate.q -sigma 10.0 -theta 0.002 -output events_volatile.csv
q generate.q -drift 0.50 -output events_trending.csv
