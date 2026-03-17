#pragma once

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

enum class Side { Buy, Sell };

// Price grid constants — compile-time fixed.
// 100 ticks of 0.05 covers [97.50, 102.45].
constexpr int    N_TICKS    = 100;
constexpr double BASE_PRICE = 97.50;
constexpr double TICK_SIZE  = 0.05;

// Convert between double price and integer tick index.
// Tick 0 == BASE_PRICE; tick N == BASE_PRICE + N * TICK_SIZE.
static inline int priceToTick(double price) {
    return static_cast<int>((price - BASE_PRICE) * 20.0 + 0.5);
}
static inline double tickToPrice(int tick) {
    return BASE_PRICE + tick * TICK_SIZE;
}

// Members ordered largest-to-smallest to eliminate padding waste.
// sizeof(Order) == 24 (was 32 in Iteration 1).
struct Order {
    double price;
    double quantity;
    int    id;
    Side   side;
};

static_assert(sizeof(Order) == 24, "Order layout changed — check struct padding");

// A single price level: a contiguous queue of resting orders.
// The level's price is encoded by its slot index in bid_levels/ask_levels;
// the price field has been removed — use tickToPrice(tick) at the call site.
// head advances past filled/cancelled orders (logical pop_front, no shifting).
// Cancelled orders are marked with id=0 (tombstone); the match loop skips them.
// liveOrders tracks non-cancelled, non-filled orders for O(1) empty() checks.
struct PriceLevel {
    std::vector<Order> orders;
    std::size_t        head       = 0;
    std::size_t        liveOrders = 0;

    bool         empty()     const { return liveOrders == 0; }
    std::size_t  liveCount() const { return liveOrders; }
    Order&       front()           { return orders[head]; }
    const Order& front()     const { return orders[head]; }

    // Advance head. Only decrements liveOrders for live orders — tombstones
    // (id=0) were already decremented at cancel time.
    void pop_front() {
        if (orders[head].id != 0) --liveOrders;
        ++head;
    }

    void push_back(const Order& o) { ++liveOrders; orders.push_back(o); }

    // Mark order at oi as cancelled. O(1) — no shifting.
    void cancel_at(std::size_t oi) { orders[oi].id = 0; --liveOrders; }
};

static_assert(sizeof(PriceLevel) == 40, "PriceLevel layout changed");

class OrderBook {
public:
    // Insert a limit order. Matches immediately if crossing. Returns assigned id.
    int addOrder(Side side, double price, double quantity);

    // Remove an order by id. Returns true if found and removed.
    bool cancelOrder(int id);

    std::optional<double> getBestBid() const;
    std::optional<double> getBestAsk() const;

    // Returns ask - bid. nullopt if either side is empty.
    std::optional<double> getSpread() const;

    // Instrumentation — active price level counts (set bits in bitmap)
    std::size_t bidLevels() const {
        return __builtin_popcountll(bid_bits[0]) + __builtin_popcountll(bid_bits[1]);
    }
    std::size_t askLevels() const {
        return __builtin_popcountll(ask_bits[0]) + __builtin_popcountll(ask_bits[1]);
    }

    // Instrumentation — append live order count per level into out
    void sampleLiveCounts(std::vector<std::size_t>& out) const {
        for (int i = 0; i < N_TICKS; ++i)
            if (bid_levels[i].liveCount() > 0) out.push_back(bid_levels[i].liveCount());
        for (int i = 0; i < N_TICKS; ++i)
            if (ask_levels[i].liveCount() > 0) out.push_back(ask_levels[i].liveCount());
    }

private:
    int nextId = 1;

    // 128-bit bitmaps: bit i set means tick i has live orders on that side.
    // Fixed arrays of N_TICKS levels — slot index encodes price.
    uint64_t   bid_bits[2]        = {};
    uint64_t   ask_bits[2]        = {};
    PriceLevel bid_levels[N_TICKS];
    PriceLevel ask_levels[N_TICKS];

    struct OrderLocation {
        Side        side;
        int         levelTick;  // direct index into bid_levels / ask_levels
        std::size_t orderIdx;   // index into PriceLevel::orders — stable (no shifting)
    };

    // O(1) cancel lookup: id → exact location.
    // Direct-index vector: orderIndex[id] holds the location while the order rests.
    // Avoids std::unordered_map's divq (prime rehash policy) and per-node operator delete.
    // IDs are sequential from 1; vector grows as needed and slots are reset to nullopt on use.
    std::vector<std::optional<OrderLocation>> orderIndex;

    void matchBuy(Order& order, int orderTick);
    void matchSell(Order& order, int orderTick);
};
