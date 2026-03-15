#include "orderbook.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

static uint64_t rdtscp() {
    uint32_t lo, hi, aux;
    __asm__ volatile ("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return ((uint64_t)hi << 32) | lo;
}

static void printBook(const OrderBook& book) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  best bid : " << (book.getBestBid()  ? std::to_string(*book.getBestBid())  : "—") << "\n";
    std::cout << "  best ask : " << (book.getBestAsk()  ? std::to_string(*book.getBestAsk())  : "—") << "\n";
    std::cout << "  spread   : " << (book.getSpread()   ? std::to_string(*book.getSpread())   : "—") << "\n";
}

int main() {
    OrderBook book;
    std::cout << "=== OrderBook Smoke Test (Iteration 1) ===\n\n";

    // Build a two-sided book
    book.addOrder(Side::Buy,   99.50, 100.0);
    book.addOrder(Side::Buy,   99.00, 200.0);
    book.addOrder(Side::Buy,   98.50, 150.0);
    book.addOrder(Side::Sell, 100.50, 100.0);
    book.addOrder(Side::Sell, 101.00, 200.0);
    book.addOrder(Side::Sell, 101.50, 150.0);

    std::cout << "After building book:\n";
    printBook(book);

    // Crossing buy — should match against 100.50 ask
    std::cout << "\nSubmit buy 150 @ 101.00 (crosses 100.50 ask):\n";
    book.addOrder(Side::Buy, 101.00, 150.0);
    printBook(book);

    // Cancel a resting bid
    int id = book.addOrder(Side::Buy, 98.00, 50.0);
    std::cout << "\nAdded bid id=" << id << " @ 98.00 qty 50, then cancel it:\n";
    book.cancelOrder(id);
    printBook(book);

    // --- CSV load test + timing ---
    std::cout << "\n=== CSV Load Test ===\n\n";

    std::ifstream csv("data/orders.csv");
    if (!csv.is_open()) {
        std::cerr << "Could not open data/orders.csv\n";
        return 1;
    }

    // Pre-load all lines so CSV I/O is not included in the orderbook timing
    std::vector<std::string> lines;
    lines.reserve(1000001);
    std::string line;
    std::getline(csv, line);  // skip header
    while (std::getline(csv, line)) lines.push_back(line);

    constexpr bool INSTRUMENT_P          = true;   // set false to eliminate p-tracking at compile time
    constexpr bool INSTRUMENT_TIMESERIES = true;   // set false to eliminate time-series at compile time
    constexpr std::size_t TS_BUCKETS     = 10;     // constexpr → stack-allocated std::array, no heap

    int loaded = 0;
    int malformed = 0;
    OrderBook csvBook;

    std::vector<std::size_t> pSamples;
    if constexpr (INSTRUMENT_P) pSamples.reserve(lines.size());

    // Stack-allocated: TS_BUCKETS is constexpr so array size is compile-time
    std::array<uint64_t, TS_BUCKETS + 1> tsMarks{};
    std::size_t tsNext = 0;   // next boundary (runtime — lines.size() not constexpr)
    std::size_t tsMark = 1;   // next slot to write into tsMarks
    if constexpr (INSTRUMENT_TIMESERIES)
        tsNext = lines.size() / TS_BUCKETS;

    uint64_t t0 = rdtscp();
    if constexpr (INSTRUMENT_TIMESERIES) tsMarks[0] = t0;

    for (const auto& row : lines) {
        std::istringstream ss(row);
        std::string priceStr, qtyStr, sideStr;

        if (!std::getline(ss, priceStr, ',') ||
            !std::getline(ss, qtyStr,   ',') ||
            !std::getline(ss, sideStr,  ',')) {
            ++malformed;
            continue;
        }

        Side side;
        if      (sideStr == "B") side = Side::Buy;
        else if (sideStr == "S") side = Side::Sell;
        else { ++malformed; continue; }

        csvBook.addOrder(side, std::stod(priceStr), std::stod(qtyStr));
        if constexpr (INSTRUMENT_P)
            pSamples.push_back(csvBook.bidLevels() + csvBook.askLevels());
        ++loaded;

        // Equality check — cheaper than modulo on every iteration
        if constexpr (INSTRUMENT_TIMESERIES) {
            if ((std::size_t)loaded == tsNext && tsMark < TS_BUCKETS) {
                tsMarks[tsMark++] = rdtscp();
                tsNext += lines.size() / TS_BUCKETS;
            }
        }
    }

    uint64_t t1 = rdtscp();
    if constexpr (INSTRUMENT_TIMESERIES) tsMarks[TS_BUCKETS] = t1;
    uint64_t cycles = t1 - t0;

    std::cout << "Rows loaded  : " << loaded    << "\n";
    std::cout << "Malformed    : " << malformed  << "\n";
    std::cout << "Total cycles : " << cycles     << "\n";
    std::cout << "Cycles/order : " << cycles / loaded << "\n";

    if constexpr (INSTRUMENT_P) {
        std::sort(pSamples.begin(), pSamples.end());
        double pMean = (double)std::accumulate(pSamples.begin(), pSamples.end(), 0ULL) / pSamples.size();
        std::cout << "\n--- Active price levels (p = bids + asks) ---\n";
        std::cout << "  min  : " << pSamples.front() << "\n";
        std::cout << "  mean : " << std::fixed << std::setprecision(1) << pMean << "\n";
        std::cout << "  p50  : " << pSamples[pSamples.size() * 50 / 100] << "\n";
        std::cout << "  p95  : " << pSamples[pSamples.size() * 95 / 100] << "\n";
        std::cout << "  p99  : " << pSamples[pSamples.size() * 99 / 100] << "\n";
        std::cout << "  max  : " << pSamples.back() << "\n";
    }

    if constexpr (INSTRUMENT_TIMESERIES) {
        const std::size_t bucketOrders = lines.size() / TS_BUCKETS;
        std::cout << "\n--- Cycles/order by decile ---\n";
        for (std::size_t i = 0; i < TS_BUCKETS; ++i) {
            std::cout << "  [" << std::setw(3) << (i * 10)
                      << "-" << std::setw(3) << ((i + 1) * 10) << "%] : "
                      << (tsMarks[i + 1] - tsMarks[i]) / bucketOrders << "\n";
        }
    }
    printBook(csvBook);

    return 0;
}
