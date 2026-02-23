# kdbZPH — Project Status

## Current State

**Branch:** `feature/zph-handler`
**Current Iteration:** 4 complete — **Next: Iteration 5 (POST Handler + JSON Layer)**

## Completed Iterations

| Iteration | Focus | Status |
|-----------|-------|--------|
| 1 | Basic HTTP response (`httpResp`, `.z.ph`) | ✓ Complete |
| 2 | Request parsing (`parseQS`, `parseHdr`, `parseReq`, `buildReq`) | ✓ Complete |
| 3 | Router + landing page (`routes`, `dispatch`, `htmlPage`, `htmlProcessInfo`, `htmlObjectBrowser`) | ✓ Complete |
| 4 | Static file server (`mimeType`, `handleStatic`, prefix routing, CSS extraction) | ✓ Complete |

**Tests:** 53 passing in `test/test_zph.q`

## Files

| File | Purpose |
|------|---------|
| `src/zph.q` | All implementation — HTTP handler, parser, router, HTML builders, static server |
| `test/test_zph.q` | Test suite — 53 tests (assertions, strContains helper) |
| `static/style.css` | Site stylesheet (extracted from `htmlPage` in Iteration 4) |
| `cfg/` | Empty — reserved for config (Iteration 11) |
| `project.md` | Full project plan and iteration roadmap |
| `status.md` | This file |

## Next Steps

### Iteration 5: POST Handler + JSON Layer
1. Add `.z.pp[x]` entry point — parses body, dispatches on `action` key
2. Add `parsePost[x]` — extracts body string and headers from `.z.pp` argument
3. Add `jsonResp[data]` — wraps `.j.j data` in HTTP 200 `application/json` response
4. Add `jsonErr[msg]` — `{"error":"..."}` with HTTP 400
5. Add `postRoutes` dict — keyed on action symbols
6. First action: `"ping"` returns `{"status":"ok","ts":"<.z.p>"}`
7. Add tests — ping round-trip, malformed JSON handled, CORS header present

**Key pitfall:** `.z.pp` does NOT receive the URL path — route via `"action"` key in JSON body only.

## Bug History

| Iteration | Source Bugs | Test Bugs | Notes |
|-----------|------------|-----------|-------|
| 1 | 0 | 0 | — |
| 2 | 0 | 0 | — |
| 3 | 0 | 0 | Browser compatibility fix applied (commit 01e547e) |
| 4 | 0 | 0 | — |
