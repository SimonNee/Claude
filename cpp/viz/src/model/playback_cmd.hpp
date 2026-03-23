/* playback_cmd.hpp — PlaybackState enum and PlaybackCmd crossing struct
 *
 * Written by the render thread (view layer) when the user interacts with
 * playback controls.  Read by the sim thread.  Protected by std::mutex.
 *
 * Includes: <cstdint> only.
 * Namespace: viz::model
 */

#pragma once

#include <cstdint>

namespace viz::model {

// ---------------------------------------------------------------------------
// PlaybackState  (spec: Data Model / PlaybackCmd)
// ---------------------------------------------------------------------------

enum class PlaybackState : uint8_t {
    RUNNING = 0,
    PAUSED  = 1
};

// ---------------------------------------------------------------------------
// PlaybackCmd  (spec: Data Model / PlaybackCmd)
//
// Layout:
//   state       offset 0 — RUNNING or PAUSED
//   _pad[3]     offset 1 — explicit pad
//   speed_mult  offset 4 — replay speed multiplier; range [0.0625, 16.0]
//               total: 8 bytes
// ---------------------------------------------------------------------------

struct PlaybackCmd {
    PlaybackState  state;         // offset 0 — RUNNING or PAUSED
    uint8_t        _pad[3];       // offset 1 — explicit pad
    float          speed_mult;    // offset 4 — replay speed multiplier; 1.0 = real-time
                                  // range: [0.0625, 16.0]; 0.0 is not valid
};                                // total: 8 bytes

static_assert(sizeof(PlaybackCmd)  == 8U, "PlaybackCmd layout changed");
static_assert(alignof(PlaybackCmd) == 4U, "PlaybackCmd alignment changed");

} // namespace viz::model
