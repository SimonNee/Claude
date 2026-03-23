/* dom_ladder.hpp — draw_dom_ladder() declaration
 *
 * Draws an ImGui window showing the DOM ladder from a BookSnapshot.
 * Iteration 2: stub — placeholder text only.
 *
 * Includes: book_snapshot.hpp only from the model layer. No sim_engine.hpp.
 * Namespace: viz::view
 */

#pragma once

#include "book_snapshot.hpp"

namespace viz::view {

// Draw the DOM Ladder window.  snap is the most-recent snapshot from
// SnapshotBuffer::read_snapshot().
void draw_dom_ladder(const viz::model::BookSnapshot& snap);

} // namespace viz::view
