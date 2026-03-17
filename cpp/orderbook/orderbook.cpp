#include "orderbook.h"

#include <algorithm>

// Bitmap helpers.
// bits[tick >> 6] is the 64-bit word; (1ULL << (tick & 63)) is the bit mask.
// These operate on the two-word array passed by pointer (decays from uint64_t[2]).

static inline void setBit(uint64_t bits[2], int tick) {
    bits[tick >> 6] |= (1ULL << (tick & 63));
}

static inline void clearBit(uint64_t bits[2], int tick) {
    bits[tick >> 6] &= ~(1ULL << (tick & 63));
}

// Lowest set bit — best ask (lowest price == lowest tick).
static inline int lowestBit(const uint64_t bits[2]) {
    if (bits[0]) return __builtin_ctzll(bits[0]);
    if (bits[1]) return 64 + __builtin_ctzll(bits[1]);
    return -1;
}

// Highest set bit — best bid (highest price == highest tick).
static inline int highestBit(const uint64_t bits[2]) {
    if (bits[1]) return 64 + 63 - __builtin_clzll(bits[1]);
    if (bits[0]) return 63 - __builtin_clzll(bits[0]);
    return -1;
}

int OrderBook::addOrder(Side side, double price, double quantity) {
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

void OrderBook::matchBuy(Order& order, int orderTick) {
    // Walk asks lowest-first via bitmap; clear the bit inline when a level empties.
    // Tombstones (id==0) are skipped — cancel_at zeroes id but not quantity.
    while (order.quantity > 0.0) {
        int askTick = lowestBit(ask_bits);
        if (askTick < 0 || askTick > orderTick) break;

        PriceLevel& level = ask_levels[askTick];
        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            if (resting.id == 0) { level.pop_front(); continue; }

            // Fill arithmetic — single load of resting.quantity.
            // xmm_fill receives resting.qty and becomes fill after vminsd.
            // xmm_rest holds the copy so resting.qty can be updated without a second load.
            double xmm_fill, xmm_rest;
            __asm__ volatile (
                "vmovsd %[resting_qty], %[xmm_fill]\n\t"
                "vmovsd %[resting_qty], %[xmm_rest]\n\t"
                "vminsd %[order_qty], %[xmm_fill], %[xmm_fill]\n\t"
                "vsubsd %[xmm_fill], %[order_qty], %[order_qty]\n\t"
                "vsubsd %[xmm_fill], %[xmm_rest], %[xmm_rest]\n\t"
                "vmovsd %[xmm_rest], %[resting_qty]\n\t"
                : [order_qty]   "+x" (order.quantity),
                  [resting_qty] "+m" (resting.quantity),
                  [xmm_fill]    "=&x"(xmm_fill),
                  [xmm_rest]    "=&x"(xmm_rest)
                : :
            );

            if (resting.quantity == 0.0) {
                orderIndex[resting.id] = kEmptyLocation;
                level.pop_front();
            }
        }
        if (level.empty()) clearBit(ask_bits, askTick);
    }
}

void OrderBook::matchSell(Order& order, int orderTick) {
    // Walk bids highest-first via bitmap; clear the bit inline when a level empties.
    // Tombstones (id==0) are skipped — cancel_at zeroes id but not quantity.
    while (order.quantity > 0.0) {
        int bidTick = highestBit(bid_bits);
        if (bidTick < 0 || bidTick < orderTick) break;

        PriceLevel& level = bid_levels[bidTick];
        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            if (resting.id == 0) { level.pop_front(); continue; }

            // Fill arithmetic — single load of resting.quantity. See matchBuy.
            double xmm_fill, xmm_rest;
            __asm__ volatile (
                "vmovsd %[resting_qty], %[xmm_fill]\n\t"
                "vmovsd %[resting_qty], %[xmm_rest]\n\t"
                "vminsd %[order_qty], %[xmm_fill], %[xmm_fill]\n\t"
                "vsubsd %[xmm_fill], %[order_qty], %[order_qty]\n\t"
                "vsubsd %[xmm_fill], %[xmm_rest], %[xmm_rest]\n\t"
                "vmovsd %[xmm_rest], %[resting_qty]\n\t"
                : [order_qty]   "+x" (order.quantity),
                  [resting_qty] "+m" (resting.quantity),
                  [xmm_fill]    "=&x"(xmm_fill),
                  [xmm_rest]    "=&x"(xmm_rest)
                : :
            );

            if (resting.quantity == 0.0) {
                orderIndex[resting.id] = kEmptyLocation;
                level.pop_front();
            }
        }
        if (level.empty()) clearBit(bid_bits, bidTick);
    }
}

bool OrderBook::cancelOrder(int id) {
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

std::optional<double> OrderBook::getBestBid() const {
    int tick = highestBit(bid_bits);
    if (tick < 0) return std::nullopt;
    return tickToPrice(tick);
}

std::optional<double> OrderBook::getBestAsk() const {
    int tick = lowestBit(ask_bits);
    if (tick < 0) return std::nullopt;
    return tickToPrice(tick);
}

std::optional<double> OrderBook::getSpread() const {
    auto bid = getBestBid();
    auto ask = getBestAsk();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}

