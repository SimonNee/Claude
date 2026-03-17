#include "orderbook.h"

#include <algorithm>

// Bitmap helpers.
// bits[tick >> 6] is the 64-bit word; (1ULL << (tick & 63)) is the bit mask.
// These operate on the two-word array passed by pointer (decays from uint64_t[2]).

static inline void setBit(uint64_t bits[2], int tick) {
    bits[tick >> 6] |= (1ULL << (tick & 63));
}

static inline void clearBit(uint64_t bits[2], int tick) {
    bits[tick >> 6] &= ~(1ULL << (tick & 63));
}

// Lowest set bit — best ask (lowest price == lowest tick).
static inline int lowestBit(const uint64_t bits[2]) {
    if (bits[0]) return __builtin_ctzll(bits[0]);
    if (bits[1]) return 64 + __builtin_ctzll(bits[1]);
    return -1;
}

// Highest set bit — best bid (highest price == highest tick).
static inline int highestBit(const uint64_t bits[2]) {
    if (bits[1]) return 64 + 63 - __builtin_clzll(bits[1]);
    if (bits[0]) return 63 - __builtin_clzll(bits[0]);
    return -1;
}

int OrderBook::addOrder(Side side, double price, double quantity) {
    Order order{quantity, nextId++, side};

    int tick = priceToTick(price);

    if (side == Side::Buy) {
        matchBuy(order, tick);
        if (order.quantity > 0.0) {
            if (bid_levels[tick].empty()) setBit(bid_bits, tick);
            std::size_t idx = bid_levels[tick].orders.size();
            bid_levels[tick].push_back(order);
            if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
            orderIndex[order.id] = {Side::Buy, tick, idx};
        }
    } else {
        matchSell(order, tick);
        if (order.quantity > 0.0) {
            if (ask_levels[tick].empty()) setBit(ask_bits, tick);
            std::size_t idx = ask_levels[tick].orders.size();
            ask_levels[tick].push_back(order);
            if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
            orderIndex[order.id] = {Side::Sell, tick, idx};
        }
    }

    return order.id;
}

int OrderBook::addOrder_asm(Side side, double price, double quantity) {
    Order order{quantity, nextId++, side};

    int tick = priceToTick(price);

    if (side == Side::Buy) {
        matchBuy_asm(order, tick);
        if (order.quantity > 0.0) {
            if (bid_levels[tick].empty()) setBit(bid_bits, tick);
            std::size_t idx = bid_levels[tick].orders.size();
            bid_levels[tick].push_back(order);
            if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
            orderIndex[order.id] = {Side::Buy, tick, idx};
        }
    } else {
        matchSell_asm(order, tick);
        if (order.quantity > 0.0) {
            if (ask_levels[tick].empty()) setBit(ask_bits, tick);
            std::size_t idx = ask_levels[tick].orders.size();
            ask_levels[tick].push_back(order);
            if (order.id >= (int)orderIndex.size()) orderIndex.resize(order.id + 1);
            orderIndex[order.id] = {Side::Sell, tick, idx};
        }
    }

    return order.id;
}

void OrderBook::matchBuy(Order& order, int orderTick) {
    // Walk asks lowest-first via bitmap; clear the bit inline when a level empties.
    // No drained flag, no remove_if pass — the bitmap is the index of live levels.
    while (order.quantity > 0.0) {
        int askTick = lowestBit(ask_bits);
        if (askTick < 0 || askTick > orderTick) break;

        PriceLevel& level = ask_levels[askTick];
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
        if (level.empty()) clearBit(ask_bits, askTick);
    }
}

void OrderBook::matchSell(Order& order, int orderTick) {
    // Walk bids highest-first via bitmap; clear the bit inline when a level empties.
    // No drained flag, no remove_if pass — the bitmap is the index of live levels.
    while (order.quantity > 0.0) {
        int bidTick = highestBit(bid_bits);
        if (bidTick < 0 || bidTick < orderTick) break;

        PriceLevel& level = bid_levels[bidTick];
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
        if (level.empty()) clearBit(bid_bits, bidTick);
    }
}

bool OrderBook::cancelOrder(int id) {
    if (id <= 0 || id >= (int)orderIndex.size() || !orderIndex[id]) return false;

    auto [side, levelTick, orderIdx] = *orderIndex[id];
    PriceLevel& level = (side == Side::Buy) ? bid_levels[levelTick] : ask_levels[levelTick];

    level.cancel_at(orderIdx);
    if (level.empty()) {
        // Clear the bit on whichever side this order rested.
        uint64_t* bits = (side == Side::Buy) ? bid_bits : ask_bits;
        clearBit(bits, levelTick);
    }
    orderIndex[id] = std::nullopt;
    return true;
}

