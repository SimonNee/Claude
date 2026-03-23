/* match_tape.hpp — draw_match_tape() declaration
 *
 * Draws an ImGui window showing estimated fills from a BookSnapshot.
 * Iteration 2: stub — placeholder text only.
 *
 * Includes: book_snapshot.hpp only from the model layer.
 * Namespace: viz::view
 */

#pragma once

#include "book_snapshot.hpp"

namespace viz::view {

void draw_match_tape(const viz::model::BookSnapshot& snap);

} // namespace viz::view
