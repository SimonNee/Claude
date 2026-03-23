/* playback_controls.cpp — draw_playback_controls() stub implementation
 *
 * Iteration 2: Play/Pause button writes PlaybackCmd under mutex.
 */

#include "playback_controls.hpp"
#include "imgui.h"

namespace viz::view {

void draw_playback_controls(const viz::model::BookSnapshot& snap,
                             std::mutex*                     cmd_mutex,
                             viz::model::PlaybackCmd*        cmd) {
    (void)snap;   // snap not used in stub; included in sig for consistency

    ImGui::Begin("Playback");

    // Read current state (snapshot read — no mutex needed for display).
    // We take the mutex only on write to avoid holding it during ImGui draw.
    viz::model::PlaybackState current_state = viz::model::PlaybackState::PAUSED;
    {
        std::lock_guard<std::mutex> lk(*cmd_mutex);
        current_state = cmd->state;
    }

    bool is_running = (current_state == viz::model::PlaybackState::RUNNING);
    const char* btn_label = is_running ? "Pause" : "Play";

    if (ImGui::Button(btn_label)) {
        std::lock_guard<std::mutex> lk(*cmd_mutex);
        if (cmd->state == viz::model::PlaybackState::RUNNING) {
            cmd->state = viz::model::PlaybackState::PAUSED;
        } else {
            cmd->state = viz::model::PlaybackState::RUNNING;
        }
    }

    ImGui::End();
}

} // namespace viz::view
