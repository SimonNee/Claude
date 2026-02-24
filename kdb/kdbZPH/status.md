# kdbZPH — Project Status

## Current State

**Branch:** `feature/zph-handler`
**Current Iteration:** 6 complete — **Next: Iteration 7 (Data Explorer)**

## Completed Iterations

| Iteration | Focus | Status |
|-----------|-------|--------|
| 1 | Basic HTTP response (`httpResp`, `.z.ph`) | ✓ Complete |
| 2 | Request parsing (`parseQS`, `parseHdr`, `parseReq`, `buildReq`) | ✓ Complete |
| 3 | Router + landing page (`routes`, `dispatch`, `htmlPage`, `htmlProcessInfo`, `htmlObjectBrowser`) | ✓ Complete |
| 4 | Static file server (`mimeType`, `handleStatic`, prefix routing, CSS extraction) | ✓ Complete |
| 5 | POST handler + JSON layer (`parsePost`, `jsonResp`, `jsonErr`, `postRoutes`, `.z.pp`, `handlePing`) | ✓ Complete |
| 6 | q REPL endpoint (`evalExpr`, `qToJson`, `handleEval`, `htmlRepl`, `static/app.js`) | ✓ Complete |

**Tests:** 76 passing in `test/test_zph.q`

## Files

| File | Purpose |
|------|---------|
| `src/zph.q` | All implementation — HTTP handler, parser, router, HTML builders, static server, POST handler, REPL |
| `test/test_zph.q` | Test suite — 76 tests |
| `static/style.css` | Site stylesheet |
| `static/app.js` | Browser REPL — fetch-based eval, Ctrl+Enter shortcut |
| `cfg/` | Empty — reserved for config (Iteration 11) |
| `project.md` | Full project plan and iteration roadmap |
| `status.md` | This file |

## Next Steps

### Iteration 7: Data Explorer
1. `GET /api/tables` — JSON list of tables with row/col counts
2. `GET /api/meta?table=X` — table schema as JSON
3. `GET /api/data?table=X&n=100&offset=0` — paginated rows as column-oriented JSON
4. `/explorer` route — HTML page with table picker, schema panel, data grid (vanilla JS)
5. Wire new GET routes into `routes` dict

**Key pitfalls:**
- `meta tbl` returns a table; the `t` column is **char** (type `"c"`), not symbol
- `tables[]` only returns default namespace tables
- Pagination: `n#offset _ tbl` for unkeyed tables; `value tbl` first for keyed tables
- Temporal columns: `.j.j` serializes as q string form — acceptable for display

## Bug History

| Iteration | Source Bugs | Test Bugs | Notes |
|-----------|------------|-----------|-------|
| 1 | 0 | 0 | — |
| 2 | 0 | 0 | — |
| 3 | 0 | 0 | Browser compatibility fix applied (commit 01e547e) |
| 4 | 0 | 0 | — |
| 5 (object browser) | 1 | 1 | `key\`` returns namespace names only — fixed with `system "f"`/`system "v"`; `ss` wildcard pitfall in test |
| 5 (POST handler) | 0 | 1 | Test needle with `*` crashed `ss` — trimmed needle |
| 6 | 2 | 0 | `min[a;b]` is rank error (use `a&b`); `.j.k` returns symbol keys not string keys |
