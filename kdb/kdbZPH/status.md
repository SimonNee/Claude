# kdbZPH — Project Status

## Current State

**Branch:** `feature/zph-handler`
**Current Iteration:** 7 complete — **Next: Iteration 8 (WebSocket REPL)**

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

**Tests:** 87 passing in `test/test_zph.q`

## Files

| File | Purpose |
|------|---------|
| `src/zph.q` | All implementation |
| `test/test_zph.q` | Test suite — 87 tests |
| `static/style.css` | Site stylesheet |
| `static/app.js` | Browser REPL + data explorer frontend |
| `cfg/` | Empty — reserved for config (Iteration 11) |
| `project.md` | Full project plan and iteration roadmap |
| `status.md` | This file |

## Next Steps

### Iteration 8: WebSocket REPL
1. Define `.z.ws[x]` — parses JSON message, evals, sends result back via `neg[.z.w]`
2. Add `wsEval[msgStr]` — pure string-in/string-out function (testable without a live socket)
3. Support correlation `"id"` field so browser can match responses to requests
4. Update browser JS to use `WebSocket` instead of `fetch` for REPL; add connection status indicator

**Key pitfalls:**
- `x` in `.z.ws` is a string if browser sends text frame, byte vector if binary — check `10h=type x`
- Send with `neg[.z.w] responseString` — `.z.w` is only valid **during** the callback
- KDB+ handles the WebSocket HTTP Upgrade automatically when `.z.ws` is defined — do not intercept in `.z.ph`
- Test `wsEval` as a pure function; document manual browser test for the live WS connection

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
| 7 | 1 | 0 | `meta` returns keyed table; `flip` on it is `'nyi` — use `0!` first |
