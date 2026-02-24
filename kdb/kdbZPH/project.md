# kdbZPH — KDB+ Self-Hosted Workbench

## Project Vision

kdbZPH is a **self-hosted KDB+ workbench** served directly from a running q process via its built-in HTTP callbacks. The goal is a browser-based IDE for q — think Jupyter but native to q, without Python in the middle.

**Key characteristics:**
- General purpose — not a financial/tick-data tool
- Exploratory and speculative — bold design decisions welcome
- Self-hosted — the q process serves its own frontend
- Incrementally deliverable — each iteration is a working prototype
- Goal may shift as we learn what works

**Core pillars (end state):**
1. **REPL/Console** — evaluate q expressions from the browser, see results
2. **Object Browser** — live introspection (tables, functions, namespaces, views)
3. **Data Explorer** — query and inspect table contents interactively
4. **Graphing/Visualization** — visualize data from the process using JS libraries
5. **Code Editor** — edit q code with syntax highlighting

---

## Architecture Principles

- **GET** (`.z.ph`) handles read-only data: pages, object lists, metadata, CSV export
- **POST** (`.z.pp`) handles execution: eval, plot data, config writes
- **WebSocket** (`.z.ws`) added later (Iteration 8) for live REPL
- **Static files** served from `static/` via `.z.ph` prefix routing for `/static/*`
- **JSON** is the API wire format for POST/response; `.j.j` / `.j.k` throughout
- **Frontend**: vanilla JS + CDN libraries (no build step); q process is the only server

### Critical `.z.pp` Architecture Note

`.z.pp` does **not** receive the URL path — unlike `.z.ph`, there is no path argument. All POST routing uses an `"action"` key in the JSON body:

```q
/ Client sends: {"action":"eval","expr":"1+1"}
/ Client sends: {"action":"ping"}
postRoutes:(`ping`eval)!(handlePing;handleEval)

.z.pp:{[x]
  body:first x;
  req:@[.j.k; body; {[e]`$()}];
  action:`$req`action;
  handler:$[action in key postRoutes; postRoutes action; handleBadAction];
  handler req
 }
