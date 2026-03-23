/* loader.hpp — CSV event loader for the ETH/USDT L2 orderbook benchmark.
 *
 * Reads events.csv, parses each row, returns a std::vector<Event>.
 * The loader is not timed. It runs once before any benchmark loop.
 *
 * CSV format: event_type,side,tick,qty
 *   - event_type: UPSERT or DELETE
 *   - side:       BID or ASK
 *   - tick:       plain unsigned integer (e.g. 300000), no float
 *   - qty:        scaled uint64_t (10^8 units); 0 for DELETE
 *
 * Build flags: -std=c++17 -O2 -march=native -Wall -Wextra
 *              -Wconversion -Wsign-conversion -Werror -fno-exceptions
 */

#pragma once

#include "book.hpp"

#include <cstdint>
#include <vector>

namespace eth::loader {

// ---------------------------------------------------------------------------
// EventType
// ---------------------------------------------------------------------------

enum class EventType : uint8_t { UPSERT = 0, DELETE = 1 };

// ---------------------------------------------------------------------------
// Event — in-memory representation of one CSV row.
//
// Layout (as specified in the task brief):
//   tick      at offset  0 — 4 bytes (uint32_t absolute tick)
//   _tick_pad at offset  4 — 4 bytes (explicit pad to push qty to offset 8)
//   qty       at offset  8 — 8 bytes (uint64_t scaled qty; 0 for DELETE)
//   side      at offset 16 — 1 byte  (eth::book::side_t)
//   type      at offset 17 — 1 byte  (EventType)
//   _pad      at offset 18 — 6 bytes (pad to 24-byte total)
//
// Total: 24 bytes
// ---------------------------------------------------------------------------

struct Event {
    eth::book::tick_t  tick;      // offset  0, 4 bytes — absolute tick
    uint32_t           _tick_pad; // offset  4, 4 bytes — explicit pad
    eth::book::qty_t   qty;       // offset  8, 8 bytes — 0 for DELETE
    eth::book::side_t  side;      // offset 16, 1 byte
    EventType          type;      // offset 17, 1 byte
    uint8_t            _pad[6];   // offset 18, 6 bytes — pad to 24 bytes
};                                 // total: 24 bytes

static_assert(sizeof(Event)  == 24U, "Event layout changed");
static_assert(alignof(Event) ==  8U, "Event alignment changed");

// ---------------------------------------------------------------------------
// load_csv — parse events.csv and return the event vector.
//
// path: absolute or relative path to the CSV file.
// Returns an empty vector on I/O or parse error (caller should check).
// [[nodiscard]]: discarding the result means no events are benchmarked.
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<Event> load_csv(const char* path);

} // namespace eth::loader
