/* dom_ladder.cpp — draw_dom_ladder() real implementation
 *
 * Iteration 3: full ImGui Table with asks above spread and bids below.
 *
 * Build constraints: -Wconversion -Wsign-conversion -Werror -fno-exceptions
 * All uint32_t / uint64_t -> double conversions are explicit static_cast.
 */

#include "dom_ladder.hpp"
#include "theme.hpp"
#include "imgui.h"

namespace viz::view {

// Sentinel value for an empty/uninitialised tick — must match
// eth::book::TICK_INVALID without importing book.hpp into the view layer.
static constexpr uint32_t kTickInvalid = 0xFFFFFFFFU;

// Helper: convert ImVec4 colour to ImU32 with an explicit alpha override.
// ImGui::ColorConvertFloat4ToU32 is used here — no inline literals in draw code.
static ImU32 vec4_to_u32_alpha(ImVec4 col, float alpha) {
    ImVec4 c = col;
    c.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

void draw_dom_ladder(const viz::model::BookSnapshot& snap) {
    ImGui::Begin("DOM Ladder");

    ImGui::PushFont(viz::view::body_font);  // nullptr = default font; ImGui handles it

    // Compute max qty across all visible levels for depth-bar scaling.
    uint64_t max_qty = 1U;  // floor at 1 to avoid divide-by-zero
    for (uint32_t i = 0U; i < viz::model::VIZ_LADDER_DEPTH; ++i) {
        if (snap.asks[i].qty > max_qty) max_qty = snap.asks[i].qty;
        if (snap.bids[i].qty > max_qty) max_qty = snap.bids[i].qty;
    }

    // Table: 3 columns — Price, Qty, Depth
    // ImGuiTableFlags_BordersInnerV: vertical dividers between columns.
    // ImGuiTableFlags_NoHostExtendX: table does not stretch beyond its content width.
    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("dom_ladder_tbl", 3, table_flags)) {
        ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed, kPriceColWidth);
        ImGui::TableSetupColumn("Qty",   ImGuiTableColumnFlags_WidthFixed, kQtyColWidth);
        ImGui::TableSetupColumn("Depth", ImGuiTableColumnFlags_WidthFixed, kDepthBarWidth);
        ImGui::TableHeadersRow();

        // ----------------------------------------------------------------
        // Ask rows: render worst ask first (index VIZ_LADDER_DEPTH-1 → 0)
        // so that best ask sits immediately above the spread row.
        // ----------------------------------------------------------------
        for (uint32_t rev = 0U; rev < viz::model::VIZ_LADDER_DEPTH; ++rev) {
            uint32_t i = viz::model::VIZ_LADDER_DEPTH - 1U - rev;

            bool is_best_ask = (i == 0U) &&
                               (snap.asks[0U].qty != 0U) &&
                               (snap.asks[0U].tick != kTickInvalid);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, kDomRowHeight);

            // Row background colour
            ImVec4 row_col = is_best_ask ? kBestAskHighlight : kAskRowBg;
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   ImGui::ColorConvertFloat4ToU32(row_col));

            // Column 0: Price
            ImGui::TableSetColumnIndex(0);
            if (snap.asks[i].tick == kTickInvalid || snap.asks[i].qty == 0U) {
                ImGui::TextUnformatted("--.--.--");
            } else {
                double price = static_cast<double>(snap.asks[i].tick) * 0.01;
                ImGui::Text("%.2f", price);
            }

            // Column 1: Qty
            ImGui::TableSetColumnIndex(1);
            if (snap.asks[i].qty != 0U && snap.asks[i].tick != kTickInvalid) {
                double qty = static_cast<double>(snap.asks[i].qty) / 1e8;
                ImGui::Text("%.4f", qty);
            }

            // Column 2: Depth bar
            ImGui::TableSetColumnIndex(2);
            if (snap.asks[i].qty != 0U && snap.asks[i].tick != kTickInvalid) {
                float ratio = static_cast<float>(
                    static_cast<double>(snap.asks[i].qty) /
                    static_cast<double>(max_qty));
                float alpha = is_best_ask ? kActiveAlpha : kDimmedAlpha;
                ImU32 bar_col = vec4_to_u32_alpha(kBestAskHighlight, alpha);

                ImVec2 cell_min = ImGui::GetCursorScreenPos();
                float bar_w = ratio * kDepthBarWidth;
                float bar_h = kDomRowHeight - 2.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(
                    cell_min,
                    ImVec2(cell_min.x + bar_w, cell_min.y + bar_h),
                    bar_col
                );
            }
        }

        // ----------------------------------------------------------------
        // Spread row
        // ----------------------------------------------------------------
        ImGui::TableNextRow(ImGuiTableRowFlags_None, kDomRowHeight);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                               ImGui::ColorConvertFloat4ToU32(kSpreadRowBg));

        ImGui::TableSetColumnIndex(0);
        if (snap.best_bid_tick != kTickInvalid && snap.best_ask_tick != kTickInvalid &&
            snap.best_ask_tick > snap.best_bid_tick) {
            // Spread in ticks; display as price units (each tick = 0.01)
            uint32_t spread_ticks = snap.best_ask_tick - snap.best_bid_tick;
            double spread_price = static_cast<double>(spread_ticks) * 0.01;
            ImGui::Text("Spread: %.2f", spread_price);
        } else {
            ImGui::TextUnformatted("--- SPREAD ---");
        }
        // Columns 1 and 2 left intentionally blank for the spread row.

        // ----------------------------------------------------------------
        // Bid rows: render best bid first (index 0 → VIZ_LADDER_DEPTH-1)
        // ----------------------------------------------------------------
        for (uint32_t i = 0U; i < viz::model::VIZ_LADDER_DEPTH; ++i) {
            bool is_best_bid = (i == 0U) &&
                               (snap.bids[0U].qty != 0U) &&
                               (snap.bids[0U].tick != kTickInvalid);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, kDomRowHeight);

            ImVec4 row_col = is_best_bid ? kBestBidHighlight : kBidRowBg;
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   ImGui::ColorConvertFloat4ToU32(row_col));

            // Column 0: Price
            ImGui::TableSetColumnIndex(0);
            if (snap.bids[i].tick == kTickInvalid || snap.bids[i].qty == 0U) {
                ImGui::TextUnformatted("--.--.--");
            } else {
                double price = static_cast<double>(snap.bids[i].tick) * 0.01;
                ImGui::Text("%.2f", price);
            }

            // Column 1: Qty
            ImGui::TableSetColumnIndex(1);
            if (snap.bids[i].qty != 0U && snap.bids[i].tick != kTickInvalid) {
                double qty = static_cast<double>(snap.bids[i].qty) / 1e8;
                ImGui::Text("%.4f", qty);
            }

            // Column 2: Depth bar
            ImGui::TableSetColumnIndex(2);
            if (snap.bids[i].qty != 0U && snap.bids[i].tick != kTickInvalid) {
                float ratio = static_cast<float>(
                    static_cast<double>(snap.bids[i].qty) /
                    static_cast<double>(max_qty));
                float alpha = is_best_bid ? kActiveAlpha : kDimmedAlpha;
                ImU32 bar_col = vec4_to_u32_alpha(kBestBidHighlight, alpha);

                ImVec2 cell_min = ImGui::GetCursorScreenPos();
                float bar_w = ratio * kDepthBarWidth;
                float bar_h = kDomRowHeight - 2.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(
                    cell_min,
                    ImVec2(cell_min.x + bar_w, cell_min.y + bar_h),
                    bar_col
                );
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();
    ImGui::End();
}

} // namespace viz::view
