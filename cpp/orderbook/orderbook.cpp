#include "orderbook.h"

#include <algorithm>

// Binary search helpers — return iterator to matching level or insertion point.

static auto findBidLevel(std::vector<PriceLevel>& bids, double price) {
    // bids is descending; find first element where price >= pl.price
    return std::lower_bound(bids.begin(), bids.end(), price,
        [](const PriceLevel& pl, double p) { return pl.price > p; });
}

static auto findAskLevel(std::vector<PriceLevel>& asks, double price) {
    // asks is ascending; find first element where price <= pl.price
    return std::lower_bound(asks.begin(), asks.end(), price,
        [](const PriceLevel& pl, double p) { return pl.price < p; });
}

int OrderBook::addOrder(Side side, double price, double quantity) {
    Order order{price, quantity, nextId++, side};

    if (side == Side::Buy) {
        matchBuy(order);
        if (order.quantity > 0.0) {
            auto it = findBidLevel(bids, price);
            if (it != bids.end() && it->price == price) {
                it->push_back(order);
            } else {
                PriceLevel level;
                level.price = price;
                level.push_back(order);
                bids.insert(it, std::move(level));
            }
        }
    } else {
        matchSell(order);
        if (order.quantity > 0.0) {
            auto it = findAskLevel(asks, price);
            if (it != asks.end() && it->price == price) {
                it->push_back(order);
            } else {
                PriceLevel level;
                level.price = price;
                level.push_back(order);
                asks.insert(it, std::move(level));
            }
        }
    }

    return order.id;
}

void OrderBook::matchBuy(Order& order) {
    // Walk asks lowest-first; stop when no more crossable levels.
    // Compaction is deferred to a single pass, but only runs when at least one
    // level was fully drained — avoids O(p) scan on the common non-crossing case.
    bool drained = false;
    for (auto& level : asks) {
        if (order.quantity <= 0.0 || level.price > order.price) break;

        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            double fill = std::min(order.quantity, resting.quantity);
            order.quantity   -= fill;
            resting.quantity -= fill;
            if (resting.quantity == 0.0) level.pop_front();
        }
        if (level.empty()) drained = true;
    }
    if (drained)
        asks.erase(std::remove_if(asks.begin(), asks.end(),
            [](const PriceLevel& pl) { return pl.empty(); }), asks.end());
}

void OrderBook::matchSell(Order& order) {
    // Walk bids highest-first; stop when no more crossable levels.
    // Same conditional deferred compaction as matchBuy.
    bool drained = false;
    for (auto& level : bids) {
        if (order.quantity <= 0.0 || level.price < order.price) break;

        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            double fill = std::min(order.quantity, resting.quantity);
            order.quantity   -= fill;
            resting.quantity -= fill;
            if (resting.quantity == 0.0) level.pop_front();
        }
        if (level.empty()) drained = true;
    }
    if (drained)
        bids.erase(std::remove_if(bids.begin(), bids.end(),
            [](const PriceLevel& pl) { return pl.empty(); }), bids.end());
}

bool OrderBook::cancelOrder(int id) {
    for (std::size_t li = 0; li < bids.size(); ++li) {
        auto& level = bids[li];
        for (std::size_t oi = level.head; oi < level.orders.size(); ++oi) {
            if (level.orders[oi].id == id) {
                level.orders.erase(level.orders.begin() + oi);
                if (level.empty()) bids.erase(bids.begin() + li);
                return true;
            }
        }
    }
    for (std::size_t li = 0; li < asks.size(); ++li) {
        auto& level = asks[li];
        for (std::size_t oi = level.head; oi < level.orders.size(); ++oi) {
            if (level.orders[oi].id == id) {
                level.orders.erase(level.orders.begin() + oi);
                if (level.empty()) asks.erase(asks.begin() + li);
                return true;
            }
        }
    }
    return false;
}

std::optional<double> OrderBook::getBestBid() const {
    if (bids.empty()) return std::nullopt;
    return bids.front().price;
}

std::optional<double> OrderBook::getBestAsk() const {
    if (asks.empty()) return std::nullopt;
    return asks.front().price;
}

std::optional<double> OrderBook::getSpread() const {
    auto bid = getBestBid();
    auto ask = getBestAsk();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}
