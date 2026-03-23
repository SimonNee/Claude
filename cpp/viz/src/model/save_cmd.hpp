/* save_cmd.hpp — SaveCmd crossing struct
 *
 * Written by the render thread when the user triggers a CSV save of the
 * current OU-generated event sequence.  Read by the sim thread.
 * Protected by a separate std::mutex (save_mutex_ in main.cpp, distinct
 * from cmd_mutex_).  The save is not on the hot path.
 *
 * Protocol: render thread sets pending = true and writes path under
 * save_mutex_.  Sim thread polls pending under save_mutex_ after each event
 * dispatch.  When pending is true, sim calls save_csv(), sets pending = false.
 * Render thread does not wait for completion — fire-and-forget.
 *
 * Includes: <cstddef>, <cstring> only.
 * Namespace: viz::model
 */

#pragma once

#include <cstddef>
#include <cstring>

namespace viz::model {

// ---------------------------------------------------------------------------
// SaveCmd  (spec: Data Model / SaveCmd)
//
// Layout:
//   pending   offset   0 — true = save has been requested
//   _pad[7]   offset   1 — pad to 8 bytes before path
//   path[512] offset   8 — null-terminated output path
//             total: 520 bytes
// ---------------------------------------------------------------------------

struct SaveCmd {
    bool     pending;     // offset   0 — true = a save has been requested
    uint8_t  _pad[7];     // offset   1 — pad to 8 bytes before path
    char     path[512];   // offset   8 — null-terminated output path
};                        // total: 520 bytes

static_assert(sizeof(SaveCmd)  == 520U, "SaveCmd layout changed");
static_assert(alignof(SaveCmd) ==   1U, "SaveCmd alignment changed");

} // namespace viz::model