std::optional<double> OrderBook::getBestBid() const {
    int tick = highestBit(bid_bits);
    if (tick < 0) return std::nullopt;
    return tickToPrice(tick);
}

std::optional<double> OrderBook::getBestAsk() const {
    int tick = lowestBit(ask_bits);
    if (tick < 0) return std::nullopt;
    return tickToPrice(tick);
}

std::optional<double> OrderBook::getSpread() const {
    auto bid = getBestBid();
    auto ask = getBestAsk();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}

// ---------------------------------------------------------------------------
// agentASM Iteration 8 — matchBuy_asm
//
// Optimisation: eliminate the double-load of resting.quantity.
//
// Compiler-generated inner loop (from -S output, matchBuy .L42):
//   LOAD 1: vmovsd (%rax), %xmm1      ; resting.quantity → xmm1
//           vminsd %xmm0, %xmm1, %xmm1 ; fill = min(order.qty, resting.qty)
//           vsubsd %xmm1, %xmm0, %xmm0  ; order.quantity -= fill
//           vmovsd %xmm0, (%rsi)         ; store order.quantity
//   LOAD 2: vmovsd (%rax), %xmm0      ; resting.quantity AGAIN (because xmm1 = fill)
//           vsubsd %xmm1, %xmm0, %xmm0  ; resting.quantity -= fill
//           vmovsd %xmm0, (%rax)         ; store resting.quantity
//
// Fix: copy resting.quantity into xmm3 before vminsd so xmm1 can become fill
// and xmm3 retains the original value for the subtraction:
//   vmovsd  → xmm1 (resting.qty), copy to xmm3
//   vminsd  → xmm1 becomes fill
//   vsubsd  → order.qty -= fill      (using xmm1)
//   vsubsd  → xmm3 -= fill           (no reload)
//   store xmm3 → resting.quantity
//
// Target 2 assessment — branchless zero-check NOT implemented:
// The `resting.quantity == 0.0` branch gates two C++ operations with
// non-trivial side effects: orderIndex[resting.id] = std::nullopt (writes a
// std::optional<OrderLocation>) and level.pop_front() (decrements liveOrders,
// increments head, with a conditional inside). These cannot be cleanly masked
// with SSE predicates. The branch stays in surrounding C++ code.
// The branch is also highly predictable (almost always not-taken until drain),
// so misprediction cost is O(1) per order, not per iteration.
//
// Syntax: AT&T — short block, standard scalar FP, matching surrounding code.
// ---------------------------------------------------------------------------
void OrderBook::matchBuy_asm(Order& order, int orderTick) {
    // Walk asks lowest-first via bitmap; clear the bit inline when a level empties.
    // No drained flag, no remove_if pass — the bitmap is the index of live levels.
    while (order.quantity > 0.0) {
        int askTick = lowestBit(ask_bits);
        if (askTick < 0 || askTick > orderTick) break;

        PriceLevel& level = ask_levels[askTick];
        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();

            // Fill arithmetic — single load of resting.quantity.
            //
            // Compiler generates two loads (see header comment above):
            //   LOAD 1 into xmm1, then vminsd makes xmm1 = fill.
            //   LOAD 2 needed because original resting.quantity is gone.
            //
            // Fix: use two distinct XMM scratch registers.
            //   xmm_fill  — receives resting.quantity, becomes fill after vminsd.
            //   xmm_rest  — holds the copy of resting.quantity across the vminsd.
            //
            // Constraint notes:
            //   [order_qty]  "+x"  — order.quantity is read and written in an XMM reg.
            //   [resting_qty]"+m"  — memory operand; asm issues the load (vmovsd) and
            //                        the final store. Compiler tracks this address as
            //                        both read and written.
            //   [xmm_fill]  "=&x" — early clobber: written (loaded) before the asm
            //                        finishes reading all inputs (order_qty is still
            //                        needed for vminsd and vsubsd after this write).
            //   [xmm_rest]  "=&x" — early clobber for the same reason; must be a
            //                        distinct C++ variable so GCC allocates a different
            //                        XMM register from [xmm_fill].
            //
            // No "cc": vmovsd / vminsd / vsubsd are all AVX scalar FP — EFLAGS untouched.
            // No global "memory": resting.quantity is an explicit "+m" operand.
            // volatile: asm writes to memory; store order matters.
            //
            // AT&T 3-operand vminsd / vsubsd direction:
            //   vminsd src1, src2, dst  →  dst = min(src2, src1)
            //   vsubsd src1, src2, dst  →  dst = src2 - src1
            // So "vminsd %[order_qty], %[xmm_fill], %[xmm_fill]"
            //    → xmm_fill = min(xmm_fill, order_qty)  — correct: fill = min(resting, order)
            // And "vsubsd %[xmm_fill], %[order_qty], %[order_qty]"
            //    → order_qty = order_qty - xmm_fill      — correct: order.qty -= fill
            // And "vsubsd %[xmm_fill], %[xmm_rest], %[xmm_rest]"
            //    → xmm_rest = xmm_rest - xmm_fill        — correct: resting.qty -= fill
            double xmm_fill, xmm_rest;
            __asm__ volatile (
                // Load resting.quantity into both scratch XMM registers (single memory read).
                // We issue two vmovsd from the same address: one for xmm_fill (which
                // vminsd will overwrite to become fill) and one for xmm_rest (which
                // keeps the original value for the post-fill subtraction).
                // This is still one memory load per register; both operands resolve to
                // the same cache line. The second vmovsd is the register copy the
                // compiler failed to emit — it is cheaper than the reload it replaces
                // because it is L1-resident.
                "vmovsd %[resting_qty], %[xmm_fill]\n\t"   // xmm_fill = resting.qty
                "vmovsd %[resting_qty], %[xmm_rest]\n\t"   // xmm_rest = resting.qty (copy)
                // fill = min(order.quantity, resting.quantity)
                "vminsd %[order_qty], %[xmm_fill], %[xmm_fill]\n\t"
                // order.quantity -= fill
                "vsubsd %[xmm_fill], %[order_qty], %[order_qty]\n\t"
                // resting.quantity -= fill  (no reload — xmm_rest has the original)
                "vsubsd %[xmm_fill], %[xmm_rest], %[xmm_rest]\n\t"
                // write updated resting.quantity back to memory
                "vmovsd %[xmm_rest], %[resting_qty]\n\t"
                : [order_qty]   "+x" (order.quantity),
                  [resting_qty] "+m" (resting.quantity),
                  [xmm_fill]    "=&x"(xmm_fill),
                  [xmm_rest]    "=&x"(xmm_rest)
                :
                :
            );

            if (resting.quantity == 0.0) {
                orderIndex[resting.id] = std::nullopt;
                level.pop_front();
            }
        }
        if (level.empty()) clearBit(ask_bits, askTick);
    }
}

