/* ou_event_source.hpp — OUEventSource declaration
 *
 * Pre-bakes synthetic OU-process events in the constructor.
 * Implements IEventSource; also provides save_csv() for exporting.
 *
 * Includes: event_source.hpp, <vector>, <cstddef>, <cstdint> only.
 * No ImGui, no book.hpp.
 * Namespace: viz::model
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "event_source.hpp"

namespace viz::model {

// ---------------------------------------------------------------------------
// OUParams — Ornstein-Uhlenbeck process parameters
// ---------------------------------------------------------------------------

struct OUParams {
    double   mu;               // mean reversion level; default 2000.0 (ETH price)
    double   theta;            // mean reversion speed; default 0.005
    double   sigma;            // volatility; default 10.0
    double   drift;            // deterministic drift per step; default 0.0
    double   tick_size;        // minimum price increment; default 0.01 (ETH/USDT)
    double   base_price;       // window base price; default 0.0
    uint32_t ticks_per_dollar; // tick count per dollar; default 100
    uint32_t max_tick;         // maximum absolute tick; default 65535
    uint32_t cancels_per_add;  // cancel events per add event; default 10
    uint32_t seed;             // PRNG seed; default 42
};

// Default OUParams — used when no override is needed.
static constexpr OUParams OU_DEFAULT_PARAMS = {
    2000.0,   // mu
    0.005,    // theta
    10.0,     // sigma
    0.0,      // drift
    0.01,     // tick_size
    0.0,      // base_price
    100U,     // ticks_per_dollar
    65535U,   // max_tick
    10U,      // cancels_per_add
    42U       // seed
};

// Default event count: one synthetic trading day at 1 ms/event.
static constexpr std::size_t OU_DEFAULT_EVENT_COUNT = 86400U;

// ---------------------------------------------------------------------------
// OUEventSource
// ---------------------------------------------------------------------------

class OUEventSource final : public IEventSource {
public:
    // Pre-bakes event_count synthetic events using the given params.
    // If any param precondition is violated, events_ is left empty.
    explicit OUEventSource(OUParams params,
                           std::size_t event_count = OU_DEFAULT_EVENT_COUNT);

    // IEventSource interface
    bool        next_event(ReplayEvent& out) override;
    void        reset() override;
    std::size_t event_count() const override;

    // Save baked events to CSV in replay format.
    // Returns true on success, false if file cannot be opened.
    [[nodiscard]] bool save_csv(const char* path) const;

private:
    std::vector<ReplayEvent> events_;
    std::size_t              cursor_;

    // Pre-bake implementation — called once from constructor.
    void bake(OUParams params, std::size_t n_events);
};

} // namespace viz::model
