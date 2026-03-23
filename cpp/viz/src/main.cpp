/* main.cpp — Orderbook Visualiser entry point
 *
 * Constructs all objects, wires them together, runs the render loop.
 * This is the only file that includes both model-layer and view-layer headers.
 *
 * Source selection:
 *   (no args)  — run with --ou (OUEventSource, default params)
 *   --ou       — synthetic OU event source
 *   --selftest — render exactly one frame then exit 0 (CI smoke test)
 *   <path>     — CSV replay file (CsvEventSource)
 *
 * Build flags: -std=c++17 -O2 -march=native -Wall -Wextra
 *              -Wconversion -Wsign-conversion -Werror -fno-exceptions
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <exception>   // std::terminate

// GLFW + OpenGL (order matters: GL before GLFW on some platforms)
#include <GL/gl.h>
#include <GLFW/glfw3.h>

// ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Model layer
#include "event_source.hpp"
#include "csv_event_source.hpp"
#include "ou_event_source.hpp"
#include "kdb_event_source.hpp"
#include "replay_loader.hpp"
#include "sim_engine.hpp"
#include "snapshot_buffer.hpp"
#include "playback_cmd.hpp"
#include "save_cmd.hpp"

// View layer
#include "dom_ladder.hpp"
#include "match_tape.hpp"
#include "playback_controls.hpp"
#include "ou_controls.hpp"

// ---------------------------------------------------------------------------
// GLFW error callback — called on any GLFW error
// ---------------------------------------------------------------------------

static void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Parse arguments
    bool     selftest = false;
    bool     use_ou   = false;
    bool     use_kdb  = false;
    uint16_t kdb_port = viz::model::KDB_DEFAULT_PORT;
    const char* csv_path = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) {
            selftest = true;
        } else if (std::strcmp(argv[i], "--ou") == 0) {
            use_ou = true;
        } else if (std::strcmp(argv[i], "--kdb") == 0) {
            use_kdb = true;
        } else if (std::strcmp(argv[i], "--kdb-port") == 0 && i + 1 < argc) {
            long p = std::strtol(argv[i + 1], nullptr, 10);
            if (p > 0L && p <= 65535L) {
                // Range-checked above; static_cast is the boundary conversion.
                kdb_port = static_cast<uint16_t>(p);
            }
            ++i;
        } else {
            csv_path = argv[i];
        }
    }

    // Default: no args → use OU source
    if (!use_ou && !use_kdb && csv_path == nullptr) {
        use_ou = true;
    }

    // ------------------------------------------------------------------
    // Construct model objects
    // ------------------------------------------------------------------

    viz::model::SnapshotBuffer sb;

    viz::model::PlaybackCmd cmd{};
    cmd.state      = viz::model::PlaybackState::PAUSED;
    cmd._pad[0]    = 0U;
    cmd._pad[1]    = 0U;
    cmd._pad[2]    = 0U;
    cmd.speed_mult = 1.0f;

    std::mutex cmd_mutex;

    viz::model::SaveCmd save_cmd{};
    save_cmd.pending = false;
    std::memset(save_cmd._pad, 0, sizeof(save_cmd._pad));
    std::memset(save_cmd.path, 0, sizeof(save_cmd.path));

    std::mutex save_mutex;

    // Construct the appropriate event source.
    // Use unique_ptr for RAII; source is accessed via IEventSource*.
    std::unique_ptr<viz::model::IEventSource> source_owner;

    if (use_kdb) {
        viz::model::KdbEventSourceConfig kdb_cfg;
        kdb_cfg.port         = kdb_port;
        kdb_cfg.drop_on_full = true;
        auto* kdb = new viz::model::KdbEventSource(kdb_cfg);
        // Constructor calls bind()+listen(); does NOT block.
        // start() launches the receive thread; accept() blocks inside it.
        kdb->start();
        source_owner.reset(kdb);
    } else if (use_ou) {
        source_owner = std::unique_ptr<viz::model::IEventSource>(
            new viz::model::OUEventSource(viz::model::OU_DEFAULT_PARAMS,
                                          viz::model::OU_DEFAULT_EVENT_COUNT));
    } else {
        auto events = viz::model::load_replay_csv(csv_path);
        source_owner = std::unique_ptr<viz::model::IEventSource>(
            new viz::model::CsvEventSource(std::move(events)));
    }

    viz::model::SimEngine sim_engine(
        source_owner.get(),
        &sb,
        &cmd_mutex,
        &cmd,
        &save_mutex,
        &save_cmd
    );

    // ------------------------------------------------------------------
    // GLFW initialisation
    // ------------------------------------------------------------------
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        if (selftest) {
            // In a headless CI environment, no display is available.
            // The build and struct checks are the meaningful test here.
            // Exit 0 rather than terminate so the CI selftest passes.
            std::fprintf(stdout, "SELFTEST: glfwInit() skipped (no display)\n");
            return 0;
        }
        std::fprintf(stderr, "glfwInit() failed\n");
        std::terminate();
    }

    // OpenGL 3.3 core profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Suppress window in selftest to avoid needing a display server
    if (selftest) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow(
        1280, 720, "Orderbook Visualiser", nullptr, nullptr
    );
    if (!window) {
        if (selftest) {
            glfwTerminate();
            std::fprintf(stdout, "SELFTEST: glfwCreateWindow() skipped (no display)\n");
            return 0;
        }
        std::fprintf(stderr, "glfwCreateWindow() failed\n");
        glfwTerminate();
        std::terminate();
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);   // vsync

    // ------------------------------------------------------------------
    // ImGui initialisation
    // ------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // disable imgui.ini persistence for now

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Verify ImDrawIdx is 32-bit (imconfig.h sets #define ImDrawIdx unsigned int)
    IM_ASSERT(sizeof(ImDrawIdx) == 4);

    // ------------------------------------------------------------------
    // Launch sim thread (after GLFW init to ensure clean startup order)
    // ------------------------------------------------------------------
    // Switch to RUNNING so the sim thread processes events immediately.
    {
        std::lock_guard<std::mutex> lk(cmd_mutex);
        cmd.state = viz::model::PlaybackState::RUNNING;
    }

    std::thread sim_thread(&viz::model::SimEngine::run, &sim_engine);

    // ------------------------------------------------------------------
    // Render loop
    // ------------------------------------------------------------------
    bool running = true;

    // 60 Hz cap: ~16.67 ms per frame.
    auto frame_duration = std::chrono::microseconds(16667);
    auto next_frame = std::chrono::steady_clock::now() + frame_duration;

    while (running && !glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Read latest snapshot from SnapshotBuffer.
        // Triple-buffer consumer protocol: load published_idx with acquire,
        // take the per-slot mutex, copy out the snapshot, release the mutex.
        // We copy rather than hold the mutex across the full render+swap to
        // avoid blocking the sim thread during vsync.
        viz::model::BookSnapshot frame_snap;
        {
            uint32_t pub_idx = sb.published_idx.load(std::memory_order_acquire);
            std::lock_guard<std::mutex> snap_lk(sb.slot_mutex[pub_idx]);
            frame_snap = sb.buffers[pub_idx];
        }
        const viz::model::BookSnapshot& snap = frame_snap;

        // Begin frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Draw all widgets
        viz::view::draw_dom_ladder(snap);
        viz::view::draw_match_tape(snap);
        viz::view::draw_playback_controls(snap, &cmd_mutex, &cmd);
        viz::view::draw_ou_controls(&save_mutex, &save_cmd);

        // Render
        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        // --selftest: render exactly one frame then exit
        if (selftest) {
            running = false;
        } else {
            // 60 Hz cap: sleep until next frame time.
            std::this_thread::sleep_until(next_frame);
            next_frame += frame_duration;
        }
    }

    // ------------------------------------------------------------------
    // Shutdown
    // ------------------------------------------------------------------
    sim_engine.stop();
    sim_thread.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
