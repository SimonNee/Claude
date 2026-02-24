# CLAUDE.md — kdbZPH Project

## What This Project Is

kdbZPH is a **self-hosted KDB+ workbench** served directly from a running q process. It is exploratory and general-purpose — not a financial/tick-data tool. The end goal is a browser-based IDE for q: REPL, object browser, data explorer, visualization, and code editor — all served from `.z.ph`/`.z.pp`/`.z.ws`.

**Read `project.md` for the full plan and `status.md` for current iteration.**

## Current State

- **Branch:** `feature/zph-handler`
- **Iterations complete:** 1–7 (HTTP handler, parser, router, static server, POST/JSON layer, REPL, data explorer)
- **Next:** Iteration 8 — WebSocket REPL
- **Tests:** `q test/test_zph.q` — 87 passing

## Key Files

- `src/zph.q` — all implementation
- `test/test_zph.q` — test suite
- `static/style.css` — site stylesheet
- `cfg/` — empty, config will go here in Iteration 11
- `project.md` — full plan with all iterations and q pitfalls
- `status.md` — current status and next steps

## How to Run

```bash
q src/zph.q -p 5050
# then open http://localhost:5050
```

## How to Test

```bash
q test/test_zph.q
```

## Architecture in Brief

- **GET** (`.z.ph`) — read-only: pages, object lists, metadata, static files
- **POST** (`.z.pp`) — execution: eval, plot, config. Routed by `"action"` key in JSON body (NOT by URL path — `.z.pp` does not receive the path)
- **WebSocket** (`.z.ws`) — added in Iteration 8 for live REPL
- **JSON** is the API wire format for all POST endpoints
- **Frontend** — vanilla JS + CDN libraries (no build step)

## AgentQ

Use the AgentQ agent for all KDB+/q code. It has q-specific pitfall documentation. The most common pitfalls relevant to this project are documented per-iteration in `project.md`.

## Coding Principles

- KISS — simplest solution that works
- Each iteration must be a working, testable prototype
- Tests first: all tests must pass before moving to next iteration
- Keep functions under 50 lines
- Explicit over implicit
