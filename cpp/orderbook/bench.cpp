#include "orderbook.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

static uint64_t rdtscp() {
    uint32_t lo, hi, aux;
    __asm__ volatile ("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return ((uint64_t)hi << 32) | lo;
}

static void printResult(const char* name, long n, uint64_t cycles) {
    std::cout << std::left << std::setw(38) << name
              << " N=" << std::setw(8) << n
              << " cycles/op=" << cycles / n << "\n";
}

// OU price walk — same parameters as gen_orders.q.
// Returns N prices rounded to 2 d.p., bounded within [MID-BAND, MID+BAND].
static std::vector<double> ouWalk(std::mt19937& rng, int n) {
    constexpr double MID   = 100.0;
    constexpr double STEP  = 0.05;
    constexpr double THETA = 0.05;
    constexpr double BAND  = 2.50;

    std::normal_distribution<double> noise(0.0, STEP);
    std::vector<double> prices;
    prices.reserve(n);
    double price = MID;
    for (int i = 0; i < n; ++i) {
        price += THETA * (MID - price) + noise(rng);
        price = std::round(price * 20.0) / 20.0;  // snap to 0.05 grid
        price = std::max(MID - BAND, std::min(MID + BAND, price));
        prices.push_back(price);
    }
    return prices;
}

// Seed the book with ~40 bid levels and ~40 ask levels around MID.
// Spread is intentionally wide so benchmark adds don't cross.
static void seedBook(OrderBook& book) {
    for (int i = 1; i <= 40; ++i)
        book.addOrder(Side::Buy,  100.0 - i * 0.05, 10.0);
    for (int i = 1; i <= 40; ++i)
        book.addOrder(Side::Sell, 100.0 + i * 0.05, 10.0);
}

// --------------------------------------------------------------------------

static void benchAddNoCross(int n) {
    std::mt19937 rng(42);
    auto prices = ouWalk(rng, n + n / 10);

    OrderBook book;
    seedBook(book);

    // Warm-up: 10% of N, outside timing window
    for (int i = 0; i < n / 10; ++i) {
        double p = prices[i];
        Side side = (p < 100.0) ? Side::Buy : Side::Sell;
        // Push price away from mid so it never crosses
        p = (side == Side::Buy) ? p - 0.50 : p + 0.50;
        book.addOrder(side, p, 1.0);
    }

    uint64_t t0 = rdtscp();
    for (int i = n / 10; i < n + n / 10; ++i) {
        double p = prices[i];
        Side side = (p < 100.0) ? Side::Buy : Side::Sell;
        p = (side == Side::Buy) ? p - 0.50 : p + 0.50;
        book.addOrder(side, p, 1.0);
    }
    uint64_t t1 = rdtscp();

    printResult("addOrder no-cross", n, t1 - t0);
}

static void benchAddCross1Level(int n) {
    std::mt19937 rng(43);

    OrderBook book;

    // Warm-up
    for (int i = 0; i < n / 10; ++i) {
        book.addOrder(Side::Sell, 100.05, 1.0);
        book.addOrder(Side::Buy,  100.05, 1.0);
    }

    uint64_t t0 = rdtscp();
    for (int i = 0; i < n; ++i) {
        book.addOrder(Side::Sell, 100.05, 1.0);  // resting ask
        book.addOrder(Side::Buy,  100.05, 1.0);  // crossing buy — drains it
    }
    uint64_t t1 = rdtscp();

    // n rounds = 2n addOrder calls, but the crossing buy is the measured operation
    printResult("addOrder crossing 1 level", n, t1 - t0);
}

static void benchAddCross5Levels(int n) {
    OrderBook book;

    // Warm-up
    for (int i = 0; i < n / 10; ++i) {
        for (int l = 1; l <= 5; ++l)
            book.addOrder(Side::Sell, 100.0 + l * 0.05, 1.0);
        book.addOrder(Side::Buy, 100.30, 5.0);
    }

    uint64_t t0 = rdtscp();
    for (int i = 0; i < n; ++i) {
        for (int l = 1; l <= 5; ++l)
            book.addOrder(Side::Sell, 100.0 + l * 0.05, 1.0);
        book.addOrder(Side::Buy, 100.30, 5.0);  // crosses 5 ask levels
    }
    uint64_t t1 = rdtscp();

    printResult("addOrder crossing 5 levels", n, t1 - t0);
}

static void benchCancelOrder(int n) {
    OrderBook book;
    std::deque<int> ids;

    // Pre-populate book with n resting orders, record ids
    std::mt19937 rng(44);
    auto prices = ouWalk(rng, n + n / 10);
    for (int i = 0; i < n; ++i) {
        double p = prices[i];
        Side side = (p < 100.0) ? Side::Buy : Side::Sell;
        p = (side == Side::Buy) ? p - 0.50 : p + 0.50;
        ids.push_back(book.addOrder(side, p, 1.0));
    }

    // Warm-up: cancel 10% outside timing window (replenish first)
    int warmup = n / 10;
    for (int i = 0; i < warmup; ++i) {
        book.cancelOrder(ids.front());
        ids.pop_front();
    }
    // Replenish
    for (int i = n; i < n + warmup; ++i) {
        double p = prices[i];
        Side side = (p < 100.0) ? Side::Buy : Side::Sell;
        p = (side == Side::Buy) ? p - 0.50 : p + 0.50;
        ids.push_back(book.addOrder(side, p, 1.0));
    }

    uint64_t t0 = rdtscp();
    for (int i = 0; i < n; ++i) {
        book.cancelOrder(ids.front());
        ids.pop_front();
    }
    uint64_t t1 = rdtscp();

    printResult("cancelOrder", n, t1 - t0);
}

static void benchGetBestBidAskSpread(int n) {
    OrderBook book;
    seedBook(book);

    // Warm-up
    for (int i = 0; i < n / 10; ++i) {
        book.getBestBid();
        book.getBestAsk();
        book.getSpread();
    }

    uint64_t t0 = rdtscp();
    for (int i = 0; i < n; ++i) {
        book.getBestBid();
        book.getBestAsk();
        book.getSpread();
    }
    uint64_t t1 = rdtscp();

    printResult("getBestBid+Ask+Spread (per trio)", n, t1 - t0);
}

static void benchMixed(int n, double cancelRate, const char* label) {
    std::mt19937 rng(45);
    auto prices = ouWalk(rng, n + n / 10);
    std::uniform_real_distribution<double> roll(0.0, 1.0);

    OrderBook book;
    seedBook(book);
    std::deque<int> liveIds;

    // All orders are offset away from mid so they never cross — every add
    // results in a resting order, every cancel is genuine.
    auto restingPrice = [](double p) -> std::pair<Side, double> {
        if (p <= 100.0) return {Side::Buy,  p - 0.50};
        else            return {Side::Sell, p + 0.50};
    };

    // Warm-up
    for (int i = 0; i < n / 10; ++i) {
        auto [side, p] = restingPrice(prices[i]);
        liveIds.push_back(book.addOrder(side, p, 1.0));
        if (!liveIds.empty() && roll(rng) < cancelRate) {
            book.cancelOrder(liveIds.front());
            liveIds.pop_front();
        }
    }

    uint64_t t0 = rdtscp();
    int ops = 0;
    for (int i = n / 10; i < n + n / 10; ++i) {
        auto [side, p] = restingPrice(prices[i]);
        liveIds.push_back(book.addOrder(side, p, 1.0));
        ++ops;
        if (!liveIds.empty() && roll(rng) < cancelRate) {
            book.cancelOrder(liveIds.front());
            liveIds.pop_front();
            ++ops;
        }
    }
    uint64_t t1 = rdtscp();

    printResult(label, ops, t1 - t0);
}


int main() {
    constexpr int N_STD    = 500000;
    constexpr int N_CROSS  = 100000;
    constexpr int N_CHEAP  = 1000000;

    std::cout << "=== Iteration 9 Benchmarks ===\n\n";

    benchAddNoCross(N_STD);
    benchAddCross1Level(N_CROSS);
    benchAddCross5Levels(N_CROSS);
    benchCancelOrder(N_STD);
    benchGetBestBidAskSpread(N_CHEAP);
    benchMixed(N_STD, 0.10, "mixed workload cancel=10%");
    benchMixed(N_STD, 0.50, "mixed workload cancel=50%");
    benchMixed(N_STD, 0.90, "mixed workload cancel=90%");

    return 0;
}
