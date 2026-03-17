// bench_compare.cpp
//
// Side-by-side benchmark: Iteration 1 + O(1) cancel fix  vs  current OrderBookT<100,20>.
//
// Build:
//   g++ -std=c++17 -O2 -march=native -flto -o bench_compare bench_compare.cpp orderbook.cpp
//
// The Iter1 implementation is defined inline in this file — no changes to any existing source.

#include "orderbook.h"   // current OrderBookT

#include <algorithm>
#include <cmath>
#include <deque>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Iter1 implementation — std::map + std::deque + O(1) cancel
// ---------------------------------------------------------------------------

struct Iter1Order {
    int    id;
    double price;
    double quantity;
    Side   side;
};

class Iter1Book {
public:
    int addOrder(Side side, double price, double quantity) {
        Iter1Order order{nextId++, price, quantity, side};
        if (side == Side::Buy) {
            matchBuy(order);
            if (order.quantity > 0.0) {
                int idx = (int)bids[price].size();
                bids[price].push_back(order);
                orderIndex[order.id] = {Side::Buy, price, idx};
            }
        } else {
            matchSell(order);
            if (order.quantity > 0.0) {
                int idx = (int)asks[price].size();
                asks[price].push_back(order);
                orderIndex[order.id] = {Side::Sell, price, idx};
            }
        }
        return order.id;
    }

    bool cancelOrder(int id) {
        auto it = orderIndex.find(id);
        if (it == orderIndex.end()) return false;
        auto [side, price, idx] = it->second;
        auto& level = (side == Side::Buy) ? bids[price] : asks[price];
        level[idx].id = 0;   // tombstone
        orderIndex.erase(it);
        return true;
    }

    std::optional<double> getBestBid() const {
        if (bids.empty()) return std::nullopt;
        return bids.begin()->first;
    }
    std::optional<double> getBestAsk() const {
        if (asks.empty()) return std::nullopt;
        return asks.begin()->first;
    }
    std::optional<double> getSpread() const {
        auto bid = getBestBid();
        auto ask = getBestAsk();
        if (!bid || !ask) return std::nullopt;
        return *ask - *bid;
    }

private:
    int nextId = 1;

    std::map<double, std::deque<Iter1Order>, std::greater<double>> bids;
    std::map<double, std::deque<Iter1Order>>                       asks;

    struct OrderLocation { Side side; double price; int index; };
    std::unordered_map<int, OrderLocation> orderIndex;

    void matchBuy(Iter1Order& order) {
        for (auto it = asks.begin(); it != asks.end() && order.quantity > 0.0; ) {
            if (it->first > order.price) break;
            auto& level = it->second;
            while (!level.empty() && order.quantity > 0.0) {
                Iter1Order& resting = level.front();
                if (resting.id == 0) { level.pop_front(); continue; }
                double fill = std::min(order.quantity, resting.quantity);
                order.quantity   -= fill;
                resting.quantity -= fill;
                if (resting.quantity == 0.0) level.pop_front();
            }
            it = level.empty() ? asks.erase(it) : std::next(it);
        }
    }

    void matchSell(Iter1Order& order) {
        for (auto it = bids.begin(); it != bids.end() && order.quantity > 0.0; ) {
            if (it->first < order.price) break;
            auto& level = it->second;
            while (!level.empty() && order.quantity > 0.0) {
                Iter1Order& resting = level.front();
                if (resting.id == 0) { level.pop_front(); continue; }
                double fill = std::min(order.quantity, resting.quantity);
                order.quantity   -= fill;
                resting.quantity -= fill;
                if (resting.quantity == 0.0) level.pop_front();
            }
            it = level.empty() ? bids.erase(it) : std::next(it);
        }
    }
};

// ---------------------------------------------------------------------------
// Shared benchmark infrastructure
// ---------------------------------------------------------------------------

