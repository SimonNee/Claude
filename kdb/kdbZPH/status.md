# kdbZPH — Project Status

## Current State

**Branch:** `feature/zph-handler`
**Current Iteration:** 10 complete — **PAUSED: Strategic review before Iteration 11**

## Completed Iterations

| Iteration | Focus | Status |
|-----------|-------|--------|
| 1 | Basic HTTP response (`httpResp`, `.z.ph`) | ✓ Complete |
| 2 | Request parsing (`parseQS`, `parseHdr`, `parseReq`, `buildReq`) | ✓ Complete |
| 3 | Router + landing page (`routes`, `dispatch`, `htmlPage`, `htmlProcessInfo`, `htmlObjectBrowser`) | ✓ Complete |
| 4 | Static file server (`mimeType`, `handleStatic`, prefix routing, CSS extraction) | ✓ Complete |
| 5 | POST handler + JSON layer (`parsePost`, `jsonResp`, `jsonErr`, `postRoutes`, `.z.pp`, `handlePing`) | ✓ Complete |
| 6 | q REPL endpoint (`evalExpr`, `qToJson`, `handleEval`, `htmlRepl`, `static/app.js`) | ✓ Complete |
| 7 | Data explorer (`apiTables`, `apiMeta`, `apiData`, `/explorer` route, nav links) | ✓ Complete |
| 8 | WebSocket REPL (`wsEval`, `.z.ws`, WS status badge, auto-reconnect) | ✓ Complete |
| 9 | Code editor (`editor.js`, CodeMirror 6 via CDN, history, interpreter-style keybindings) | ✓ Complete |
| 10 | Visualization (`toPlotData`, `handlePlot`, `htmlGraph`, `/graph` route, Plotly.js CDN) | ✓ Complete |

**Tests:** 111 passing in `test/test_zph.q`

## Pre-Iteration-8 Refactor (committed separately)
- CORS header moved into `httpResp` — single source of truth for all responses
- `jsonResp` and `jsonErr` simplified to call `httpResp` (removed inlined header strings)
- Dead first `htmlPage` definition removed — one authoritative definition remains


## Files

| File | Purpose |
|------|---------|
| `src/zph.q` | All implementation |
| `test/test_zph.q` | Test suite — 96 tests |
| `static/style.css` | Site stylesheet |
| `static/app.js` | Browser REPL (WebSocket) + data explorer frontend |
| `static/graph.js` | Plotly.js graph page frontend |
| `cfg/` | Empty — reserved for config (Iteration 11) |
| `project.md` | Full project plan and iteration roadmap |
| `status.md` | This file |

## Strategic Pause — Current Thinking

Iterations 1–10 were a technical exploration — proving out the platform layer (HTTP, routing, WebSocket, plotting, eval, data API). That goal is complete. All the building blocks exist and are proven to work.

**Iteration 11 as originally planned (config, CSV, namespace polish) is deferred.** Adding more to the current flat structure without a clearer architectural direction is premature.

### Open Questions

1. **Platform vs application** — kdbZPH is currently a general-purpose workbench. The more valuable thing may be to define a clean platform interface that focused business applications sit on top of, rather than continuing to expand the workbench itself.

2. **Namespace/module structure** — the current flat global namespace works but doesn't compose well. A namespace-based structure (`.http`, `.router`, `.html`, `.api`, `.ws`, `.plot`) would make the platform reusable by business apps.

3. **Business verticals** — q/kdb is not limited to finance. Viable verticals include F1 telemetry, engineering sensor data, biochem time series. Each would be a focused app built on the platform layer, not a generic tool.

4. **What does a business app look like?** — how does it register routes, own its UI, and delegate plumbing to the platform? That interface needs designing before more code is written.

### Next Steps (when ready)
- Decide on platform interface design
- Identify first focused business app / vertical
- Refactor into namespace structure if proceeding with platform approach
- Iteration 11 config work can follow once the above is settled

## Known Deferred Items

| Issue | Deferred to |
|-------|-------------|
| Object browser does not refresh without a page reload | Iteration 11 (object browser rework) |
| q syntax highlighting in CodeMirror (no q language mode implemented) | Iteration 9 follow-up or later |
| Shift+Enter newline not working on iPadOS Safari/Brave | N/A — superseded by interpreter-style keybindings (Enter = newline) |

## Bug History

| Iteration | Source Bugs | Test Bugs | Notes |
|-----------|------------|-----------|-------|
| 1 | 0 | 0 | — |
| 2 | 0 | 0 | — |
| 3 | 0 | 0 | Browser compatibility fix applied (commit 01e547e) |
| 4 | 0 | 0 | — |
| 5 (object browser) | 1 | 1 | `key\`` returns namespace names only; `ss` wildcard pitfall |
| 5 (POST handler) | 0 | 1 | Test needle `*` crashed `ss` |
| 6 | 2 | 0 | `min[a;b]` rank error; `.j.k` returns symbol keys |
| 7 | 2 | 0 | `meta` returns keyed table (`'nyi` on flip); `n#tbl` recycles rows |
| 10 | 3 | 1 | `enlist dict` → 98h not 0h; `key plain_table` raises `'type`; spurious `ycols` in `each` branch |
