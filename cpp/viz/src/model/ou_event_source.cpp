/* ou_event_source.cpp — OUEventSource implementation
 *
 * Pre-bakes synthetic OU-process events.  Algorithm matches generate.q.
 *
 * Box-Muller: u1 = max(1e-10, U(0,1)); z1 = sqrt(-2*log(u1))*cos(2*pi*u2)
 *                                        z2 = sqrt(-2*log(u1))*sin(2*pi*u2)
 * OU step:    P[t+1] = P[t] + theta*(mu - P[t]) + drift + sigma*Z[t]
 * Snap:       snapped = tick_size * floor(0.5 + price/tick_size)
 * Clamp:      [base_price, base_price + max_tick*tick_size]
 * Tick:       static_cast<uint32_t>(floor((price-base_price)*ticks_per_dollar+0.5))
 *
 * No exceptions. -fno-exceptions compliant.
 * Pitfall 7: no inadvertent copies; vector populated in place.
 */

#include "ou_event_source.hpp"

#include <cmath>
#include <cstdio>
#include <random>

namespace viz::model {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// pi: computed as acos(-1.0) per spec to avoid M_PI portability issues.
static const double PI = std::acos(-1.0);

// snap price to nearest tick_size grid.
[[nodiscard]] static double snap_price(double price, double tick_size) {
    return tick_size * std::floor(0.5 + price / tick_size);
}

// clamp price within [lo, hi].
[[nodiscard]] static double clamp_price(double price, double lo, double hi) {
    if (price < lo) { return lo; }
    if (price > hi) { return hi; }
    return price;
}

// convert price to absolute tick index.
[[nodiscard]] static uint32_t price_to_tick(double price,
                                             double base_price,
                                             uint32_t ticks_per_dollar) {
    // Sanctioned boundary cast: double -> uint32_t after floor+0.5 rounding.
    double raw = std::floor((price - base_price) * static_cast<double>(ticks_per_dollar) + 0.5);
    if (raw < 0.0) { return 0U; }
    if (raw > 65535.0) { return 65535U; }
    return static_cast<uint32_t>(raw);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// OUEventSource constructor
// ---------------------------------------------------------------------------

OUEventSource::OUEventSource(OUParams params, std::size_t n_events)
    : cursor_(0U)
{
    bake(params, n_events);
}

// ---------------------------------------------------------------------------
// bake — pre-generate all events
// ---------------------------------------------------------------------------

void OUEventSource::bake(OUParams params, std::size_t n_events) {
    // Param precondition checks — leave events_ empty if violated.
    if (params.tick_size <= 0.0) { return; }
    if (params.ticks_per_dollar == 0U) { return; }
    if (params.max_tick == 0U) { return; }
    if (n_events == 0U) { return; }

    events_.reserve(n_events);

    // PRNG — mt19937_64 seeded with params.seed.
    std::mt19937_64 rng(static_cast<uint64_t>(params.seed));

    // Uniform [0,1) distribution.
    std::uniform_real_distribution<double> udist(0.0, 1.0);
    // Uniform integer distributions for qty.
    std::uniform_int_distribution<uint32_t> qty_small(1U, 5U);
    std::uniform_int_distribution<uint32_t> qty_large(6U, 50U);
    std::uniform_int_distribution<uint32_t> qty_match(1U, 20U);

    const double price_lo = params.base_price;
    const double price_hi = params.base_price + static_cast<double>(params.max_tick) * params.tick_size;

    // Number of ADD events.
    // adds = n_events / (1 + cancels_per_add + 1)
    std::size_t denom = static_cast<std::size_t>(1U + params.cancels_per_add + 1U);
    std::size_t adds  = n_events / denom;
    if (adds == 0U) { return; }

    // Generate Box-Muller z-values for ADD events.
    // We need one z per add; allocate pairs (z1, z2) covering adds.
    // z1 used for ADD; z2 stored for next step.
    double price = params.mu;   // current OU price; start at mu

    // Timestamp base: 1ms per event slot.
    // Layout per "block" (1 add + cancels_per_add cancels + 1 match):
    //   Timestamps: block_start + [0 .. denom-1] * 1'000'000 ns
    // We assign timestamps using a monotone counter.

    uint64_t ts_ns = 0U;   // monotone timestamp counter

    // We use a local variable for the second Box-Muller z to avoid recomputing.
    bool z2_ready = false;
    double z2_saved = 0.0;

    for (std::size_t add_i = 0U; add_i < adds; ++add_i) {
        // --- OU step for mid price ---
        double z1 = 0.0;
        if (z2_ready) {
            z1 = z2_saved;
            z2_ready = false;
        } else {
            double u1 = udist(rng);
            if (u1 < 1e-10) { u1 = 1e-10; }
            double u2 = udist(rng);
            z1 = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * PI * u2);
            z2_saved = std::sqrt(-2.0 * std::log(u1)) * std::sin(2.0 * PI * u2);
            z2_ready = true;
        }

        price = price + params.theta * (params.mu - price) + params.drift + params.sigma * z1;
        double snapped = snap_price(price, params.tick_size);
        snapped = clamp_price(snapped, price_lo, price_hi);

        uint32_t mid_tick = price_to_tick(snapped, params.base_price, params.ticks_per_dollar);

        // BID = mid_tick - 1 (even index), ASK = mid_tick + 1 (odd index)
        // Guard against underflow/overflow.
        uint32_t bid_tick = (mid_tick > 0U) ? (mid_tick - 1U) : 0U;
        uint32_t ask_tick = (mid_tick < params.max_tick) ? (mid_tick + 1U) : params.max_tick;

        // ADD event (alternating BID/ASK: even add_i = BID, odd = ASK)
        uint8_t add_side = (add_i % 2U == 0U) ? uint8_t(0U) : uint8_t(1U);
        uint32_t add_tick = (add_side == 0U) ? bid_tick : ask_tick;

        // Qty draw
        double u_qty = udist(rng);
        uint64_t qty_raw = 0U;
        if (u_qty < 0.70) {
            qty_raw = static_cast<uint64_t>(qty_small(rng));
        } else {
            qty_raw = static_cast<uint64_t>(qty_large(rng));
        }
        uint64_t qty = qty_raw * 100000000ULL;

        {
            ReplayEvent ev{};
            ev.timestamp_ns = ts_ns;
            ev.tick         = add_tick;
            ev._pad         = 0U;
            ev.qty          = qty;
            ev.side         = add_side;
            events_.push_back(ev);
        }
        ts_ns += 1000000ULL;

        // CANCEL events (qty=0, same side, tick=0)
        for (uint32_t c = 0U; c < params.cancels_per_add; ++c) {
            ReplayEvent ev{};
            ev.timestamp_ns = ts_ns;
            ev.tick         = 0U;
            ev._pad         = 0U;
            ev.qty          = 0U;    // qty=0 → delete
            ev.side         = add_side;
            events_.push_back(ev);
            ts_ns += 1000000ULL;
        }

        // MATCH event: independent OU price step, alternating BID/ASK
        double z_match = 0.0;
        if (z2_ready) {
            z_match = z2_saved;
            z2_ready = false;
        } else {
            double u1m = udist(rng);
            if (u1m < 1e-10) { u1m = 1e-10; }
            double u2m = udist(rng);
            z_match = std::sqrt(-2.0 * std::log(u1m)) * std::cos(2.0 * PI * u2m);
            z2_saved = std::sqrt(-2.0 * std::log(u1m)) * std::sin(2.0 * PI * u2m);
            z2_ready = true;
        }

        double match_price = price + params.theta * (params.mu - price)
                             + params.drift + params.sigma * z_match;
        double match_snapped = snap_price(match_price, params.tick_size);
        match_snapped = clamp_price(match_snapped, price_lo, price_hi);
        uint32_t match_tick = price_to_tick(match_snapped, params.base_price,
                                            params.ticks_per_dollar);

        uint8_t match_side = (add_i % 2U == 0U) ? uint8_t(1U) : uint8_t(0U);
        uint64_t match_qty = static_cast<uint64_t>(qty_match(rng)) * 100000000ULL;

        {
            ReplayEvent ev{};
            ev.timestamp_ns = ts_ns;
            ev.tick         = match_tick;
            ev._pad         = 0U;
            ev.qty          = match_qty;
            ev.side         = match_side;
            events_.push_back(ev);
        }
        ts_ns += 1000000ULL;

        // Stop if we have filled n_events (due to denom truncation we may
        // produce slightly fewer; that is acceptable).
        if (events_.size() >= n_events) {
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// IEventSource interface
// ---------------------------------------------------------------------------

bool OUEventSource::next_event(ReplayEvent& out) {
    if (cursor_ >= events_.size()) {
        return false;
    }
    out = events_[cursor_];
    ++cursor_;
    return true;
}

void OUEventSource::reset() {
    cursor_ = 0U;
}

std::size_t OUEventSource::event_count() const {
    return events_.size();
}

// ---------------------------------------------------------------------------
// save_csv — write events_ to CSV in replay format
// ---------------------------------------------------------------------------

bool OUEventSource::save_csv(const char* path) const {
    std::FILE* fp = std::fopen(path, "w");
    if (fp == nullptr) {
        return false;
    }

    std::fprintf(fp, "timestamp_ns,event_type,side,tick,qty\n");

    for (const ReplayEvent& ev : events_) {
        const char* evtype = (ev.qty == 0U) ? "DELETE" : "UPSERT";
        const char* side   = (ev.side == 0U) ? "BID" : "ASK";
        std::fprintf(fp, "%llu,%s,%s,%u,%llu\n",
            static_cast<unsigned long long>(ev.timestamp_ns),
            evtype,
            side,
            static_cast<unsigned int>(ev.tick),
            static_cast<unsigned long long>(ev.qty));
    }

    std::fclose(fp);
    return true;
}

} // namespace viz::model
