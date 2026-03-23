/* dom_ladder.cpp — draw_dom_ladder() stub implementation
 *
 * Iteration 2: placeholder window.  Full table rendering deferred to Iteration 3.
 */

#include "dom_ladder.hpp"
#include "imgui.h"

namespace viz::view {

void draw_dom_ladder(const viz::model::BookSnapshot& snap) {
    ImGui::Begin("DOM Ladder");
    ImGui::Text("best_bid_tick: %u", static_cast<unsigned int>(snap.best_bid_tick));
    ImGui::Text("best_ask_tick: %u", static_cast<unsigned int>(snap.best_ask_tick));
    ImGui::Text("(full DOM ladder — Iteration 3)");
    ImGui::End();
}

} // namespace viz::view
