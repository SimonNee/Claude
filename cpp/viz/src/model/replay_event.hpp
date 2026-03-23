/* replay_event.hpp — ReplayEvent: in-memory parsed form of one replay CSV row
 *
 * This struct is also the element type produced by OUEventSource.
 * It has no dependency on book.hpp or any other model header.
 *
 * side is stored as uint8_t rather than eth::book::side_t so this header
 * remains free of book.hpp.  Conversion to eth::book::side_t occurs in
 * SimEngine::dispatch_event() — the single conversion site.
 *
 * Includes: <cstdint> only.
 * Namespace: viz::model
 */

#pragma once

#include <cstdint>

namespace viz::model {

// ---------------------------------------------------------------------------
// ReplayEvent  (spec: Data Model / ReplayEvent)
//
// Layout:
//   timestamp_ns  offset  0 — virtual clock target; nanoseconds
//   tick          offset  8 — absolute price tick
//   _pad          offset 12 — explicit pad
//   qty           offset 16 — scaled qty (10^8); 0 = delete level
//   side          offset 24 — 0 = BID, 1 = ASK
//   _pad2[7]      offset 25 — pad to 32 bytes
//                 total: 32 bytes
// ---------------------------------------------------------------------------

struct ReplayEvent {
    uint64_t  timestamp_ns;  // offset  0 — virtual clock target; nanoseconds
    uint32_t  tick;          // offset  8 — absolute price tick
    uint32_t  _pad;          // offset 12 — explicit pad
    uint64_t  qty;           // offset 16 — scaled qty (10^8); 0 = delete level
    uint8_t   side;          // offset 24 — 0 = BID, 1 = ASK
    uint8_t   _pad2[7];      // offset 25 — pad to 32 bytes
};                           // total: 32 bytes

static_assert(sizeof(ReplayEvent)  == 32U, "ReplayEvent layout changed");
static_assert(alignof(ReplayEvent) ==  8U, "ReplayEvent alignment changed");

} // namespace viz::model
