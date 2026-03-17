#include "orderbook.h"

// Explicit instantiation — forces the compiler to emit all method definitions
// for OrderBookT<100,20> in this translation unit. This speeds up compilation
// of tests.cpp and bench.cpp because they can link against the pre-compiled
// object rather than re-instantiating the template themselves.
template class OrderBookT<100, 20>;
