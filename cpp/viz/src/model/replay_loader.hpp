/* replay_loader.hpp — load_replay_csv() declaration
 *
 * Reads a CSV file with columns: timestamp_ns,event_type,side,tick,qty
 * Returns a vector<ReplayEvent> sorted ascending by timestamp_ns.
 *
 * On file open failure or any parse error: affected rows are skipped;
 * returns empty vector only if file cannot be opened.
 *
 * Includes: <vector>, replay_event.hpp only.
 * No float arithmetic. No exceptions.
 * Namespace: viz::model
 */

#pragma once

#include <vector>
#include "replay_event.hpp"

namespace viz::model {

// Load and parse a replay CSV file.
// Returns sorted vector of ReplayEvent; empty if file cannot be opened.
[[nodiscard]] std::vector<ReplayEvent> load_replay_csv(const char* path);

} // namespace viz::model
