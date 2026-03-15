#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

enum class Side { Buy, Sell };

// Members ordered largest-to-smallest to eliminate padding waste.
// sizeof(Order) == 24 (was 32 in Iteration 1).
struct Order {
    double price;
    double quantity;
    int    id;
    Side   side;
};

static_assert(sizeof(Order) == 24, "Order layout changed — check struct padding");

// A single price level: one price, a contiguous queue of resting orders.
// head advances past filled/cancelled orders (logical pop_front, no shifting).
// Cancelled orders are marked with id=0 (tombstone); the match loop skips them.
// liveOrders tracks non-cancelled, non-filled orders for O(1) empty() checks.
struct PriceLevel {
    double             price;
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

    // Instrumentation — active price level counts
    std::size_t bidLevels() const { return bids.size(); }
    std::size_t askLevels() const { return asks.size(); }

    // Instrumentation — append live order count per level into out
    void sampleLiveCounts(std::vector<std::size_t>& out) const {
        for (const auto& l : bids) out.push_back(l.liveCount());
        for (const auto& l : asks) out.push_back(l.liveCount());
    }

private:
    int nextId = 1;

    std::vector<PriceLevel> bids;  // sorted descending by price
    std::vector<PriceLevel> asks;  // sorted ascending by price

    struct OrderLocation {
        Side        side;
        double      levelPrice;
        std::size_t orderIdx;   // index into PriceLevel::orders — stable (no shifting)
    };

    // O(1) cancel lookup: id → exact location.
    // Inserted when an order rests; erased when filled or cancelled.
    std::unordered_map<int, OrderLocation> orderIndex;

    void matchBuy(Order& order);
    void matchSell(Order& order);
};
