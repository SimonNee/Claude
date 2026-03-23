/* match_tape.cpp — draw_match_tape() real implementation
 *
 * Iteration 3: scrolling fill tape, three columns: Time, Price, Qty.
 *
 * Build constraints: -Wconversion -Wsign-conversion -Werror -fno-exceptions
 * All uint64_t -> double conversions are explicit static_cast.
 */

#include "match_tape.hpp"
#include "theme.hpp"
#include "imgui.h"

namespace viz::view {

void draw_match_tape(const viz::model::BookSnapshot& snap) {
    ImGui::Begin("Match Tape (estimated)");

    ImGui::PushFont(viz::view::body_font);  // nullptr = default font

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_BordersInnerV  |
        ImGuiTableFlags_RowBg          |
        ImGuiTableFlags_ScrollY        |
        ImGuiTableFlags_SizingFixedFit;

    // Reserve enough height to show all fills; cap at window height.
    ImVec2 outer_size(0.0f, 0.0f);  // (0,0) = fill available space

    if (ImGui::BeginTable("match_tape_tbl", 3, table_flags, outer_size)) {
        ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed, kPriceColWidth);
        ImGui::TableSetupColumn("Qty",   ImGuiTableColumnFlags_WidthFixed, kQtyColWidth);
        ImGui::TableHeadersRow();

        uint32_t count = snap.fill_count;
        if (count > viz::model::VIZ_TAPE_DEPTH) {
            count = viz::model::VIZ_TAPE_DEPTH;
        }

        for (uint32_t i = 0U; i < count; ++i) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, kTapeRowHeight);

            // Alternate row colour by index (Phase 1: no aggressor side info).
            // Even index → buy colour, odd index → sell colour.
            ImVec4 row_col = ((i & 1U) == 0U) ? kBuyFillColor : kSellFillColor;
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   ImGui::ColorConvertFloat4ToU32(row_col));

            // Column 0: Time (virtual clock in seconds, 3dp)
            ImGui::TableSetColumnIndex(0);
            double time_s = static_cast<double>(snap.fills[i].timestamp_ns) / 1e9;
            ImGui::Text("%.3f", time_s);

            // Column 1: Price
            ImGui::TableSetColumnIndex(1);
            double price = static_cast<double>(snap.fills[i].price_tick) * 0.01;
            ImGui::Text("%.2f", price);

            // Column 2: Qty
            ImGui::TableSetColumnIndex(2);
            double qty = static_cast<double>(snap.fills[i].qty) / 1e8;
            ImGui::Text("%.4f", qty);
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();
    ImGui::End();
}

} // namespace viz::view
