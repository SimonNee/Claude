# ImGui Project Status

## Current State: Orientation Complete — Ready to Build

**Date:** 2026-03-02
**Branch:** `ImGui` (from `main`)

---

## What's Done

- Cloned Dear ImGui repo to `imgui/`
- Chose GLFW + OpenGL3 backend
- Installed deps: `libglfw3-dev`, `libgl-dev`
- Built `example_glfw_opengl3` successfully (zero warnings)
- Walked through `main.cpp` — full notes in `imgui_walkthrough.md`

## Mental Model Established

- Immediate mode vs retained mode (MFC background — contrast understood)
- Single-threaded context; worker threads feed data, UI thread owns ImGui
- MVC/MVVM is the developer's responsibility — ImGui imposes nothing
- Key pitfall noted: keep UI concerns (colours, fonts, layout) out of the model layer

## Next Step

- User is reading `imgui_walkthrough.md`
- On return: start building our own application from scratch