static uint64_t rdtscp() {
    uint32_t lo, hi, aux;
    __asm__ volatile ("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return ((uint64_t)hi << 32) | lo;
}

static void printRow(const char* label, uint64_t iter1_cycles, long iter1_n,
                                        uint64_t curr_cycles,  long curr_n) {
    long cpi = iter1_cycles / iter1_n;
    long cpc = curr_cycles  / curr_n;
    double ratio = (double)cpi / cpc;
    std::cout << std::left  << std::setw(30) << label
              << std::right << std::setw(10) << cpi
              << std::setw(10) << cpc
              << std::setw(10) << std::fixed << std::setprecision(1) << ratio << "x\n";
}

static std::vector<double> ouWalk(std::mt19937& rng, int n,
                                   double mid, double band, int tpu) {
    constexpr double STEP  = 0.05;
    constexpr double THETA = 0.05;
    std::normal_distribution<double> noise(0.0, STEP);
    std::vector<double> prices;
    prices.reserve(n);
    double price = mid;
    for (int i = 0; i < n; ++i) {
        price += THETA * (mid - price) + noise(rng);
        price  = std::round(price * tpu) / tpu;
        price  = std::max(mid - band, std::min(mid + band, price));
        prices.push_back(price);
    }
    return prices;
}

// seed both book types identically
static void seedIter1(Iter1Book& book, double mid, double tick) {
    for (int i = 1; i <= 40; ++i) book.addOrder(Side::Buy,  mid - i * tick, 10.0);
    for (int i = 1; i <= 40; ++i) book.addOrder(Side::Sell, mid + i * tick, 10.0);
}
static void seedCurr(OrderBook& book, double mid, double tick) {
    for (int i = 1; i <= 40; ++i) book.addOrder(Side::Buy,  mid - i * tick, 10.0);
    for (int i = 1; i <= 40; ++i) book.addOrder(Side::Sell, mid + i * tick, 10.0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    constexpr double BASE  = 97.50;
    constexpr double MID   = 100.0;
    constexpr double BAND  =   2.5;
    constexpr double TICK  =  0.05;
    constexpr int    TPU   =    20;
    constexpr double OFFSET = 10 * TICK;

    constexpr int N_STD   = 500000;
    constexpr int N_CROSS = 100000;
    constexpr int N_CHEAP = 1000000;

    std::cout << "\n=== Iter1+O(1)cancel  vs  OrderBookT<100,20> ===\n\n";
    std::cout << std::left  << std::setw(30) << "Benchmark"
              << std::right << std::setw(10) << "Iter1"
              << std::setw(10) << "Current"
              << std::setw(10) << "Speedup" << "\n";
    std::cout << std::string(60, '-') << "\n";

    // --- addOrder no-cross ---
    {
        std::mt19937 rng(42);
        auto prices = ouWalk(rng, N_STD + N_STD/10, MID, BAND, TPU);

        Iter1Book b1; seedIter1(b1, MID, TICK);
        for (int i = 0; i < N_STD/10; ++i) {
            double p = prices[i]; Side s = (p < MID) ? Side::Buy : Side::Sell;
            b1.addOrder(s, (s==Side::Buy) ? p-OFFSET : p+OFFSET, 1.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = N_STD/10; i < N_STD+N_STD/10; ++i) {
            double p = prices[i]; Side s = (p < MID) ? Side::Buy : Side::Sell;
            b1.addOrder(s, (s==Side::Buy) ? p-OFFSET : p+OFFSET, 1.0);
        }
        uint64_t t1 = rdtscp();
        uint64_t cy1 = t1 - t0;

        std::mt19937 rng2(42);
        auto prices2 = ouWalk(rng2, N_STD + N_STD/10, MID, BAND, TPU);
        OrderBook bc(BASE); seedCurr(bc, MID, TICK);
        for (int i = 0; i < N_STD/10; ++i) {
            double p = prices2[i]; Side s = (p < MID) ? Side::Buy : Side::Sell;
            bc.addOrder(s, (s==Side::Buy) ? p-OFFSET : p+OFFSET, 1.0);
        }
        t0 = rdtscp();
        for (int i = N_STD/10; i < N_STD+N_STD/10; ++i) {
            double p = prices2[i]; Side s = (p < MID) ? Side::Buy : Side::Sell;
            bc.addOrder(s, (s==Side::Buy) ? p-OFFSET : p+OFFSET, 1.0);
        }
        t1 = rdtscp();
        printRow("addOrder no-cross", cy1, N_STD, t1-t0, N_STD);
    }

    // --- addOrder crossing 1 level ---
    {
        const double cp = MID + TICK;
        Iter1Book b1;
        for (int i = 0; i < N_CROSS/10; ++i) {
            b1.addOrder(Side::Sell, cp, 1.0); b1.addOrder(Side::Buy, cp, 1.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = 0; i < N_CROSS; ++i) {
            b1.addOrder(Side::Sell, cp, 1.0); b1.addOrder(Side::Buy, cp, 1.0);
        }
        uint64_t t1 = rdtscp();
        uint64_t cy1 = t1 - t0;

        OrderBook bc(BASE);
        for (int i = 0; i < N_CROSS/10; ++i) {
            bc.addOrder(Side::Sell, cp, 1.0); bc.addOrder(Side::Buy, cp, 1.0);
        }
        t0 = rdtscp();
        for (int i = 0; i < N_CROSS; ++i) {
            bc.addOrder(Side::Sell, cp, 1.0); bc.addOrder(Side::Buy, cp, 1.0);
        }
        t1 = rdtscp();
        printRow("addOrder cross-1L", cy1, N_CROSS, t1-t0, N_CROSS);
    }

    // --- addOrder crossing 5 levels ---
    {
        Iter1Book b1;
        for (int i = 0; i < N_CROSS/10; ++i) {
            for (int l=1;l<=5;++l) b1.addOrder(Side::Sell, MID+l*TICK, 1.0);
            b1.addOrder(Side::Buy, MID+6*TICK, 5.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = 0; i < N_CROSS; ++i) {
            for (int l=1;l<=5;++l) b1.addOrder(Side::Sell, MID+l*TICK, 1.0);
            b1.addOrder(Side::Buy, MID+6*TICK, 5.0);
        }
        uint64_t t1 = rdtscp();
        uint64_t cy1 = t1 - t0;

        OrderBook bc(BASE);
        for (int i = 0; i < N_CROSS/10; ++i) {
            for (int l=1;l<=5;++l) bc.addOrder(Side::Sell, MID+l*TICK, 1.0);
            bc.addOrder(Side::Buy, MID+6*TICK, 5.0);
        }
        t0 = rdtscp();
        for (int i = 0; i < N_CROSS; ++i) {
            for (int l=1;l<=5;++l) bc.addOrder(Side::Sell, MID+l*TICK, 1.0);
            bc.addOrder(Side::Buy, MID+6*TICK, 5.0);
        }
        t1 = rdtscp();
        printRow("addOrder cross-5L", cy1, N_CROSS, t1-t0, N_CROSS);
    }

    // --- cancelOrder ---
    {
        std::mt19937 rng(44);
        auto prices = ouWalk(rng, N_STD + N_STD/10, MID, BAND, TPU);

        Iter1Book b1;
        std::vector<int> ids1; ids1.reserve(N_STD);
        for (int i = 0; i < N_STD; ++i) {
            double p = prices[i]; Side s = (p < MID) ? Side::Buy : Side::Sell;
            ids1.push_back(b1.addOrder(s, (s==Side::Buy)?p-OFFSET:p+OFFSET, 1.0));
        }
        int warmup = N_STD/10;
        int wi = 0;
        for (int i = 0; i < warmup; ++i) { b1.cancelOrder(ids1[wi++]); }
        for (int i = N_STD; i < N_STD+warmup; ++i) {
            double p = prices[i]; Side s = (p < MID) ? Side::Buy : Side::Sell;
            ids1.push_back(b1.addOrder(s, (s==Side::Buy)?p-OFFSET:p+OFFSET, 1.0));
        }
        uint64_t t0 = rdtscp();
        for (int i = 0; i < N_STD; ++i) { b1.cancelOrder(ids1[wi++]); }
        uint64_t t1 = rdtscp();
        uint64_t cy1 = t1 - t0;

        std::mt19937 rng2(44);
        auto prices2 = ouWalk(rng2, N_STD + N_STD/10, MID, BAND, TPU);
        OrderBook bc(BASE);
        std::vector<int> ids2; ids2.reserve(N_STD);
        for (int i = 0; i < N_STD; ++i) {
            double p = prices2[i]; Side s = (p < MID) ? Side::Buy : Side::Sell;
            ids2.push_back(bc.addOrder(s, (s==Side::Buy)?p-OFFSET:p+OFFSET, 1.0));
        }
        wi = 0;
        for (int i = 0; i < warmup; ++i) { bc.cancelOrder(ids2[wi++]); }
        for (int i = N_STD; i < N_STD+warmup; ++i) {
            double p = prices2[i]; Side s = (p < MID) ? Side::Buy : Side::Sell;
            ids2.push_back(bc.addOrder(s, (s==Side::Buy)?p-OFFSET:p+OFFSET, 1.0));
        }
        t0 = rdtscp();
        for (int i = 0; i < N_STD; ++i) { bc.cancelOrder(ids2[wi++]); }
        t1 = rdtscp();
        printRow("cancelOrder", cy1, N_STD, t1-t0, N_STD);
    }

    // --- getBestBid+Ask+Spread ---
    {
        Iter1Book b1; seedIter1(b1, MID, TICK);
        double sink = 0.0;
        for (int i = 0; i < N_CHEAP/10; ++i) {
            sink += b1.getBestBid().value_or(0.0);
            sink += b1.getBestAsk().value_or(0.0);
            sink += b1.getSpread().value_or(0.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = 0; i < N_CHEAP; ++i) {
            sink += b1.getBestBid().value_or(0.0);
            sink += b1.getBestAsk().value_or(0.0);
            sink += b1.getSpread().value_or(0.0);
        }
        uint64_t t1 = rdtscp();
        uint64_t cy1 = t1 - t0;

        OrderBook bc(BASE); seedCurr(bc, MID, TICK);
        for (int i = 0; i < N_CHEAP/10; ++i) {
            sink += bc.getBestBid().value_or(0.0);
            sink += bc.getBestAsk().value_or(0.0);
            sink += bc.getSpread().value_or(0.0);
        }
        t0 = rdtscp();
        for (int i = 0; i < N_CHEAP; ++i) {
            sink += bc.getBestBid().value_or(0.0);
            sink += bc.getBestAsk().value_or(0.0);
            sink += bc.getSpread().value_or(0.0);
        }
        t1 = rdtscp();
        printRow("getBestBid+Ask+Spread", cy1, N_CHEAP, t1-t0, N_CHEAP);
        if (sink == -1.0) std::cout << sink;
    }

    // --- mixed cancel=50% ---
    {
        auto runMixed = [&](double rate) -> std::pair<uint64_t,long> {
            std::mt19937 rng(45);
            auto prices = ouWalk(rng, N_STD + N_STD/10, MID, BAND, TPU);
            std::uniform_real_distribution<double> roll(0.0, 1.0);
            Iter1Book b1; seedIter1(b1, MID, TICK);
            std::deque<int> live;
            auto rp = [&](double p) -> std::pair<Side,double> {
                return (p <= MID) ? std::make_pair(Side::Buy, p-OFFSET)
                                  : std::make_pair(Side::Sell, p+OFFSET);
            };
            for (int i = 0; i < N_STD/10; ++i) {
                auto [s,p] = rp(prices[i]);
                live.push_back(b1.addOrder(s, p, 1.0));
                if (!live.empty() && roll(rng) < rate) { b1.cancelOrder(live.front()); live.pop_front(); }
            }
            uint64_t t0 = rdtscp(); long ops = 0;
            for (int i = N_STD/10; i < N_STD+N_STD/10; ++i) {
                auto [s,p] = rp(prices[i]);
                live.push_back(b1.addOrder(s, p, 1.0)); ++ops;
                if (!live.empty() && roll(rng) < rate) { b1.cancelOrder(live.front()); live.pop_front(); ++ops; }
            }
            uint64_t t1 = rdtscp();
            return {t1-t0, ops};
        };

        auto runMixedCurr = [&](double rate) -> std::pair<uint64_t,long> {
            std::mt19937 rng(45);
            auto prices = ouWalk(rng, N_STD + N_STD/10, MID, BAND, TPU);
            std::uniform_real_distribution<double> roll(0.0, 1.0);
            OrderBook bc(BASE); seedCurr(bc, MID, TICK);
            std::deque<int> live;
            auto rp = [&](double p) -> std::pair<Side,double> {
                return (p <= MID) ? std::make_pair(Side::Buy, p-OFFSET)
                                  : std::make_pair(Side::Sell, p+OFFSET);
            };
            for (int i = 0; i < N_STD/10; ++i) {
                auto [s,p] = rp(prices[i]);
                live.push_back(bc.addOrder(s, p, 1.0));
                if (!live.empty() && roll(rng) < rate) { bc.cancelOrder(live.front()); live.pop_front(); }
            }
            uint64_t t0 = rdtscp(); long ops = 0;
            for (int i = N_STD/10; i < N_STD+N_STD/10; ++i) {
                auto [s,p] = rp(prices[i]);
                live.push_back(bc.addOrder(s, p, 1.0)); ++ops;
                if (!live.empty() && roll(rng) < rate) { bc.cancelOrder(live.front()); live.pop_front(); ++ops; }
            }
            uint64_t t1 = rdtscp();
            return {t1-t0, ops};
        };

        auto [cy1, n1] = runMixed(0.50);
        auto [cy2, n2] = runMixedCurr(0.50);
        printRow("mixed cancel=50%", cy1, n1, cy2, n2);

        auto [cy3, n3] = runMixed(0.90);
        auto [cy4, n4] = runMixedCurr(0.90);
        printRow("mixed cancel=90%", cy3, n3, cy4, n4);
    }

    std::cout << "\n";
    return 0;
}