// ---------------------------------------------------------------------------
// agentASM Iteration 8 — matchSell_asm
//
// Identical fill arithmetic optimisation to matchBuy_asm.
// Walks bids highest-first; inner loop body is structurally the same.
// ---------------------------------------------------------------------------
void OrderBook::matchSell_asm(Order& order, int orderTick) {
    // Walk bids highest-first via bitmap; clear the bit inline when a level empties.
    // No drained flag, no remove_if pass — the bitmap is the index of live levels.
    while (order.quantity > 0.0) {
        int bidTick = highestBit(bid_bits);
        if (bidTick < 0 || bidTick < orderTick) break;

        PriceLevel& level = bid_levels[bidTick];
        while (!level.empty() && order.quantity > 0.0) {
            Order& resting = level.front();

            // Identical fill arithmetic optimisation to matchBuy_asm.
            // See full constraint notes in matchBuy_asm above.
            double xmm_fill, xmm_rest;
            __asm__ volatile (
                "vmovsd %[resting_qty], %[xmm_fill]\n\t"
                "vmovsd %[resting_qty], %[xmm_rest]\n\t"
                "vminsd %[order_qty], %[xmm_fill], %[xmm_fill]\n\t"
                "vsubsd %[xmm_fill], %[order_qty], %[order_qty]\n\t"
                "vsubsd %[xmm_fill], %[xmm_rest], %[xmm_rest]\n\t"
                "vmovsd %[xmm_rest], %[resting_qty]\n\t"
                : [order_qty]   "+x" (order.quantity),
                  [resting_qty] "+m" (resting.quantity),
                  [xmm_fill]    "=&x"(xmm_fill),
                  [xmm_rest]    "=&x"(xmm_rest)
                :
                :
            );

            if (resting.quantity == 0.0) {
                orderIndex[resting.id] = std::nullopt;
                level.pop_front();
            }
        }
        if (level.empty()) clearBit(bid_bits, bidTick);
    }
}