```

---

## What Has Been Built (Iterations 1–5)

**Source:** `src/zph.q`
**Tests:** `test/test_zph.q` — 64 passing tests

### Function Reference

| Function | Signature | Purpose |
|----------|-----------|---------|
| `httpResp` | `[status;ctype;body]` | Builds a full HTTP/1.1 response string |
| `parseQS` | `[qs]` | Parses `"a=1&b=2"` → `` `a`b!("1";"2") `` |
| `parseHdr` | `[ln]` | Parses `"Key: value"` → `` (`Key;"value") `` |
| `parseReq` | `[raw]` | Parses raw HTTP request → dict with `method path query version headers` |
| `buildReq` | `[x]` | Adapts `.z.ph` input (string or `(string;headerDict)`) → parsed request dict |
| `htmlPage` | `[ttl;bodyContent]` | Wraps content in full HTML document with stylesheet link |
| `htmlProcessInfo` | `[]` | HTML card: port, PID, version, OS, memory, table/function counts |
| `htmlObjectBrowser` | `[]` | HTML card: all tables/views/functions/variables in default namespace |
| `html404` | `[pth]` | 404 not-found HTML card |
| `handleRoot` | `[req]` | Serves landing page (process info + object browser) |
| `handle404` | `[req]` | Serves 404 page |
| `routes` | dict | Path symbol → handler fn: `` (enlist`$"/")!enlist handleRoot `` |
| `dispatch` | `[parsed]` | Routes parsed GET request; prefix routes `/static/*` before dict lookup |
| `mimeType` | dict | File extension string → MIME type string |
| `handleStatic` | `[req]` | Serves files from `static/`; path traversal guard; 404 on miss |
| `parsePost` | `[x]` | Extracts body string and headers from `.z.pp` argument |
| `jsonResp` | `[data]` | Wraps `.j.j data` in HTTP 200 `application/json` response with CORS header |
| `jsonErr` | `[msg]` | Returns HTTP 400 `{"error":"..."}` response with CORS header |
| `handlePing` | `[req]` | POST action `ping` — returns `{"status":"ok","ts":"..."}` |
| `postRoutes` | dict | Action symbol → handler fn: `` (enlist`ping)!enlist handlePing `` |
| `.z.ph` | `[x]` | KDB+ HTTP GET entry point; `buildReq` → `dispatch` with 500 error trap |
| `.z.pp` | `[x]` | KDB+ HTTP POST entry point; `parsePost` → `.j.k` → action dispatch |

### Test Harness Pattern

```q
\l src/zph.q
pass:0
fail:0
assert:{[msg;cond] $[cond; [pass+::1;-1 "PASS: ",msg]; [fail+::1;-1 "FAIL: ",msg]]}
strContains:{[haystack;needle] 0<count ss[haystack;needle]}
/ ...tests...
-1 "Results: ",(string pass)," passed, ",(string fail)," failed";
if[fail>0; exit 1]
```

---

## Iteration Roadmap

### Iteration 4 — Static File Server
**Objective:** Serve files from `static/` and move inline CSS out of q strings.

**Deliverables:**
- `mimeType[ext]` — dict: `"css"!"text/css"`, `"js"!"application/javascript"`, etc.
- `handleStatic[req]` — reads file from `static/`, sets Content-Type, returns 404 on miss
- Prefix routing in `dispatch`: paths starting `/static/` route to `handleStatic` before dict lookup
- `static/style.css` — extracted from `htmlPage`; `<head>` links to it

**q pitfalls:**
- Use `"\n" sv read0 hsym`$path`` for text files; `read1` returns byte vector (type 4h), not a string
- **Path traversal**: reject any path component equal to `".."` before constructing the filesystem path
- File read with error trap: `@[read0; path; {[e](::)}]` — treat any error as 404; no existence-check-then-read
- Serving binary (images) via `.z.ph` is problematic (response must be type 10h); defer to text-only (CSS/JS) for now
- **`key\`` returns namespace names only** — not regular variables. Use `system "f"` for user functions and `system "v"` for user variables instead of filtering `key\`` by type
- **`ss` treats `*` and `?` as wildcards** — avoid these characters in `strContains` needles or the call will throw `'length`

**Tests:** static file round-trip, MIME types, path traversal rejected, 404 on missing file

---

### Iteration 5 — POST Handler + JSON Layer
**Objective:** Add `.z.pp`; establish JSON as the API wire format.

**Deliverables:**
- `.z.pp[x]` — entry point; parses body, dispatches on `action` key in JSON body
- `parsePost[x]` — extracts body string and headers from `.z.pp`'s argument
- `jsonResp[data]` — wraps `.j.j data` in HTTP 200 `application/json` response
- `jsonErr[msg]` — `{"error":"..."}` with HTTP 400
- First POST endpoint: action `"ping"` returns `{"status":"ok","ts":"<.z.p>"}`

**q pitfalls:**
- `.z.pp` path routing: see Architecture section above — path is NOT in the argument
- Test early whether returning the response string works in your q version vs needing `neg[.z.w] resp`
- Always trap `.j.k`: `@[.j.k; body; {[e](::)}]` — malformed JSON signals an error; returns `(::)` on failure
- Add `Access-Control-Allow-Origin: *` to `jsonResp` from the start to avoid CORS pain
- **`.[f;enlist x;h]` not `@[f;x;h]` in `.z.pp`** — `.z.pp`'s `x` is a general list (type 0h); using `@[f;x;h]` would iterate over its elements instead of passing `x` as a whole

**Tests:** ping round-trip, malformed JSON handled, CORS header present

---

### Iteration 6 — q REPL Endpoint (Core Feature)
**Objective:** `action:"eval"` evaluates a q expression and returns the result as JSON.

**Deliverables:**
- `evalExpr[exprStr]` — safe eval returning `(ok; result)` pair
- `qToJson[x]` — converts any q result to JSON-serializable form with type-aware fallbacks
- Row limit enforcement before serialization (default 1000 rows)
- Minimal browser REPL: textarea + pre, `fetch()` POST to eval action, display result

**Safe eval pattern:**
```q
evalExpr:{[exprStr]
  @[{(1b;value x)}; exprStr; {[e](0b;e)}]
 }
```

**q pitfalls:**
- Pass expression as **string** (type 10h), not a symbol — `value "1+1"` executes q; `` value `sym `` looks up a variable
- Result can be any q type including `(::)`, `()`, mixed lists, functions, tables — all must survive `qToJson`
- For tables: use **column-oriented JSON** (`flip` then `.j.j`) — `{"col1":[...],"col2":[...]}` — compact and better for plotting
- Row limit: `$[98h=type r; (min[1000;count r])#r; r]`
- Mixed-type lists crash `.j.j` — use `string each x` as universal fallback
- Function results (type >= 100h): return `string fn` for source representation
- Use `value` (not `reval`) for local single-user workbench; document this assumption

**Tests:** known expressions, table results with row limit, error results, empty results, function results

---

### Iteration 7 — Data Explorer
**Objective:** Browse and query table contents interactively via GET endpoints.

**Deliverables:**
- `GET /api/tables` — JSON list of tables with row/col counts
- `GET /api/meta?table=X` — table schema as JSON
- `GET /api/data?table=X&n=100&offset=0` — paginated rows as column-oriented JSON
- `/explorer` route — HTML page with table picker, schema panel, data grid (vanilla JS)

**q pitfalls:**
- `meta tbl` returns a table; the `t` column is **char** (type `"c"`), not symbol — single chars like `"f"`, `"j"`, `"p"`
- `tables[]` only returns default namespace tables; named namespaces (`.myns.trade`) need `` key `.myns` `` filtered by type
- Pagination: `n#offset _ tbl` works for in-memory unkeyed tables; `value tbl` first for keyed tables
- Temporal columns: `.j.j` serializes as q string form — acceptable for display

**Tests:** table list, meta schema, pagination, empty table, keyed table

---

### Iteration 8 — WebSocket REPL
**Objective:** Replace fetch-based REPL with a persistent WebSocket connection.

**Deliverables:**
- `.z.ws[x]` — parses JSON message, evals, sends result back to same handle
- `wsEval[msgStr]` — pure string-in/string-out function (testable without a live socket)
- Correlation `"id"` field in messages so browser matches responses to requests
- Browser: `WebSocket` API, connection status indicator, reconnect on close

**q pitfalls:**
- `x` is a **string** if browser sends text frame, or byte vector if binary — check: `$[10h=type x; ...; ...]`
- Send with `neg[.z.w] responseString` — `.z.w` is only valid **during the callback**; wrap in error trap
- KDB+ handles WebSocket HTTP Upgrade **automatically** when `.z.ws` is defined — do not intercept in `.z.ph`
- Long evals block the q main thread; acceptable for local single-user workbench; document this
- Start with JSON text frames; binary IPC (faster) requires a custom JS deserializer

**Testing:** Test `wsEval` as pure function; document manual browser test procedure for the WS connection.

---

### Iteration 9 — Code Editor (CodeMirror 6)
**Objective:** Replace textarea with a real editor with q syntax highlighting.

**Deliverables:**
- CodeMirror 6 loaded via CDN (no build step)
- Replace textarea in REPL with CodeMirror editor instance
- q syntax mode (keyword list highlighting as minimum)
- Shift+Enter = newline, Enter = submit
- Expression history persisted to `localStorage`

**q pitfalls:** Minimal — mostly frontend work. Ensure `htmlPage` does not double-escape `<script>` content.

---

### Iteration 10 — Visualization (Plotly.js)
**Objective:** Plot q data directly in the browser.

**Deliverables:**
- `GET /graph` route — page with chart area and controls
- POST action `"plot"` — receives `{"action":"plot","expr":"...","chart":"line"}`, evals, returns column-oriented JSON
- `toPlotData[tbl;chartType]` — converts q table to Plotly trace format
- Plotly.js from CDN; `Plotly.newPlot()` on receipt

**q pitfalls:**
- **Column-oriented JSON for Plotly**: `{"x":[...],"y":[...]}` not row-oriented `[{"x":1,"y":2},...]`
- Temporal types: `string each timestamps` gives Plotly-parseable strings for most chart types
- KDB+ nulls become JSON `null` — Plotly renders as gaps in line charts (usually correct)
- Downsampling: `select[::n] from tbl` to keep results under ~10k points

**Open decision:** Plotly.js vs Observable Plot. Both work with the column-oriented JSON format established in Iteration 6. Decide at implementation time.

---

### Iteration 11 — Configuration + Polish
**Objective:** Config file, full namespace support, CSV export.

**Deliverables:**
- `cfg/config.q` — row limits, allowed namespaces, history size with defaults
- `loadCfg[]` — loads config with fallback defaults
- Multi-namespace object browser
- `GET /api/csv?table=X&n=1000` — table as CSV download

**q pitfalls:**
- `\l file` **does not work inside a function** — use `system "l file"` or top-level load with trap: `@[system;"l cfg/config.q";{[e]-1 "no config, using defaults"}]`
- `` key `.myns` `` on a **non-existent namespace** returns integer `0`, not an empty list — check `99h=type key `.ns`` before iterating
- CSV: `"," 0: tbl` returns list of strings; join with `"\n" sv "," 0: tbl`. Set `Content-Disposition: attachment; filename="export.csv"` in response header to trigger browser download

---

## File Structure (End State)

```
src/
  zph.q          — main HTTP handler (grows each iteration)
static/
  style.css      — extracted from htmlPage (Iteration 4)
  app.js         — REPL + explorer frontend (Iteration 6+)
cfg/
  config.q       — runtime config (Iteration 11)
test/
  test_zph.q     — test suite (grows each iteration)
project.md       — this file
status.md        — current iteration status
```

---

## Verification Pattern (Each Iteration)

1. `q test/test_zph.q` — all tests pass, 0 failed
2. `q src/zph.q -p 5050` — start the process
3. Browser: `http://localhost:5050` — visual smoke test of new feature
4. For WebSocket (Iteration 8): browser console test procedure documented in iteration
