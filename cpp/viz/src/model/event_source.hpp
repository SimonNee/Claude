/* event_source.hpp — IEventSource: abstract interface for event producers
 *
 * Consumed by SimEngine via a non-owning pointer.  Concrete implementations:
 *   CsvEventSource — wraps a pre-loaded vector<ReplayEvent>
 *   OUEventSource  — pre-bakes synthetic OU-process events
 *
 * Includes: replay_event.hpp, <cstddef> only.
 * No ImGui, no book.hpp, no STL containers.
 * Namespace: viz::model
 */

#pragma once

#include <cstddef>
#include "replay_event.hpp"

namespace viz::model {

// ---------------------------------------------------------------------------
// IEventSource — event producer abstraction
//
// next_event() returns false when the source is exhausted.
// reset()      rewinds the source to the beginning.
// event_count() returns the total number of events available.
// ---------------------------------------------------------------------------

class IEventSource {
public:
    virtual ~IEventSource() = default;

    // Returns true and copies the next event into out; false when exhausted.
    virtual bool next_event(ReplayEvent& out) = 0;

    // Rewind to beginning.
    virtual void reset() = 0;

    // Total events available (constant after construction).
    virtual std::size_t event_count() const = 0;
};

} // namespace viz::model
