# kdbZPH — Project Status

## Current State

**Branch:** `feature/zph-handler`
**Current Iteration:** 5 complete — **Next: Iteration 6 (q REPL Endpoint)**

## Completed Iterations

| Iteration | Focus | Status |
|-----------|-------|--------|
| 1 | Basic HTTP response (`httpResp`, `.z.ph`) | ✓ Complete |
| 2 | Request parsing (`parseQS`, `parseHdr`, `parseReq`, `buildReq`) | ✓ Complete |
| 3 | Router + landing page (`routes`, `dispatch`, `htmlPage`, `htmlProcessInfo`, `htmlObjectBrowser`) | ✓ Complete |
| 4 | Static file server (`mimeType`, `handleStatic`, prefix routing, CSS extraction) | ✓ Complete |
| 5 | POST handler + JSON layer (`parsePost`, `jsonResp`, `jsonErr`, `postRoutes`, `.z.pp`, `handlePing`) | ✓ Complete |

**Tests:** 64 passing in `test/test_zph.q`

## Files

| File | Purpose |
|------|---------|
| `src/zph.q` | All implementation — HTTP handler, parser, router, HTML builders, static server, POST handler |
| `test/test_zph.q` | Test suite — 64 tests (assertions, strContains helper) |
| `static/style.css` | Site stylesheet (extracted from `htmlPage` in Iteration 4) |
| `cfg/` | Empty — reserved for config (Iteration 11) |
| `project.md` | Full project plan and iteration roadmap |
| `status.md` | This file |

## Next Steps

### Iteration 6: q REPL Endpoint (Core Feature)
1. Add `evalExpr[exprStr]` — safe eval returning `(ok; result)` pair via `@[{(1b;value x)};...]`
2. Add `qToJson[x]` — converts any q result to JSON-serializable form with type-aware fallbacks
3. Add `handleEval[req]` — POST action `"eval"`: takes `expr` string, returns result as JSON
4. Add `"eval"` to `postRoutes`
5. Row limit enforcement (default 1000 rows) before serialization
6. Minimal browser REPL in `static/app.js`: textarea + pre, `fetch()` POST to eval, display result

**Key pitfalls:**
- Pass expression as **string** to `value` — `value "1+1"` executes q; `` value `sym `` looks up a variable
- Mixed-type lists crash `.j.j` — use `string each x` as universal fallback
- For tables: use column-oriented JSON (`flip` then `.j.j`) — compact and better for plotting
- Function results (type >= 100h): return `string fn` for source representation

## Bug History

| Iteration | Source Bugs | Test Bugs | Notes |
|-----------|------------|-----------|-------|
| 1 | 0 | 0 | — |
| 2 | 0 | 0 | — |
| 3 | 0 | 0 | Browser compatibility fix applied (commit 01e547e) |
| 4 | 0 | 0 | — |
| 5 (object browser) | 1 | 1 | `key\`` returns namespace names only — fixed with `system "f"`/`system "v"`; `ss` treats `*` as wildcard in test needle |
| 5 (POST handler) | 0 | 1 | Test needle `"Access-Control-Allow-Origin: *"` crashed `ss` due to wildcard — trimmed to `"Access-Control-Allow-Origin"` |
