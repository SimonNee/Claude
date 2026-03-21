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

// OU price walk — parameterised by mid, band, ticks_per_unit.
// Snaps prices to the tick grid (round to nearest tick_size = 1/ticks_per_unit).
// STEP and THETA match gen_orders.q.
static std::vector<double> ouWalk(std::mt19937& rng, int n,
                                   double mid, double band, int ticks_per_unit) {
    constexpr double STEP  = 0.05;
    constexpr double THETA = 0.05;
    const double tpu = static_cast<double>(ticks_per_unit);

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

// Seed the book with 40 bid levels and 40 ask levels around mid.
// Spread is intentionally wide so benchmark adds don't cross.
template<typename BookType>
static void seedBook(BookType& book, double mid, double tick_size) {
    for (int i = 1; i <= 40; ++i)
        book.addOrder(Side::Buy,  mid - i * tick_size, 10.0);
    for (int i = 1; i <= 40; ++i)
        book.addOrder(Side::Sell, mid + i * tick_size, 10.0);
}

// --------------------------------------------------------------------------
// benchSuite — runs all benchmark operations for a given OrderBookT instantiation.
//
// base_price  : tick 0 of the book's price range.
// mid         : derived as base_price + (N_TICKS/2) * TICK_SIZE.
// band        : half the tick range; OU walk stays within [mid-band, mid+band].
// offset      : 10-tick safety margin used to keep no-cross adds away from mid.
// --------------------------------------------------------------------------

template<int N_TICKS, int TICKS_PER_UNIT>
static void benchSuite(const char* label, double base_price) {
    using BookType = OrderBookT<N_TICKS, TICKS_PER_UNIT>;
    constexpr double TICK_SIZE = 1.0 / TICKS_PER_UNIT;
    const double mid    = base_price + (N_TICKS / 2) * TICK_SIZE;
    const double band   = (N_TICKS / 2) * TICK_SIZE;
    constexpr double offset = 10.0 * TICK_SIZE;  // 10-tick no-cross safety margin

    constexpr int N_STD   = 500000;
    constexpr int N_CROSS = 100000;
    constexpr int N_CHEAP = 1000000;

    std::cout << "\n=== " << label << " ===\n\n";

    // --- addOrder no-cross ---
    {
        std::mt19937 rng(42);
        auto prices = ouWalk(rng, N_STD + N_STD / 10, mid, band, TICKS_PER_UNIT);
        BookType book(base_price);
        seedBook(book, mid, TICK_SIZE);
        for (int i = 0; i < N_STD / 10; ++i) {
            double p = prices[i];
            Side s   = (p < mid) ? Side::Buy : Side::Sell;
            book.addOrder(s, (s == Side::Buy) ? p - offset : p + offset, 1.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = N_STD / 10; i < N_STD + N_STD / 10; ++i) {
            double p = prices[i];
            Side s   = (p < mid) ? Side::Buy : Side::Sell;
            book.addOrder(s, (s == Side::Buy) ? p - offset : p + offset, 1.0);
        }
        uint64_t t1 = rdtscp();
        printResult("addOrder no-cross", N_STD, t1 - t0);
    }

    // --- addOrder no-cross uniform (full tick range) ---
    // Buys from lower half [0, midTick-offset), sells from upper half (midTick+offset, N_TICKS-1].
    // Forces the entire PriceLevel[N_TICKS] array into the working set — exposes L1→L2 transition.
    {
        constexpr int midTick    = N_TICKS / 2;
        constexpr int offsetTick = 10;
        std::mt19937 rng(46);
        std::uniform_int_distribution<int> buyTick(0,  midTick - offsetTick - 1);
        std::uniform_int_distribution<int> sellTick(midTick + offsetTick, N_TICKS - 1);

        BookType book(base_price);
        for (int i = 0; i < N_STD / 10; ++i) {
            if (i & 1) book.addOrder(Side::Sell, base_price + sellTick(rng) * TICK_SIZE, 1.0);
            else        book.addOrder(Side::Buy,  base_price + buyTick(rng)  * TICK_SIZE, 1.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = N_STD / 10; i < N_STD + N_STD / 10; ++i) {
            if (i & 1) book.addOrder(Side::Sell, base_price + sellTick(rng) * TICK_SIZE, 1.0);
            else        book.addOrder(Side::Buy,  base_price + buyTick(rng)  * TICK_SIZE, 1.0);
        }
        uint64_t t1 = rdtscp();
        printResult("addOrder no-cross uniform", N_STD, t1 - t0);
    }

    // --- addOrder crossing 1 level ---
    {
        const double cross_price = mid + TICK_SIZE;
        BookType book(base_price);
        for (int i = 0; i < N_CROSS / 10; ++i) {
            book.addOrder(Side::Sell, cross_price, 1.0);
            book.addOrder(Side::Buy,  cross_price, 1.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = 0; i < N_CROSS; ++i) {
            book.addOrder(Side::Sell, cross_price, 1.0);  // resting ask
            book.addOrder(Side::Buy,  cross_price, 1.0);  // crossing buy — drains it
        }
        uint64_t t1 = rdtscp();
        printResult("addOrder crossing 1 level", N_CROSS, t1 - t0);
    }

    // --- addOrder crossing 5 levels ---
    {
        BookType book(base_price);
        for (int i = 0; i < N_CROSS / 10; ++i) {
            for (int l = 1; l <= 5; ++l)
                book.addOrder(Side::Sell, mid + l * TICK_SIZE, 1.0);
            book.addOrder(Side::Buy, mid + 6.0 * TICK_SIZE, 5.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = 0; i < N_CROSS; ++i) {
            for (int l = 1; l <= 5; ++l)
                book.addOrder(Side::Sell, mid + l * TICK_SIZE, 1.0);
            book.addOrder(Side::Buy, mid + 6.0 * TICK_SIZE, 5.0);  // crosses 5 ask levels
        }
        uint64_t t1 = rdtscp();
        printResult("addOrder crossing 5 levels", N_CROSS, t1 - t0);
    }

    // --- cancelOrder ---
    {
        BookType book(base_price);
        std::deque<int> ids;
        std::mt19937 rng(44);
        auto prices = ouWalk(rng, N_STD + N_STD / 10, mid, band, TICKS_PER_UNIT);
        for (int i = 0; i < N_STD; ++i) {
            double p = prices[i];
            Side s   = (p < mid) ? Side::Buy : Side::Sell;
            ids.push_back(book.addOrder(s, (s == Side::Buy) ? p - offset : p + offset, 1.0));
        }
        int warmup = N_STD / 10;
        for (int i = 0; i < warmup; ++i) { book.cancelOrder(ids.front()); ids.pop_front(); }
        for (int i = N_STD; i < N_STD + warmup; ++i) {
            double p = prices[i];
            Side s   = (p < mid) ? Side::Buy : Side::Sell;
            ids.push_back(book.addOrder(s, (s == Side::Buy) ? p - offset : p + offset, 1.0));
        }
        uint64_t t0 = rdtscp();
        for (int i = 0; i < N_STD; ++i) { book.cancelOrder(ids.front()); ids.pop_front(); }
        uint64_t t1 = rdtscp();
        printResult("cancelOrder", N_STD, t1 - t0);
    }

    // --- getBestBid+Ask+Spread ---
    {
        BookType book(base_price);
        seedBook(book, mid, TICK_SIZE);
        double sink = 0.0;
        for (int i = 0; i < N_CHEAP / 10; ++i) {
            sink += book.getBestBid().value_or(0.0);
            sink += book.getBestAsk().value_or(0.0);
            sink += book.getSpread().value_or(0.0);
        }
        uint64_t t0 = rdtscp();
        for (int i = 0; i < N_CHEAP; ++i) {
            sink += book.getBestBid().value_or(0.0);
            sink += book.getBestAsk().value_or(0.0);
            sink += book.getSpread().value_or(0.0);
        }
        uint64_t t1 = rdtscp();
        printResult("getBestBid+Ask+Spread (per trio)", N_CHEAP, t1 - t0);
        if (sink == -1.0) std::cout << sink;  // prevent sink optimisation without branching in hot path
    }

    // --- mixed workload ---
    auto runMixed = [&](double cancelRate, const char* mixLabel) {
        std::mt19937 rng(45);
        auto prices = ouWalk(rng, N_STD + N_STD / 10, mid, band, TICKS_PER_UNIT);
        std::uniform_real_distribution<double> roll(0.0, 1.0);
        BookType book(base_price);
        seedBook(book, mid, TICK_SIZE);
        std::deque<int> liveIds;
        auto restingPrice = [&](double p) -> std::pair<Side, double> {
            return (p <= mid) ? std::make_pair(Side::Buy,  p - offset)
                              : std::make_pair(Side::Sell, p + offset);
        };
        for (int i = 0; i < N_STD / 10; ++i) {
            auto [side, p] = restingPrice(prices[i]);
            liveIds.push_back(book.addOrder(side, p, 1.0));
            if (!liveIds.empty() && roll(rng) < cancelRate) {
                book.cancelOrder(liveIds.front()); liveIds.pop_front();
            }
        }
        uint64_t t0 = rdtscp();
        int ops = 0;
        for (int i = N_STD / 10; i < N_STD + N_STD / 10; ++i) {
            auto [side, p] = restingPrice(prices[i]);
            liveIds.push_back(book.addOrder(side, p, 1.0));
            ++ops;
            if (!liveIds.empty() && roll(rng) < cancelRate) {
                book.cancelOrder(liveIds.front()); liveIds.pop_front(); ++ops;
            }
        }
        uint64_t t1 = rdtscp();
        printResult(mixLabel, ops, t1 - t0);
    };

    runMixed(0.10, "mixed workload cancel=10%");
    runMixed(0.50, "mixed workload cancel=50%");
    runMixed(0.90, "mixed workload cancel=90%");
}

// --------------------------------------------------------------------------

int main() {
    std::cout << "=== Iteration 18 Benchmarks ===\n";

    benchSuite<100,   20>("100 ticks  0.05", 97.50);   // current baseline
    benchSuite<200,   20>("200 ticks  0.05", 95.00);
    benchSuite<500,   20>("500 ticks  0.05", 87.50);
    benchSuite<500,  100>("500 ticks  0.01", 99.50);
    benchSuite<1000,  20>("1000 ticks 0.05", 75.00);
    benchSuite<2000,  20>("2000 ticks 0.05", 50.00);   // L2: ~160KB metadata
    benchSuite<3000,  20>("3000 ticks 0.05", 25.00);   // L2 limit: ~240KB metadata
    benchSuite<4000,  20>("4000 ticks 0.05",  0.00);   // L3: ~320KB metadata

    return 0;
}
