/* replay_loader.cpp — load_replay_csv() implementation
 *
 * CSV columns: timestamp_ns,event_type,side,tick,qty
 *   event_type: UPSERT passes qty through; DELETE forces qty=0
 *   side:       BID → uint8_t(0); ASK → uint8_t(1); other → skip row
 *
 * No float arithmetic. No exceptions. -fno-exceptions compliant.
 * Pitfall 7: no inadvertent copies; vector returned by value (NRVO).
 */

#include "replay_loader.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace viz::model {

namespace {

// ---------------------------------------------------------------------------
// parse_u64 — parse unsigned 64-bit integer from a null-terminated field.
// Returns false if the string is empty or contains a non-digit character.
// ---------------------------------------------------------------------------

[[nodiscard]] static bool parse_u64(const char* s, uint64_t& out) {
    if (s == nullptr || *s == '\0') {
        return false;
    }
    uint64_t v = 0U;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        v = v * 10U + static_cast<uint64_t>(*p - '0');
    }
    out = v;
    return true;
}

// ---------------------------------------------------------------------------
// parse_u32 — parse unsigned 32-bit integer from a null-terminated field.
// ---------------------------------------------------------------------------

[[nodiscard]] static bool parse_u32(const char* s, uint32_t& out) {
    uint64_t v = 0U;
    if (!parse_u64(s, v)) {
        return false;
    }
    if (v > 0xFFFFFFFFU) {
        return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

// ---------------------------------------------------------------------------
// tokenise_line — split a line buffer by commas into up to max_fields tokens.
// Modifies the buffer in place (replaces commas with '\0').
// Returns the number of tokens found.
// ---------------------------------------------------------------------------

static int tokenise_line(char* line, char* fields[], int max_fields) {
    int count = 0;
    fields[count++] = line;
    for (char* p = line; *p != '\0' && *p != '\n' && *p != '\r'; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (count < max_fields) {
                fields[count++] = p + 1;
            }
        }
    }
    // Strip trailing newline from the last field
    char* last = fields[count - 1];
    for (char* p = last; *p != '\0'; ++p) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            break;
        }
    }
    return count;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// load_replay_csv
// ---------------------------------------------------------------------------

std::vector<ReplayEvent> load_replay_csv(const char* path) {
    std::vector<ReplayEvent> events;

    std::FILE* fp = std::fopen(path, "r");
    if (fp == nullptr) {
        return events;
    }

    // Reserve a reasonable initial capacity to avoid repeated reallocations.
    // Pitfall 9: reserve pre-known upper bound if known; here we do not know
    // the file size, so reserve a modest initial amount.
    events.reserve(4096U);

    char line[512];
    bool header_skipped = false;

    while (std::fgets(line, static_cast<int>(sizeof(line)), fp) != nullptr) {
        // Skip header row (first line)
        if (!header_skipped) {
            header_skipped = true;
            continue;
        }

        char* fields[8];
        int nf = tokenise_line(line, fields, 8);
        if (nf < 5) {
            continue;   // malformed row — skip
        }

        // Column order: timestamp_ns, event_type, side, tick, qty
        const char* f_ts        = fields[0];
        const char* f_evtype    = fields[1];
        const char* f_side      = fields[2];
        const char* f_tick      = fields[3];
        const char* f_qty       = fields[4];

        // Parse timestamp_ns
        uint64_t timestamp_ns = 0U;
        if (!parse_u64(f_ts, timestamp_ns)) {
            continue;
        }

        // Parse event_type
        bool is_delete = false;
        if (std::strcmp(f_evtype, "UPSERT") == 0) {
            is_delete = false;
        } else if (std::strcmp(f_evtype, "DELETE") == 0) {
            is_delete = true;
        } else {
            continue;   // unknown event type — skip
        }

        // Parse side
        uint8_t side = 0U;
        if (std::strcmp(f_side, "BID") == 0) {
            side = 0U;
        } else if (std::strcmp(f_side, "ASK") == 0) {
            side = 1U;
        } else {
            continue;   // unknown side — skip
        }

        // Parse tick
        uint32_t tick = 0U;
        if (!parse_u32(f_tick, tick)) {
            continue;
        }

        // Parse qty (ignored for DELETE; forced to 0)
        uint64_t qty = 0U;
        if (!is_delete) {
            if (!parse_u64(f_qty, qty)) {
                continue;
            }
        }

        ReplayEvent ev{};
        ev.timestamp_ns = timestamp_ns;
        ev.tick         = tick;
        ev._pad         = 0U;
        ev.qty          = qty;
        ev.side         = side;
        // _pad2 is zero-initialised by {}

        events.push_back(ev);
    }

    std::fclose(fp);

    // Sort ascending by timestamp_ns (stable: preserve parse order for ties)
    std::stable_sort(events.begin(), events.end(),
        [](const ReplayEvent& a, const ReplayEvent& b) {
            return a.timestamp_ns < b.timestamp_ns;
        });

    return events;
}

} // namespace viz::model
