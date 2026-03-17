#pragma once

// Template method definitions for OrderBookT<N_TICKS, TICKS_PER_UNIT>.
// Included at the bottom of orderbook.h — not intended to be included directly.

#include <algorithm>

// ---------------------------------------------------------------------------
// Bitmap helpers
// ---------------------------------------------------------------------------
// bits[tick >> 6] is the 64-bit word; (1ULL << (tick & 63)) is the bit mask.
// setBit/clearBit take a raw pointer so they work for any array width.

static inline void setBit(uint64_t* bits, int tick) {
    bits[tick >> 6] |= (1ULL << (tick & 63));
}

static inline void clearBit(uint64_t* bits, int tick) {
    bits[tick >> 6] &= ~(1ULL << (tick & 63));
}

// Lowest set bit — best ask (lowest price == lowest tick).
// GCC unrolls the loop for small NWORDS (≤8); for NWORDS=16 it emits a runtime
// loop. Either way correctness is unaffected; the loop exits on first non-zero word.
template<int NWORDS>
static inline int lowestBit(const uint64_t* bits) {
#pragma GCC unroll 64
    for (int w = 0; w < NWORDS; ++w)
        if (bits[w]) return w * 64 + __builtin_ctzll(bits[w]);
    return -1;
}

// Highest set bit — best bid (highest price == highest tick).
template<int NWORDS>
static inline int highestBit(const uint64_t* bits) {
#pragma GCC unroll 64
    for (int w = NWORDS - 1; w >= 0; --w)
        if (bits[w]) return w * 64 + 63 - __builtin_clzll(bits[w]);
    return -1;
}

// ---------------------------------------------------------------------------
// OrderBookT method definitions
// ---------------------------------------------------------------------------

template<int N_TICKS, int TICKS_PER_UNIT>
int OrderBookT<N_TICKS, TICKS_PER_UNIT>::addOrder(Side side, double price, double quantity) {
    Order order{quantity, nextId++, side};

    int tick = priceToTick(price);

    if (side == Side::Buy) {
        matchBuy(order, tick);
        if (order.quantity > 0.0) {
            if (bid_levels[tick].empty()) setBit(bid_bits, tick);
            std::size_t idx = bid_levels[tick].orders.size();
            bid_levels[tick].push_back(order);
            if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1, kEmptyLocation);
            orderIndex[order.id] = {Side::Buy, tick, idx};
        }
    } else {
        matchSell(order, tick);
        if (order.quantity > 0.0) {
            if (ask_levels[tick].empty()) setBit(ask_bits, tick);
            std::size_t idx = ask_levels[tick].orders.size();
            ask_levels[tick].push_back(order);
            if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1, kEmptyLocation);
            orderIndex[order.id] = {Side::Sell, tick, idx};
        }
    }

    return order.id;
}

template<int N_TICKS, int TICKS_PER_UNIT>
void OrderBookT<N_TICKS, TICKS_PER_UNIT>::matchBuy(Order& order, int orderTick) {
    // Walk asks lowest-first via bitmap; clear the bit inline when a level empties.
    // Tombstones (id==0) are skipped — cancel_at zeroes id but not quantity.
    while (order.quantity > 0.0) {
        int askTick = lowestBit<N_BITMAP_WORDS>(ask_bits);
        if (askTick < 0 || askTick > orderTick) break;

        PriceLevel& level = ask_levels[askTick];
        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            if (resting.id == 0) { level.pop_front(); continue; }

            // Fill arithmetic — single load of resting.quantity.
            // xmm_fill receives resting.qty and becomes fill after vminsd.
            // xmm_rest holds the copy so resting.qty can be updated without a second load.
            //
            // resting_qty_out: fourth output exposes the post-update value in a register.
            // The final vmovapd copies xmm_rest into the resting_qty_out register so the
            // compiler has a live xmm holding the new resting.quantity after the block exits.
            // The if-check below uses resting_qty_out, eliminating the third memory load
            // that +m would otherwise force (compiler reloads from (%rax) for vucomisd).
            double xmm_fill, xmm_rest, resting_qty_out;
            __asm__ volatile (
                "vmovsd %[resting_qty], %[xmm_fill]\n\t"
                "vmovsd %[resting_qty], %[xmm_rest]\n\t"
                "vminsd %[order_qty], %[xmm_fill], %[xmm_fill]\n\t"
                "vsubsd %[xmm_fill], %[order_qty], %[order_qty]\n\t"
                "vsubsd %[xmm_fill], %[xmm_rest], %[xmm_rest]\n\t"
                "vmovsd %[xmm_rest], %[resting_qty]\n\t"
                "vmovapd %[xmm_rest], %[resting_qty_out]\n\t"
                : [order_qty]      "+x" (order.quantity),
                  [resting_qty]    "+m" (resting.quantity),
                  [xmm_fill]       "=&x"(xmm_fill),
                  [xmm_rest]       "=&x"(xmm_rest),
                  [resting_qty_out] "=x"(resting_qty_out)
                : :
            );

            if (resting_qty_out == 0.0) {
                orderIndex[resting.id] = kEmptyLocation;
                level.pop_front();
            }
        }
        if (level.empty()) clearBit(ask_bits, askTick);
    }
}

