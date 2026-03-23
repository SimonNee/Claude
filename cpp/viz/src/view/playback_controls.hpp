/* playback_controls.hpp — draw_playback_controls() declaration
 *
 * Draws an ImGui window with Play/Pause button that writes PlaybackCmd.
 * Iteration 2: stub.
 *
 * Includes: book_snapshot.hpp, playback_cmd.hpp from model layer.
 * No sim_engine.hpp, no book.hpp.
 * Namespace: viz::view
 */

#pragma once

#include <mutex>
#include "book_snapshot.hpp"
#include "playback_cmd.hpp"

namespace viz::view {

void draw_playback_controls(const viz::model::BookSnapshot& snap,
                             std::mutex*                     cmd_mutex,
                             viz::model::PlaybackCmd*        cmd);

} // namespace viz::view
