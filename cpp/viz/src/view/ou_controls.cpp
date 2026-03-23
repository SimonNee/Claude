/* ou_controls.cpp — draw_ou_controls() stub implementation
 *
 * Save CSV button: acquires save_mutex, sets pending=true, copies path.
 * Button disabled if save_cmd->pending is already true.
 */

#include "ou_controls.hpp"
#include "imgui.h"

#include <cstring>

namespace viz::view {

void draw_ou_controls(std::mutex*           save_mutex,
                      viz::model::SaveCmd*  save_cmd) {
    ImGui::Begin("OU Controls");

    // Static buffer for the path InputText — persists between frames.
    static char path_buf[512] = "ou_events.csv";

    ImGui::InputText("Output path", path_buf, sizeof(path_buf));

    // Read pending under mutex for the button disabled state.
    bool already_pending = false;
    {
        std::lock_guard<std::mutex> lk(*save_mutex);
        already_pending = save_cmd->pending;
    }

    if (already_pending) {
        ImGui::BeginDisabled(true);
    }

    if (ImGui::Button("Save CSV")) {
        std::lock_guard<std::mutex> lk(*save_mutex);
        if (!save_cmd->pending) {
            save_cmd->pending = true;
            std::strncpy(save_cmd->path, path_buf, 511U);
            save_cmd->path[511] = '\0';
        }
    }

    if (already_pending) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("(saving...)");
    }

    ImGui::End();
}

} // namespace viz::view
