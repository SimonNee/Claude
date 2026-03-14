#include "orderbook.h"

#include <algorithm>

int OrderBook::addOrder(Side side, double price, double quantity) {
    Order order{nextId++, price, quantity, side};

    if (side == Side::Buy) {
        matchBuy(order);
        if (order.quantity > 0.0)
            bids[price].push_back(order);
    } else {
        matchSell(order);
        if (order.quantity > 0.0)
            asks[price].push_back(order);
    }

    return order.id;
}

void OrderBook::matchBuy(Order& order) {
    // Walk asks lowest-first; stop when no more crossable levels
    for (auto it = asks.begin(); it != asks.end() && order.quantity > 0.0; ) {
        if (it->first > order.price)
            break;

        auto& level = it->second;
        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            double fill = std::min(order.quantity, resting.quantity);
            order.quantity   -= fill;
            resting.quantity -= fill;
            if (resting.quantity == 0.0)
                level.pop_front();
        }

        it = level.empty() ? asks.erase(it) : std::next(it);
    }
}

void OrderBook::matchSell(Order& order) {
    // Walk bids highest-first; stop when no more crossable levels
    for (auto it = bids.begin(); it != bids.end() && order.quantity > 0.0; ) {
        if (it->first < order.price)
            break;

        auto& level = it->second;
        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            double fill = std::min(order.quantity, resting.quantity);
            order.quantity   -= fill;
            resting.quantity -= fill;
            if (resting.quantity == 0.0)
                level.pop_front();
        }

        it = level.empty() ? bids.erase(it) : std::next(it);
    }
}

bool OrderBook::cancelOrder(int id) {
    for (auto& [price, level] : bids) {
        for (auto it = level.begin(); it != level.end(); ++it) {
            if (it->id == id) {
                level.erase(it);
                if (level.empty()) bids.erase(price);
                return true;
            }
        }
    }
    for (auto& [price, level] : asks) {
        for (auto it = level.begin(); it != level.end(); ++it) {
            if (it->id == id) {
                level.erase(it);
                if (level.empty()) asks.erase(price);
                return true;
            }
        }
    }
    return false;
}

std::optional<double> OrderBook::getBestBid() const {
    if (bids.empty()) return std::nullopt;
    return bids.begin()->first;
}

std::optional<double> OrderBook::getBestAsk() const {
    if (asks.empty()) return std::nullopt;
    return asks.begin()->first;
}

std::optional<double> OrderBook::getSpread() const {
    auto bid = getBestBid();
    auto ask = getBestAsk();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}
