/* loader.cpp — CSV event loader implementation for the ETH/USDT L2 orderbook.
 *
 * Parses events.csv into a std::vector<Event>.
 * Not on the hot path. No RDTSC. No benchmark sink needed here.
 *
 * CSV format: event_type,side,tick,qty  (4 fields, no ref_idx)
 *   - event_type: UPSERT or DELETE
 *   - tick:       plain unsigned integer — read as uint32_t with %u
 *   - qty:        scaled uint64_t — read as unsigned long long with %llu
 *
 * Validation:
 *   - tick > 0 (all valid ETH ticks are > 0)
 *   - UPSERT rows must have qty > 0
 *   - DELETE rows must have qty == 0
 *
 * Build flags: -std=c++17 -O2 -march=native -Wall -Wextra
 *              -Wconversion -Wsign-conversion -Werror -fno-exceptions
 */

#include "loader.hpp"
#include "book.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace eth::loader {

// ---------------------------------------------------------------------------
// parse_side — map CSV side string to eth::book::side_t.
// Returns true on success; false on unrecognised token.
// ---------------------------------------------------------------------------

static bool parse_side(const char* s, eth::book::side_t& out) noexcept {
    if (s[0] == 'B' && s[1] == 'I' && s[2] == 'D' && s[3] == '\0') {
        out = eth::book::side_t::BID;
        return true;
    }
    if (s[0] == 'A' && s[1] == 'S' && s[2] == 'K' && s[3] == '\0') {
        out = eth::book::side_t::ASK;
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

    // Pre-allocate for 1,000,000 events (the expected CSV size).
    // reserve() eliminates all reallocations on push_back (Pitfall 9).
    std::vector<Event> events;
    events.reserve(1'000'000U);

    // Line buffer — longest possible row:
    //   "UPSERT,BID,10000000,100000000000\n" ~ 36 chars; 256 is ample.
    char line[256];

    // Skip header row.
    if (!std::fgets(line, static_cast<int>(sizeof(line)), f)) {
        std::fclose(f);
        return {};
    }

    uint32_t row = 0U;  // 0-based data row index (header not counted)

    while (std::fgets(line, static_cast<int>(sizeof(line)), f)) {
        // Strip trailing newline / carriage-return.
        char* nl = std::strrchr(line, '\n');
        if (nl) { *nl = '\0'; }
        char* cr = std::strrchr(line, '\r');
        if (cr) { *cr = '\0'; }

        // Empty line — skip silently.
        if (line[0] == '\0') {
            continue;
        }

        // Parse: event_type, side, tick, qty
        // %6s  — max 6 chars + NUL for type_str (longest: "UPSERT" = 6 chars)
        // %3s  — max 3 chars + NUL for side_str (longest: "ASK" / "BID")
        // %u   — tick is a plain unsigned integer
        // %llu — qty is a 64-bit unsigned integer (unsigned long long on all platforms)
        char              type_str[8];
        char              side_str[4];
        uint32_t          tick_u  = 0U;
        unsigned long long qty_ull = 0ULL;

        int parsed = std::sscanf(line, "%6[^,],%3[^,],%u,%llu",
                                 type_str, side_str,
                                 &tick_u, &qty_ull);
        if (parsed != 4) {
            std::fprintf(stderr,
                         "loader: parse error at data row %u (sscanf returned %d): '%s'\n",
                         row, parsed, line);
            std::fclose(f);
            return {};
        }

        // Resolve event type.
        EventType ev_type;
        if (type_str[0] == 'U') {
            // "UPSERT"
            ev_type = EventType::UPSERT;
        } else if (type_str[0] == 'D') {
            // "DELETE"
            ev_type = EventType::DELETE;
        } else {
            std::fprintf(stderr,
                         "loader: unknown event type '%s' at data row %u\n",
                         type_str, row);
            std::fclose(f);
            return {};
        }

        eth::book::side_t ev_side = eth::book::side_t::BID;
        if (!parse_side(side_str, ev_side)) {
            std::fprintf(stderr,
                         "loader: unknown side '%s' at data row %u\n",
                         side_str, row);
            std::fclose(f);
            return {};
        }

        // Convert parsed fields to their canonical types.
        // qty_ull is unsigned long long; uint64_t is the same width on Linux x86-64.
        // The assignment is well-defined: both are 64-bit unsigned types.
        eth::book::tick_t tick_val = tick_u;
        eth::book::qty_t  qty_val  = static_cast<eth::book::qty_t>(qty_ull);

        // Validate tick: all valid ETH ticks are > 0.
        if (tick_val == 0U) {
            std::fprintf(stderr,
                         "loader: zero tick at data row %u\n",
                         row);
            std::fclose(f);
            return {};
        }

        // Validate qty against event type.
        if (ev_type == EventType::UPSERT && qty_val == 0U) {
            std::fprintf(stderr,
                         "loader: UPSERT with zero qty at data row %u\n",
                         row);
            std::fclose(f);
            return {};
        }
        if (ev_type == EventType::DELETE && qty_val != 0U) {
            std::fprintf(stderr,
                         "loader: DELETE with non-zero qty at data row %u\n",
                         row);
            std::fclose(f);
            return {};
        }

        // Build event. Zero all pad bytes explicitly — no uninitialised fields.
        Event ev;
        ev.tick      = tick_val;
        ev._tick_pad = 0U;
        ev.qty       = qty_val;
        ev.side      = ev_side;
        ev.type      = ev_type;
        ev._pad[0]   = 0U;
        ev._pad[1]   = 0U;
        ev._pad[2]   = 0U;
        ev._pad[3]   = 0U;
        ev._pad[4]   = 0U;
        ev._pad[5]   = 0U;

        events.push_back(ev);
        ++row;
    }

    std::fclose(f);

    std::fprintf(stdout,
                 "loader: read %u events from '%s'\n",
                 static_cast<unsigned>(events.size()), path);

    return events;
}

} // namespace eth::loader
