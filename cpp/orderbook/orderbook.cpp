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
    // agentASM — Iteration 4.
    // Syntax: AT&T (short integer/FP block, mixing with existing AT&T codebase).
    //
    // Two redundancies fixed vs. the -O2 compiler output of the plain C++ body:
    //
    //   Redundancy 1 — resting.quantity double-load:
    //     Compiler emits:  movsd 8(%rax), %xmm1   ; load resting.qty
    //                      minsd %xmm0, %xmm1     ; xmm1 = fill  (original qty lost)
    //                      ...
    //                      movsd 8(%rax), %xmm0   ; reload resting.qty  <-- REDUNDANT
    //                      subsd %xmm1, %xmm0     ; xmm0 = resting.qty - fill
    //     Fix: copy resting.quantity into %[rq] before minsd overwrites %[fill].
    //     The subtraction then uses %[rq] directly — no reload from memory.
    //
    //   Redundancy 2 — orders.data() reload every iteration:
    //     Compiler emits:  movq 8(%rcx), %rax  inside the inner loop.
    //     Fix: hoist 'data = level.orders.data()' before the while loop.
    //     pop_front() only increments head; it never reallocates, so the
    //     pointer is stable for the entire duration of one level's inner loop.

    bool drained = false;
    for (auto& level : asks) {
        if (order.quantity <= 0.0 || level.price > order.price) break;

        // Hoist: data pointer is stable across pop_front() calls on this level.
        Order* const data = level.orders.data();

        while (!level.empty() && order.quantity > 0.0) {
            // ---------------------------------------------------------------
            // Fill arithmetic — single asm block targeting both redundancies.
            //
            // GPR scratch [rptr]:
            //   Receives data + head*24 (the resting Order address).
            //   "=&r" — early-clobber: written by the first LEA before the
            //   asm has finished reading [data] and [head], so & is required
            //   to prevent the allocator from aliasing rptr with either input.
            //
            // XMM scratch [rq] (resting quantity, saved copy):
            //   Holds resting_ptr->quantity after the first movsd.
            //   "=&x" — early-clobber: written before [order_qty] is consumed
            //   by subsd, so & is required.
            //   After the asm block, [rq] holds the post-subtraction value
            //   (resting.quantity - fill).  C++ reads this to decide whether
            //   to run the fill-complete path (== 0.0).
            //   On the NaN path (jp taken), the movsd store is skipped;
            //   [rq] still holds resting.qty - fill (NaN), so == 0.0 is false
            //   and the fill-complete path is correctly skipped.
            //
            // XMM scratch [fill]:
            //   Receives a copy of order.quantity then becomes fill = min(...).
            //   "=&x" — early-clobber: written (copy of order_qty) before all
            //   inputs are read.  Allocated to a register distinct from [rq]
            //   and [order_qty] because they must coexist during minsd/subsd.
            //
            // [order_qty]:
            //   "+x" — read-write.  order.quantity is read as the initial value
            //   and written with order.quantity - fill.  The asm also stores it
            //   to 8(%[order]) explicitly so the store reaches memory inside the
            //   block; the constraint write-back on exit stores the same value
            //   and is harmless.
            //
            // [zero]: "x" input holding 0.0 for ucomisd.  Using an XMM input
            //   rather than an immediate because ucomisd has no form for
            //   immediate-double operands.
            //
            // Clobbers: "cc" because subsd/ucomisd modify RFLAGS.
            //           "memory" because the asm writes to resting_ptr->quantity
            //           (an untracked pointer dereference).
            // ---------------------------------------------------------------

            double resting_qty_new;   // receives [rq] value after subtraction
            double fill_scratch;      // [fill] XMM scratch, not needed post-asm
            Order* resting_ptr;       // receives computed resting Order address

            __asm__ volatile (
                /* --- Compute resting_ptr = data + head * 24 ---
                   head * 3  via LEA  (avoids imul; scale 2 gives head + head*2)
                   head * 24 via second LEA with scale 8                        */
                "leaq  (%[head],%[head],2), %[rptr]\n\t"
                "leaq  (%[data],%[rptr],8), %[rptr]\n\t"

                /* --- Load resting.quantity and save a copy ---
                   Offset +8 within Order (price=0, quantity=8).
                   [rq] holds the original value; it is NOT overwritten by minsd.
                   This is the fix for Redundancy 1.                            */
                "movsd 8(%[rptr]), %[rq]\n\t"

                /* --- Compute fill = min(order.quantity, resting.quantity) ---
                   Copy order.quantity into [fill], then minsd with [rq].
                   minsd(src,dst): dst = min(dst,src)  →  fill = min(order_qty, rq) */
                "movsd %[order_qty], %[fill]\n\t"
                "minsd %[rq], %[fill]\n\t"

                /* --- order.quantity -= fill ---
                   subsd(src,dst): dst = dst - src  →  order_qty -= fill        */
                "subsd %[fill], %[order_qty]\n\t"
                "movsd %[order_qty], 8(%[order])\n\t"

                /* --- resting.quantity -= fill  (uses saved copy, no reload) ---
                   This is the fix for Redundancy 1: [rq] still holds the
                   original resting.quantity, so no second movsd 8(%[rptr]).    */
                "subsd %[fill], %[rq]\n\t"

                /* --- NaN guard (preserves IEEE 754 semantics) ---
                   ucomisd sets PF=1 if either operand is NaN.
                   jp skips the store; the while condition (NaN > 0.0 == false)
                   then exits the inner loop on the next iteration check.       */
                "ucomisd %[zero], %[rq]\n\t"
                "jp    .Lnan%=\n\t"

                /* --- Store updated resting.quantity ---                        */
                "movsd %[rq], 8(%[rptr])\n\t"

                ".Lnan%=:\n\t"

                /* outputs */
                : [order_qty] "+x"  (order.quantity),
                  [rq]        "=&x" (resting_qty_new),
                  [fill]      "=&x" (fill_scratch),
                  [rptr]      "=&r" (resting_ptr)
                /* inputs */
                : [data]      "r"   (data),
                  [head]      "r"   (level.head),
                  [order]     "r"   (&order),
                  [zero]      "x"   (0.0)
                /* clobbers */
                : "cc", "memory"
            );

            // resting_qty_new is the post-subtraction resting.quantity (from [rq]).
            // resting_ptr points to the resting Order (data + head*24).
            if (resting_qty_new == 0.0) {
                orderIndex[resting_ptr->id] = std::nullopt;
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
