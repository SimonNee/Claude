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
                std::size_t idx = it->orders.size();
                it->push_back(order);
                if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
                orderIndex[order.id] = {Side::Buy, price, idx};
            } else {
                PriceLevel level;
                level.price = price;
                level.push_back(order);
                bids.insert(it, std::move(level));
                if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
                orderIndex[order.id] = {Side::Buy, price, 0};
            }
        }
    } else {
        matchSell(order);
        if (order.quantity > 0.0) {
            auto it = findAskLevel(asks, price);
            if (it != asks.end() && it->price == price) {
                std::size_t idx = it->orders.size();
                it->push_back(order);
                if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
                orderIndex[order.id] = {Side::Sell, price, idx};
            } else {
                PriceLevel level;
                level.price = price;
                level.push_back(order);
                asks.insert(it, std::move(level));
                if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
                orderIndex[order.id] = {Side::Sell, price, 0};
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
            if (resting.quantity == 0.0) {
                orderIndex[resting.id] = std::nullopt;
                level.pop_front();
            }
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
            if (resting.quantity == 0.0) {
                orderIndex[resting.id] = std::nullopt;
                level.pop_front();
            }
        }
        if (level.empty()) drained = true;
    }
    if (drained)
        bids.erase(std::remove_if(bids.begin(), bids.end(),
            [](const PriceLevel& pl) { return pl.empty(); }), bids.end());
}

bool OrderBook::cancelOrder(int id) {
    if (id <= 0 || id >= (int)orderIndex.size() || !orderIndex[id]) return false;

    auto [side, levelPrice, orderIdx] = *orderIndex[id];
    auto& levels = (side == Side::Buy) ? bids : asks;

    auto levelIt = (side == Side::Buy)
        ? findBidLevel(levels, levelPrice)
        : findAskLevel(levels, levelPrice);

    if (levelIt == levels.end() || levelIt->price != levelPrice) {
        orderIndex[id] = std::nullopt;
        return false;
    }

    levelIt->cancel_at(orderIdx);   // O(1) direct index — no scan, no shift
    if (levelIt->empty()) levels.erase(levelIt);
    orderIndex[id] = std::nullopt;
    return true;
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

// ---------------------------------------------------------------------------
// ASM variants — C++ baseline. Bodies replaced with inline ASM by agentASM.
// ---------------------------------------------------------------------------

int OrderBook::addOrder_asm(Side side, double price, double quantity) {
    Order order{price, quantity, nextId++, side};

    if (side == Side::Buy) {
        matchBuy_asm(order);
        if (order.quantity > 0.0) {
            auto it = findBidLevel(bids, price);
            if (it != bids.end() && it->price == price) {
                std::size_t idx = it->orders.size();
                it->push_back(order);
                if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
                orderIndex[order.id] = {Side::Buy, price, idx};
            } else {
                PriceLevel level;
                level.price = price;
                level.push_back(order);
                bids.insert(it, std::move(level));
                if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
                orderIndex[order.id] = {Side::Buy, price, 0};
            }
        }
    } else {
        matchSell_asm(order);
        if (order.quantity > 0.0) {
            auto it = findAskLevel(asks, price);
            if (it != asks.end() && it->price == price) {
                std::size_t idx = it->orders.size();
                it->push_back(order);
                if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
                orderIndex[order.id] = {Side::Sell, price, idx};
            } else {
                PriceLevel level;
                level.price = price;
                level.push_back(order);
                asks.insert(it, std::move(level));
                if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
                orderIndex[order.id] = {Side::Sell, price, 0};
            }
        }
    }

    return order.id;
}

void OrderBook::matchBuy_asm(Order& order) {
    bool drained = false;
    for (auto& level : asks) {
        if (order.quantity <= 0.0 || level.price > order.price) break;

        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            double fill = std::min(order.quantity, resting.quantity);
            order.quantity   -= fill;
            resting.quantity -= fill;
            if (resting.quantity == 0.0) {
                orderIndex[resting.id] = std::nullopt;
                level.pop_front();
            }
        }
        if (level.empty()) drained = true;
    }
    if (drained)
        asks.erase(std::remove_if(asks.begin(), asks.end(),
            [](const PriceLevel& pl) { return pl.empty(); }), asks.end());
}

void OrderBook::matchSell_asm(Order& order) {
    bool drained = false;
    for (auto& level : bids) {
        if (order.quantity <= 0.0 || level.price < order.price) break;

        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();
            double fill = std::min(order.quantity, resting.quantity);
            order.quantity   -= fill;
            resting.quantity -= fill;
            if (resting.quantity == 0.0) {
                orderIndex[resting.id] = std::nullopt;
                level.pop_front();
            }
        }
        if (level.empty()) drained = true;
    }
    if (drained)
        bids.erase(std::remove_if(bids.begin(), bids.end(),
            [](const PriceLevel& pl) { return pl.empty(); }), bids.end());
}

std::optional<double> OrderBook::getSpread_asm() const {
    auto bid = getBestBid();
    auto ask = getBestAsk();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}
