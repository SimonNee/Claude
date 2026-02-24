# kdbZPH — Project Status

## Current State

**Branch:** `feature/zph-handler`
**Current Iteration:** 8 complete — **Next: Iteration 9 (Code Editor)**

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

**Tests:** 99 passing in `test/test_zph.q`

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
| `cfg/` | Empty — reserved for config (Iteration 11) |
| `project.md` | Full project plan and iteration roadmap |
| `status.md` | This file |

## Next Steps

### Iteration 10: Visualization (Plotly.js)
1. Add `GET /graph` route — page with chart area and controls
2. Add POST action `"plot"` — evals expression, returns column-oriented JSON
3. `toPlotData[tbl;chartType]` — converts q table to Plotly trace format
4. Plotly.js from CDN; `Plotly.newPlot()` on receipt

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
