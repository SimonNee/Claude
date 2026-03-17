#include "orderbook.h"

// Explicit instantiation — forces the compiler to emit all method definitions
// in this translation unit. tests.cpp and main.cpp link against <100,20>.
// bench.cpp also uses the wider configurations added in Iteration 14.
template class OrderBookT<100,   20>;
template class OrderBookT<200,   20>;
template class OrderBookT<500,   20>;
template class OrderBookT<500,  100>;
template class OrderBookT<1000,  20>;
template class OrderBookT<2000,  20>;
template class OrderBookT<3000,  20>;
template class OrderBookT<4000,  20>;
