# Dear ImGui — example_glfw_opengl3 Walkthrough

Source file: `imgui/examples/example_glfw_opengl3/main.cpp`

---

## 1. Includes (lines 10-18)

```cpp
#include "imgui.h"               // Core ImGui API
#include "imgui_impl_glfw.h"     // GLFW backend (handles window/input)
#include "imgui_impl_opengl3.h"  // OpenGL3 backend (handles rendering)
#include <GLFW/glfw3.h>          // The GLFW window library itself
```

ImGui has a two-layer backend system:
- **Platform backend** (GLFW) — handles the OS window, keyboard, mouse, clipboard
- **Renderer backend** (OpenGL3) — takes ImGui's draw commands and puts pixels on screen

These are kept separate so you can mix and match. e.g. GLFW+Vulkan, or SDL2+OpenGL3.

---

## 2. One-time Setup (lines 40-102)

This runs once at startup and never again.

```cpp
glfwInit();                           // Start the GLFW library
glfwCreateWindow(1280, 800, ...)      // Create an OS window
glfwMakeContextCurrent(window);       // Attach OpenGL context to it
glfwSwapInterval(1);                  // Enable VSync (cap to monitor refresh rate)

ImGui::CreateContext();               // Initialise ImGui's internal state
ImGui::StyleColorsDark();             // Choose a colour theme (Dark/Light/Classic)

ImGui_ImplGlfw_InitForOpenGL(window, true);  // Connect GLFW -> ImGui
ImGui_ImplOpenGL3_Init(glsl_version);        // Connect OpenGL -> ImGui
```

### What is a context?
ImGui stores all its state (fonts, style, widget state) in a "context". You create one
at startup and destroy it at shutdown. Advanced apps can have multiple contexts but you
almost never need that.

### The GLSL version string
The `#if` block above this (lines 45-71) just picks the right GLSL shader version
string for the platform (ES2, ES3, macOS, or standard Linux/Windows). On our Linux
machine it resolves to `"#version 130"`.

---

## 3. The Main Loop (lines 135-202)

This is the heart of every ImGui application. It runs every frame (typically 60+ times/sec).

```cpp
while (!glfwWindowShouldClose(window))
{
    // --- Step 1: Collect input ---
    glfwPollEvents();

    // --- Step 2: Start a new ImGui frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // --- Step 3: Declare your UI (see section 4) ---
    ImGui::ShowDemoWindow(&show_demo_window);
    // ... your widgets go here ...

    // --- Step 4: Finalise the draw list ---
    ImGui::Render();

    // --- Step 5: Clear the screen ---
    glViewport(0, 0, display_w, display_h);
    glClear(GL_COLOR_BUFFER_BIT);

    // --- Step 6: Draw ImGui's output via OpenGL ---
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // --- Step 7: Show the finished frame ---
    glfwSwapBuffers(window);
}
```

### The "immediate mode" concept — the most important idea in ImGui

Traditional GUI frameworks (Qt, GTK, WinForms) are **retained mode**:
- You create widget objects once
- They live in memory permanently
- You register callbacks/event handlers

ImGui is **immediate mode**:
- Every frame you re-declare your entire UI from scratch
- There are no persistent widget objects
- If you stop calling `ImGui::Button(...)`, the button simply disappears
- Widget state (is this checkbox ticked?) lives in YOUR variables, not in ImGui

This sounds wasteful but is actually very fast, and makes UI logic dramatically simpler.

### Double buffering (SwapBuffers)
OpenGL draws into a back buffer while the front buffer is displayed. `glfwSwapBuffers`
flips them. This prevents screen tearing.

---

## 4. The UI Widgets (lines 160-190)

This is where you actually build UI. Notice how little code it takes:

```cpp
ImGui::Begin("Hello, world!");   // Open a named window panel

ImGui::Text("This is some useful text.");          // Static label
ImGui::Checkbox("Demo Window", &show_demo_window); // Checkbox — reads/writes a bool
ImGui::Checkbox("Another Window", &show_another_window);

ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Slider — reads/writes a float
ImGui::ColorEdit3("clear color", (float*)&clear_color);  // Colour picker — reads/writes ImVec4

if (ImGui::Button("Button"))    // Button returns true the frame it is clicked
    counter++;
ImGui::SameLine();              // Next widget goes on the same line
ImGui::Text("counter = %d", counter);

ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

ImGui::End();   // Close the window panel — must always pair with Begin()
```

### The key pattern: widgets directly own your variables

You pass a pointer to your own variable (`&my_bool`, `&my_float`).
ImGui reads it to display the current value, and writes it back if the user changes it.
No callbacks. No signals. No observer pattern. Just a pointer.

### Begin/End pairs
Every `ImGui::Begin("name")` must have a matching `ImGui::End()`. This is how ImGui
knows which panel to append widgets to. You can have as many panels as you like.

### The "Another Window" pattern (lines 183-190)

```cpp
if (show_another_window)
{
    ImGui::Begin("Another Window", &show_another_window);
    ImGui::Text("Hello from another window!");
    if (ImGui::Button("Close Me"))
        show_another_window = false;
    ImGui::End();
}
```

Passing `&show_another_window` as the second arg to `Begin()` gives the window a
close button (X). When clicked, ImGui sets the bool to false, and next frame the
`if` block doesn't execute — the window vanishes. Clean and simple.

---

## 5. The ImGuiIO struct (line 84)

```cpp
ImGuiIO& io = ImGui::GetIO();
```

`ImGuiIO` is the main communication channel between your app and ImGui. Key fields:

| Field | Purpose |
|-------|---------|
| `io.Framerate` | Smoothed frames per second |
| `io.WantCaptureMouse` | True when ImGui wants mouse input (hovering a panel) |
| `io.WantCaptureKeyboard` | True when ImGui wants keyboard input (typing in a field) |
| `io.ConfigFlags` | Feature flags (keyboard nav, gamepad nav, docking, etc.) |
| `io.Fonts` | Font atlas — add custom fonts here |

The `WantCapture` flags are important if you have your own input handling — check them
before passing events to your game/app logic.

---

## 6. Cleanup (lines 208-213)

Mirrors setup in reverse order:

```cpp
ImGui_ImplOpenGL3_Shutdown();
ImGui_ImplGlfw_Shutdown();
ImGui::DestroyContext();
glfwDestroyWindow(window);
glfwTerminate();
```

Always shut down backends before destroying the ImGui context.

---

## Summary: The minimal ImGui app structure

Every ImGui application follows this skeleton:

```
1. glfwInit + create window
2. ImGui::CreateContext
3. Init backends (GLFW + OpenGL3)

4. while (running):
   a. glfwPollEvents
   b. NewFrame x3
   c. [your UI code here]
   d. ImGui::Render
   e. glClear
   f. RenderDrawData
   g. SwapBuffers

5. Shutdown backends
6. ImGui::DestroyContext
7. glfwTerminate
```

Everything you will ever build with ImGui lives in step 4c.