template<int N_TICKS, int TICKS_PER_UNIT>
void OrderBookT<N_TICKS, TICKS_PER_UNIT>::matchSell(Order& order, int orderTick) {
    // Walk bids highest-first via bitmap; clear the bit inline when a level empties.
    // Tombstones (id==0) are skipped — cancel_at zeroes id but not quantity.
    while (order.quantity > 0.0) {
        int bidTick = highestBit<N_BITMAP_WORDS>(bid_bits);
        if (bidTick < 0 || bidTick < orderTick) break;

        PriceLevel& level = bid_levels[bidTick];
        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            if (resting.id == 0) { level.pop_front(); continue; }

            // Fill arithmetic — single load of resting.quantity. See matchBuy.
            // resting_qty_out: same fix as matchBuy — exposes post-update xmm_rest
            // in a live register so the zero-check does not reload from memory.
            double xmm_fill, xmm_rest, resting_qty_out;
            __asm__ volatile (
                "vmovsd %[resting_qty], %[xmm_fill]\n\t"
                "vmovsd %[resting_qty], %[xmm_rest]\n\t"
                "vminsd %[order_qty], %[xmm_fill], %[xmm_fill]\n\t"
                "vsubsd %[xmm_fill], %[order_qty], %[order_qty]\n\t"
                "vsubsd %[xmm_fill], %[xmm_rest], %[xmm_rest]\n\t"
                "vmovsd %[xmm_rest], %[resting_qty]\n\t"
                "vmovapd %[xmm_rest], %[resting_qty_out]\n\t"
                : [order_qty]      "+x" (order.quantity),
                  [resting_qty]    "+m" (resting.quantity),
                  [xmm_fill]       "=&x"(xmm_fill),
                  [xmm_rest]       "=&x"(xmm_rest),
                  [resting_qty_out] "=x"(resting_qty_out)
                : :
            );

            if (resting_qty_out == 0.0) {
                orderIndex[resting.id] = kEmptyLocation;
                level.pop_front();
            }
        }
        if (level.empty()) clearBit(bid_bits, bidTick);
    }
}

template<int N_TICKS, int TICKS_PER_UNIT>
bool OrderBookT<N_TICKS, TICKS_PER_UNIT>::cancelOrder(int id) {
    if (id <= 0 || id >= (int)orderIndex.size() || orderIndex[id].levelTick == -1) return false;

    auto [side, levelTick, orderIdx] = orderIndex[id];
    PriceLevel& level = (side == Side::Buy) ? bid_levels[levelTick] : ask_levels[levelTick];

    level.cancel_at(orderIdx);
    if (level.empty()) {
        // Clear the bit on whichever side this order rested.
        uint64_t* bits = (side == Side::Buy) ? bid_bits : ask_bits;
        clearBit(bits, levelTick);
    }
    orderIndex[id] = kEmptyLocation;
    return true;
}

// getBestBid, getBestAsk, getSpread are defined inline in the class body
// (orderbook.h) so GCC inlines them without LTO heuristic interference.
