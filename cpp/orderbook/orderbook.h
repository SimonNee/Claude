#pragma once

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

enum class Side { Buy, Sell };

// Order.price removed in Iteration 7 — price is encoded by the level's slot index.
// sizeof(Order) == 16 (was 24; was 32 in Iteration 1).
struct Order {
    double quantity;
    int    id;
    Side   side;
};

static_assert(sizeof(Order) == 16, "Order layout changed — check struct padding");

// A single price level: a contiguous queue of resting orders.
// The level's price is encoded by its slot index in bid_levels/ask_levels;
// the price field has been removed — use tickToPrice(tick) at the call site.
// head advances past filled/cancelled orders (logical pop_front, no shifting).
// Cancelled orders are marked with id=0 (tombstone); the match loop skips them.
// liveOrders tracks non-cancelled, non-filled orders for O(1) empty() checks.
struct PriceLevel {
    std::vector<Order> orders;
    std::size_t        head       = 0;
    std::size_t        liveOrders = 0;

    bool         empty()     const { return liveOrders == 0; }
    std::size_t  liveCount() const { return liveOrders; }
    Order&       front()           { return orders[head]; }
    const Order& front()     const { return orders[head]; }

    // Advance head. Only decrements liveOrders for live orders — tombstones
    // (id=0) were already decremented at cancel time.
    void pop_front() {
        if (orders[head].id != 0) --liveOrders;
        ++head;
    }

    void push_back(const Order& o) { ++liveOrders; orders.push_back(o); }

    // Mark order at oi as cancelled. O(1) — no shifting.
    void cancel_at(std::size_t oi) { orders[oi].id = 0; --liveOrders; }
};

static_assert(sizeof(PriceLevel) == 40, "PriceLevel layout changed");

// Forward declarations of bitmap helpers (defined in orderbook_impl.h).
// Declared here so getBestBid/getBestAsk/getSpread can be defined inline
// in the class body — inline definitions are always inlined by GCC regardless
// of LTO inlining heuristics (no "call is cold" / code-growth refusal).
template<int NWORDS> static inline int lowestBit(const uint64_t* bits);
template<int NWORDS> static inline int highestBit(const uint64_t* bits);

template<int N_TICKS, int TICKS_PER_UNIT>
class OrderBookT {
public:
    // N_BITMAP_WORDS is determined entirely by N_TICKS and locks in the bitmap array sizes.
    // At N_TICKS=100 → 2 words. At N_TICKS=500 → 8 words. At N_TICKS=1000 → 16 words.
    // N_TICKS <= ~400 fits in 32KB L1; N_TICKS=1000 is ~80KB (L2); N_TICKS >= 1000 use heap allocation.
    static constexpr int N_BITMAP_WORDS = (N_TICKS + 63) / 64;

    // Constructor default argument preserves the current effective BASE_PRICE=97.50
    // for the <100,20> instantiation: 100.0 - 50 * (1.0/20) = 100.0 - 2.50 = 97.50.
    explicit OrderBookT(double base_price = 100.0 - 50 * (1.0 / TICKS_PER_UNIT))
        : base_price_(base_price) {}

    // Insert a limit order. Matches immediately if crossing. Returns assigned id.
    int addOrder(Side side, double price, double quantity);

    // Remove an order by id. Returns true if found and removed.
    bool cancelOrder(int id);

    // Inline definitions: always inlined by GCC without LTO heuristic refusal.
    // lowestBit/highestBit are forward-declared above the class body.
    std::optional<double> getBestBid() const {
        int tick = highestBit<N_BITMAP_WORDS>(bid_bits);
        if (tick < 0) return std::nullopt;
        return tickToPrice(tick);
    }
    std::optional<double> getBestAsk() const {
        int tick = lowestBit<N_BITMAP_WORDS>(ask_bits);
        if (tick < 0) return std::nullopt;
        return tickToPrice(tick);
    }
    // Returns ask - bid. nullopt if either side is empty.
    std::optional<double> getSpread() const {
        auto bid = getBestBid();
        auto ask = getBestAsk();
        if (!bid || !ask) return std::nullopt;
        return *ask - *bid;
    }

    // Instrumentation — active price level counts (set bits in bitmap)
    std::size_t bidLevels() const {
        std::size_t n = 0;
        for (int w = 0; w < N_BITMAP_WORDS; ++w) n += __builtin_popcountll(bid_bits[w]);
        return n;
    }
    std::size_t askLevels() const {
        std::size_t n = 0;
        for (int w = 0; w < N_BITMAP_WORDS; ++w) n += __builtin_popcountll(ask_bits[w]);
        return n;
    }

    // Instrumentation — append live order count per level into out
    void sampleLiveCounts(std::vector<std::size_t>& out) const {
        for (int i = 0; i < N_TICKS; ++i)
            if (bid_levels[i].liveCount() > 0) out.push_back(bid_levels[i].liveCount());
        for (int i = 0; i < N_TICKS; ++i)
            if (ask_levels[i].liveCount() > 0) out.push_back(ask_levels[i].liveCount());
    }

private:
    int nextId = 1;

    double base_price_;

    // Bitmap: bit i set means tick i has live orders on that side.
    // Fixed arrays of N_TICKS levels — slot index encodes price.
    uint64_t   bid_bits[N_BITMAP_WORDS] = {};
    uint64_t   ask_bits[N_BITMAP_WORDS] = {};
    PriceLevel bid_levels[N_TICKS];
    PriceLevel ask_levels[N_TICKS];

    struct OrderLocation {
        Side        side;
        int         levelTick;  // direct index into bid_levels / ask_levels; -1 = empty sentinel
        std::size_t orderIdx;   // index into PriceLevel::orders — stable (no shifting)
    };

    // O(1) cancel lookup: id → exact location.
    // Direct-index vector: orderIndex[id] holds the location while the order rests.
    // Avoids std::unordered_map's divq (prime rehash policy) and per-node operator delete.
    // IDs are sequential from 1; vector grows as needed and slots are reset to kEmptyLocation on use.
    // kEmptyLocation.levelTick == -1 is the sentinel for "slot is empty"; -1 is outside
    // the valid tick range [0, N_TICKS) and cannot arise from priceToTick() on a valid price.
    static constexpr OrderLocation kEmptyLocation{Side::Buy, -1, 0};
    static_assert(sizeof(OrderLocation) == 16, "OrderLocation layout changed — check sentinel and imulq elimination");
    std::vector<OrderLocation> orderIndex;

    // Convert between double price and integer tick index.
    // Tick 0 == base_price_; tick N == base_price_ + N * (1.0/TICKS_PER_UNIT).
    inline int priceToTick(double price) const {
        return static_cast<int>((price - base_price_) * TICKS_PER_UNIT + 0.5);
    }
    inline double tickToPrice(int tick) const {
        return base_price_ + tick * (1.0 / TICKS_PER_UNIT);
    }

    void matchBuy(Order& order, int orderTick);
    void matchSell(Order& order, int orderTick);
};

#include "orderbook_impl.h"

using OrderBook = OrderBookT<100, 20>;
