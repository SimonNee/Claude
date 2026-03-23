/* ou_controls.hpp — draw_ou_controls() declaration
 *
 * Draws an ImGui window with a Save CSV button that writes SaveCmd.
 * Iteration 2: stub.
 *
 * Includes: save_cmd.hpp from model layer only.
 * No sim_engine.hpp, no book.hpp.
 * Namespace: viz::view
 */

#pragma once

#include <mutex>
#include "save_cmd.hpp"

namespace viz::view {

void draw_ou_controls(std::mutex*            save_mutex,
                      viz::model::SaveCmd*   save_cmd);

} // namespace viz::view
