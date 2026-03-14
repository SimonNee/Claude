#pragma once

#include <deque>
#include <map>
#include <optional>

enum class Side { Buy, Sell };

struct Order {
    int    id;
    double price;
    double quantity;
    Side   side;
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

private:
    int nextId = 1;

    // Bids: highest price first
    std::map<double, std::deque<Order>, std::greater<double>> bids;
    // Asks: lowest price first
    std::map<double, std::deque<Order>> asks;

    void matchBuy(Order& order);
    void matchSell(Order& order);
};
