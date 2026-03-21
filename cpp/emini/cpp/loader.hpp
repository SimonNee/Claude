/* loader.hpp — CSV event loader for the E-mini order book benchmark.
 *
 * Reads orders.csv, parses each row, and returns a std::vector<Event>.
 * The loader is not timed. It runs once before any benchmark loop.
 *
 * CSV format (as of current generator): event_type,side,tick,quantity,ref_idx
 *   - The third column is a plain integer tick (e.g. 4391), not a float price.
 *   - ADD and MATCH rows: tick is read directly as uint32_t and validated
 *     in [0, MAX_TICKS-1]. No float arithmetic occurs anywhere in the loader.
 *   - CANCEL rows: tick=0 in the CSV; stored as zero in the event.
 *
 * Build flags: -std=c++17 -O2 -march=native -Wall -Wextra
 *              -Wconversion -Wsign-conversion -Werror -fno-exceptions
 */

#pragma once

#include "book.hpp"

#include <cstdint>
#include <vector>

namespace es::loader {

// ---------------------------------------------------------------------------
// EventType
// ---------------------------------------------------------------------------

enum class EventType : uint8_t { ADD = 0, CANCEL, MATCH };

// ---------------------------------------------------------------------------
// Event — in-memory representation of one CSV row.
//
// Layout: type(0) side(1) _pad[2](2) tick(4) qty(8) ref_idx(12)
// 16 bytes, 4-byte aligned.
// ---------------------------------------------------------------------------

struct Event {
    EventType          type;     // offset  0
    es::book::side_t   side;     // offset  1
    uint8_t            _pad[2];  // offset  2 — explicit pad to align tick to 4
    es::book::tick_t   tick;     // offset  4 — pre-converted; 0 for CANCEL
    es::book::qty_t    qty;      // offset  8 — 0 for CANCEL
    uint32_t           ref_idx;  // offset 12 — ADD row index for CANCEL; 0 otherwise
};                               // total: 16 bytes

static_assert(sizeof(Event)  == 16, "Event layout changed");
static_assert(alignof(Event) ==  4, "Event alignment changed");

// ---------------------------------------------------------------------------
// load_csv — parse orders.csv and return the event vector.
//
// path: absolute or relative path to the CSV file.
// Returns an empty vector on I/O or parse error (caller should check).
// [[nodiscard]]: discarding the result means no events are benchmarked.
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<Event> load_csv(const char* path);

} // namespace es::loader
