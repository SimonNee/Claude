/* csv_event_source.cpp — CsvEventSource implementation
 *
 * Iterates a pre-loaded vector<ReplayEvent> in order.
 * Pitfall 7: vector taken by value in constructor — caller moves it in;
 *            no copy on the construction path.
 */

#include "csv_event_source.hpp"

namespace viz::model {

CsvEventSource::CsvEventSource(std::vector<ReplayEvent> events)
    : events_(std::move(events))
    , cursor_(0U)
{}

bool CsvEventSource::next_event(ReplayEvent& out) {
    if (cursor_ >= events_.size()) {
        return false;
    }
    out = events_[cursor_];
    ++cursor_;
    return true;
}

void CsvEventSource::reset() {
    cursor_ = 0U;
}

std::size_t CsvEventSource::event_count() const {
    return events_.size();
}

} // namespace viz::model
