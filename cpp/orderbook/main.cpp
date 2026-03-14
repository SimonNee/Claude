#include "orderbook.h"

#include <iomanip>
#include <iostream>

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

    return 0;
}
