/* playback_controls.cpp — draw_playback_controls() real implementation
 *
 * Iteration 3: Play/Pause toggle, speed slider, virtual clock display.
 *
 * Build constraints: -Wconversion -Wsign-conversion -Werror -fno-exceptions
 */

#include "playback_controls.hpp"
#include "theme.hpp"
#include "imgui.h"

namespace viz::view {

void draw_playback_controls(const viz::model::BookSnapshot& snap,
                             std::mutex*                     cmd_mutex,
                             viz::model::PlaybackCmd*        cmd) {
    ImGui::Begin("Playback");

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kFrameRounding);

    // ------------------------------------------------------------------
    // Read current state under mutex; release before drawing.
    // We copy both state and speed so we can draw without holding the lock.
    // ------------------------------------------------------------------
    viz::model::PlaybackState current_state = viz::model::PlaybackState::PAUSED;
    float current_speed = 1.0f;
    {
        std::lock_guard<std::mutex> lk(*cmd_mutex);
        current_state = cmd->state;
        current_speed = cmd->speed_mult;
    }

    // ------------------------------------------------------------------
    // Play / Pause button
    // ------------------------------------------------------------------
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

    // ------------------------------------------------------------------
    // Speed slider
    // ------------------------------------------------------------------
    ImGui::SameLine();

    // Local copy used by SliderFloat; written back under mutex only on change.
    float local_speed = current_speed;
    if (ImGui::SliderFloat("Speed", &local_speed, 0.0625f, 16.0f, "%.4fx")) {
        // Clamp to valid range (slider enforces bounds but guard defensively).
        if (local_speed < 0.0625f) local_speed = 0.0625f;
        if (local_speed > 16.0f)   local_speed = 16.0f;

        std::lock_guard<std::mutex> lk(*cmd_mutex);
        cmd->speed_mult = local_speed;
    }

    // ------------------------------------------------------------------
    // Virtual clock display
    // ------------------------------------------------------------------
    double time_s = static_cast<double>(snap.virtual_clock_ns) / 1e9;
    ImGui::Text("Time: %.3f s", time_s);

    ImGui::PopStyleVar();
    ImGui::End();
}

} // namespace viz::view
