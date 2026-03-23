/* csv_event_source.hpp — CsvEventSource declaration
 *
 * Wraps a pre-loaded vector<ReplayEvent> and iterates it via IEventSource.
 * The vector is moved in at construction — no copy.
 *
 * Includes: event_source.hpp, <vector> only.
 * No ImGui, no book.hpp.
 * Namespace: viz::model
 */

#pragma once

#include <vector>
#include "event_source.hpp"

namespace viz::model {

// ---------------------------------------------------------------------------
// CsvEventSource
// ---------------------------------------------------------------------------

class CsvEventSource final : public IEventSource {
public:
    // Takes ownership of events via move.
    explicit CsvEventSource(std::vector<ReplayEvent> events);

    // IEventSource interface
    bool        next_event(ReplayEvent& out) override;
    void        reset() override;
    std::size_t event_count() const override;

private:
    std::vector<ReplayEvent> events_;
    std::size_t              cursor_;
};

} // namespace viz::model
