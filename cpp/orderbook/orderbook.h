#pragma once

#include <cstddef>
#include <optional>
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
// head advances on each fill (logical pop_front, no shifting).
// Orders before head are consumed; [head, orders.size()) are live.
struct PriceLevel {
    double             price;
    std::vector<Order> orders;
    std::size_t        head = 0;

    bool         empty()     const { return head >= orders.size(); }
    std::size_t  liveCount() const { return orders.size() - head; }
    Order&       front()           { return orders[head]; }
    const Order& front()     const { return orders[head]; }
    void         pop_front()       { ++head; }
    void         push_back(const Order& o) { orders.push_back(o); }
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

    void matchBuy(Order& order);
    void matchSell(Order& order);
};
