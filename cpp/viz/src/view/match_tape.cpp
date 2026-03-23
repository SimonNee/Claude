/* match_tape.cpp — draw_match_tape() stub implementation
 *
 * Iteration 2: placeholder window.
 */

#include "match_tape.hpp"
#include "imgui.h"

namespace viz::view {

void draw_match_tape(const viz::model::BookSnapshot& snap) {
    ImGui::Begin("Match Tape (estimated)");
    ImGui::Text("fill_count: %u", static_cast<unsigned int>(snap.fill_count));
    ImGui::Text("(full tape — Iteration 3)");
    ImGui::End();
}

} // namespace viz::view
