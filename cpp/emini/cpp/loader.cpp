/* loader.cpp — CSV event loader implementation.
 *
 * Parses orders.csv into a std::vector<Event>.
 * Not on the hot path. No RDTSC. No benchmark sink needed here.
 *
 * Build flags: -std=c++17 -O2 -march=native -Wall -Wextra
 *              -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *
 * Parsing strategy:
 *   - fgets line-by-line into a stack buffer (no heap per line).
 *   - sscanf for structured field extraction.
 *   - The tick column is a plain integer — read directly as uint32_t.
 *     No float arithmetic in the loader. No price_to_tick() call.
 *   - Non-zero ticks validated in [0, MAX_TICKS-1] on ADD/MATCH rows.
 *   - CANCEL rows: tick=0 in CSV; stored as zero.
 */

#include "loader.hpp"
#include "book.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace es::loader {

// ---------------------------------------------------------------------------
// parse_side — map CSV side string to side_t.
// Returns true on success; false on unrecognised token.
// ---------------------------------------------------------------------------

static bool parse_side(const char* s, es::book::side_t& out) noexcept {
    if (s[0] == 'B' && s[1] == 'I' && s[2] == 'D' && s[3] == '\0') {
        out = es::book::side_t::BID;
        return true;
    }
    if (s[0] == 'A' && s[1] == 'S' && s[2] == 'K' && s[3] == '\0') {
        out = es::book::side_t::ASK;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// load_csv
// ---------------------------------------------------------------------------

std::vector<Event> load_csv(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::fprintf(stderr, "loader: cannot open '%s'\n", path);
        return {};
    }

    // Pre-allocate for 1,000,000 events (the known CSV size).
    // reserve() eliminates all reallocations (Pitfall 9).
    std::vector<Event> events;
    events.reserve(1'000'000U);

    // Line buffer — longest possible row:
    //   "CANCEL,ASK,0,50,999999\n" = ~24 chars; 256 is ample.
    char line[256];

    // Skip header row.
    if (!std::fgets(line, static_cast<int>(sizeof(line)), f)) {
        std::fclose(f);
        return {};
    }

    uint32_t row = 0U;  // 0-based row index (header not counted)

    while (std::fgets(line, static_cast<int>(sizeof(line)), f)) {
        // Strip trailing newline/carriage-return.
        // strrchr approach avoids any implicit type issues.
        char* nl = std::strrchr(line, '\n');
        if (nl) { *nl = '\0'; }
        char* cr = std::strrchr(line, '\r');
        if (cr) { *cr = '\0'; }

        // Empty line — skip silently.
        if (line[0] == '\0') {
            continue;
        }

        // Parse: event_type, side, tick, quantity, ref_idx
        // Fields are comma-separated. Use sscanf with %3s / %3s for
        // the two enum-like string fields (max 6 chars: "CANCEL").
        char     type_str[8];
        char     side_str[4];
        uint32_t tick_u;
        uint32_t qty_u;
        uint32_t ref_u;

        // %7s — max 7 chars + NUL for type_str (longest: "CANCEL" = 6 chars)
        // %3s — max 3 chars + NUL for side_str (longest: "ASK" / "BID")
        // %u  — tick column is now a plain unsigned integer, not a float
        int parsed = std::sscanf(line, "%7[^,],%3[^,],%u,%u,%u",
                                 type_str, side_str,
                                 &tick_u, &qty_u, &ref_u);
        if (parsed != 5) {
            std::fprintf(stderr,
                         "loader: parse error at data row %u (sscanf returned %d): '%s'\n",
                         row, parsed, line);
            std::fclose(f);
            return {};
        }

        es::book::side_t side = es::book::side_t::BID;
        if (!parse_side(side_str, side)) {
            std::fprintf(stderr,
                         "loader: unknown side '%s' at data row %u\n",
                         side_str, row);
            std::fclose(f);
            return {};
        }

        Event ev;
        ev.side    = side;
        ev._pad[0] = 0U;
        ev._pad[1] = 0U;
        ev.ref_idx = ref_u;

        if (type_str[0] == 'A') {
            // ADD — tick comes directly from the CSV as a plain integer.
            // Validate range [0, MAX_TICKS-1]; tick=0 is a valid bid at the floor.
            if (tick_u >= es::book::MAX_TICKS) {
                std::fprintf(stderr,
                             "loader: tick %u out of range at data row %u\n",
                             tick_u, row);
                std::fclose(f);
                return {};
            }
            ev.type = EventType::ADD;
            ev.tick = tick_u;
            ev.qty  = qty_u;
        } else if (type_str[0] == 'C') {
            // CANCEL — tick=0 in the CSV; qty is unused for cancel.
            ev.type = EventType::CANCEL;
            ev.tick = 0U;
            ev.qty  = 0U;
        } else if (type_str[0] == 'M') {
            // MATCH — tick comes directly from the CSV as a plain integer.
            // Validate range [0, MAX_TICKS-1]; tick=0 is technically valid.
            if (tick_u >= es::book::MAX_TICKS) {
                std::fprintf(stderr,
                             "loader: match tick %u out of range at data row %u\n",
                             tick_u, row);
                std::fclose(f);
                return {};
            }
            ev.type = EventType::MATCH;
            ev.tick = tick_u;
            ev.qty  = qty_u;
        } else {
            std::fprintf(stderr,
                         "loader: unknown event type '%s' at data row %u\n",
                         type_str, row);
            std::fclose(f);
            return {};
        }

        events.push_back(ev);
        ++row;
    }

    std::fclose(f);

    std::fprintf(stdout,
                 "loader: read %u events from '%s'\n",
                 static_cast<unsigned>(events.size()), path);

    return events;
}

} // namespace es::loader
