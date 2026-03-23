/* theme.hpp — all visual style constants for the orderbook visualiser
 *
 * A future design pass can restyle the entire application by editing this
 * file only.  No inline style literals are permitted in any draw function.
 *
 * Every colour (ImVec4), spacing value (float), rounding value (float),
 * and font reference used by draw functions must be defined here.
 *
 * The only permitted exceptions in draw code are ImVec2(0,0) and
 * ImVec2(-1,-1) layout sentinels that carry no visual meaning.
 *
 * Includes: imgui.h only.  Must NOT include any model-layer header.
 * Namespace: viz::view
 */

#pragma once

#include "imgui.h"

namespace viz::view {

// ---------------------------------------------------------------------------
// Colours (ImVec4: r, g, b, a)
// ---------------------------------------------------------------------------

// DOM ladder — bid side
inline constexpr ImVec4 kBidRowBg      { 0.05f, 0.15f, 0.05f, 1.00f };  // dark green tint
inline constexpr ImVec4 kBestBidHighlight { 0.10f, 0.45f, 0.10f, 1.00f };  // bright green

// DOM ladder — ask side
inline constexpr ImVec4 kAskRowBg      { 0.20f, 0.05f, 0.05f, 1.00f };  // dark red tint
inline constexpr ImVec4 kBestAskHighlight { 0.55f, 0.10f, 0.10f, 1.00f };  // bright red

// DOM ladder — spread row (shown between best bid and best ask)
inline constexpr ImVec4 kSpreadRowBg   { 0.12f, 0.12f, 0.12f, 1.00f };  // neutral grey

// Match tape — fill entry by aggressor side
inline constexpr ImVec4 kBuyFillColor  { 0.20f, 0.75f, 0.20f, 1.00f };  // green: buy aggressor
inline constexpr ImVec4 kSellFillColor { 0.85f, 0.25f, 0.25f, 1.00f };  // red: sell aggressor

// Window background
inline constexpr ImVec4 kWindowBg      { 0.08f, 0.08f, 0.10f, 1.00f };  // near-black blue-tint

// Table header row
inline constexpr ImVec4 kHeaderRowBg   { 0.16f, 0.16f, 0.20f, 1.00f };  // muted blue-grey

// ---------------------------------------------------------------------------
// Spacing (float, in pixels at default DPI)
// ---------------------------------------------------------------------------

inline constexpr float kDomRowHeight   = 18.0f;   // height of each price level row
inline constexpr float kTapeRowHeight  = 18.0f;   // height of each tape entry row
inline constexpr float kPriceColWidth  = 90.0f;   // price column width in DOM ladder
inline constexpr float kQtyColWidth    = 110.0f;  // quantity column width
inline constexpr float kDepthBarWidth  = 80.0f;   // depth bar column width

// ---------------------------------------------------------------------------
// Rounding (float, in pixels)
// ---------------------------------------------------------------------------

inline constexpr float kWindowRounding = 4.0f;    // window corner rounding
inline constexpr float kFrameRounding  = 3.0f;    // frame/button corner rounding

// ---------------------------------------------------------------------------
// Alpha
// ---------------------------------------------------------------------------

inline constexpr float kDimmedAlpha    = 0.40f;   // levels far from best bid/ask
inline constexpr float kActiveAlpha    = 1.00f;   // levels near best bid/ask

// ---------------------------------------------------------------------------
// Font
//
// body_font is loaded once in RenderLoop::init() and stored here as a global
// pointer.  Draw functions reference this pointer; they must not call
// ImGui::GetIO().Fonts->AddFontFromFileTTF() themselves.
//
// Initialised to nullptr; if RenderLoop does not load a custom font (e.g.
// font file not found), draw functions must fall back to ImGui's default
// font (pass nullptr to ImGui::PushFont — ImGui handles nullptr as default).
// ---------------------------------------------------------------------------

inline ImFont* body_font = nullptr;   // set by RenderLoop::init()

} // namespace viz::view
