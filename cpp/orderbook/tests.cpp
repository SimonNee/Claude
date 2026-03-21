#include "orderbook.h"

#include <cassert>
#include <iostream>

// ---- helpers ----------------------------------------------------------------

static void pass(const char* name) {
    std::cout << "[PASS] " << name << "\n";
}

// ---- tests ------------------------------------------------------------------

void test_empty_book() {
    OrderBook book;
    assert(!book.getBestBid());
    assert(!book.getBestAsk());
    assert(!book.getSpread());
    pass("test_empty_book");
}

void test_add_single_bid() {
    OrderBook book;
    int id = book.addOrder(Side::Buy, 100.0, 10.0);
    assert(id == 1);
    assert(book.getBestBid() == 100.0);
    assert(!book.getBestAsk());
    assert(!book.getSpread());
    pass("test_add_single_bid");
}

void test_add_single_ask() {
    OrderBook book;
    int id = book.addOrder(Side::Sell, 101.0, 10.0);
    assert(id == 1);
    assert(!book.getBestBid());
    assert(book.getBestAsk() == 101.0);
    assert(!book.getSpread());
    pass("test_add_single_ask");
}

void test_spread() {
    OrderBook book;
    book.addOrder(Side::Buy,  99.0, 5.0);
    book.addOrder(Side::Sell, 101.0, 5.0);
    assert(book.getBestBid()  == 99.0);
    assert(book.getBestAsk()  == 101.0);
    assert(book.getSpread()   == 2.0);
    pass("test_spread");
}

void test_cancel_order() {
    OrderBook book;
    int id = book.addOrder(Side::Buy, 100.0, 10.0);
    assert(book.cancelOrder(id));
    assert(!book.getBestBid());
    assert(!book.cancelOrder(id));  // already gone
    pass("test_cancel_order");
}

void test_cancel_nonexistent() {
    OrderBook book;
    assert(!book.cancelOrder(999));
    pass("test_cancel_nonexistent");
}

void test_full_fill() {
    OrderBook book;
    book.addOrder(Side::Buy,  100.0, 10.0);
    book.addOrder(Side::Sell, 100.0, 10.0);
    assert(!book.getBestBid());
    assert(!book.getBestAsk());
    pass("test_full_fill");
}

void test_partial_fill_buy_remainder() {
    OrderBook book;
    book.addOrder(Side::Buy,  100.0, 10.0);
    book.addOrder(Side::Sell, 100.0,  6.0);
    // 4 units of the buy should remain
    assert(book.getBestBid() == 100.0);
    assert(!book.getBestAsk());
    pass("test_partial_fill_buy_remainder");
}

void test_partial_fill_sell_remainder() {
    OrderBook book;
    book.addOrder(Side::Buy,  100.0,  6.0);
    book.addOrder(Side::Sell, 100.0, 10.0);
    // 4 units of the sell should remain
    assert(!book.getBestBid());
    assert(book.getBestAsk() == 100.0);
    pass("test_partial_fill_sell_remainder");
}

void test_fifo_at_price_level() {
    OrderBook book;
    int id1 = book.addOrder(Side::Buy, 100.0, 5.0);
    int id2 = book.addOrder(Side::Buy, 100.0, 5.0);
    // Sell 5 — should consume id1 first (FIFO)
    book.addOrder(Side::Sell, 100.0, 5.0);
    // id1 fully filled; id2 still resting
    assert(book.getBestBid() == 100.0);
    assert(book.cancelOrder(id2));   // id2 is there
    assert(!book.cancelOrder(id1));  // id1 already consumed
    assert(!book.getBestBid());
    pass("test_fifo_at_price_level");
}

void test_price_time_priority_best_bid() {
    OrderBook book;
    book.addOrder(Side::Buy,  99.0, 5.0);
    book.addOrder(Side::Buy, 101.0, 5.0);
    assert(book.getBestBid() == 101.0);
    pass("test_price_time_priority_best_bid");
}

void test_price_time_priority_best_ask() {
    OrderBook book;
    book.addOrder(Side::Sell, 102.0, 5.0);
    book.addOrder(Side::Sell, 100.0, 5.0);
    assert(book.getBestAsk() == 100.0);
    pass("test_price_time_priority_best_ask");
}

void test_crossing_order_drains_multiple_levels() {
    OrderBook book;
    book.addOrder(Side::Buy, 101.0, 5.0);
    book.addOrder(Side::Buy, 100.0, 5.0);
    // Sell 7 at 100 crosses both levels: fills all 5 @ 101, then 2 @ 100
    book.addOrder(Side::Sell, 100.0, 7.0);
    // 3 units remain at 100
    assert(book.getBestBid() == 100.0);
    assert(!book.getBestAsk());
    pass("test_crossing_order_drains_multiple_levels");
}

void test_cancel_then_cross_same_level() {
    // Cancel a resting order, add a second live order at the same level,
    // then cross. The aggressor must fill only against the live order —
    // not the tombstone.
    OrderBook book;
    int id1 = book.addOrder(Side::Sell, 100.0, 5.0);  // resting ask — will be cancelled
    book.cancelOrder(id1);                              // tombstone at head
    int id2 = book.addOrder(Side::Sell, 100.0, 5.0);  // live ask behind the tombstone

    // Crossing buy for 3 — should fill 3 from id2 only
    book.addOrder(Side::Buy, 100.0, 3.0);

    // 2 units of id2 should remain
    assert(book.getBestAsk() == 100.0);
    assert(!book.getBestBid());
    // id2 partially filled — still cancellable
    assert(book.cancelOrder(id2));
    assert(!book.getBestAsk());
    pass("test_cancel_then_cross_same_level");
}

void test_sell_does_not_cross_below_price() {
    OrderBook book;
    book.addOrder(Side::Buy, 99.0, 10.0);
    // Ask at 100 — does NOT cross bid at 99
    book.addOrder(Side::Sell, 100.0, 10.0);
    assert(book.getBestBid() == 99.0);
    assert(book.getBestAsk() == 100.0);
    assert(book.getSpread()  == 1.0);
    pass("test_sell_does_not_cross_below_price");
}

// ---- main -------------------------------------------------------------------

int main() {
    test_empty_book();
    test_add_single_bid();
    test_add_single_ask();
    test_spread();
    test_cancel_order();
    test_cancel_nonexistent();
    test_full_fill();
    test_partial_fill_buy_remainder();
    test_partial_fill_sell_remainder();
    test_fifo_at_price_level();
    test_price_time_priority_best_bid();
    test_price_time_priority_best_ask();
    test_crossing_order_drains_multiple_levels();
    test_sell_does_not_cross_below_price();
    test_cancel_then_cross_same_level();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
