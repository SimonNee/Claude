/* test_threading.cpp — TSan threading test for SimEngine + SnapshotBuffer
 *
 * Verifies that the sim thread and a simulated render thread can run
 * concurrently with no data races detected by TSan.
 *
 * Does NOT open a GLFW window.  Tests threading and synchronisation only.
 *
 * Build: -fsanitize=thread (see CMakeLists.txt test_threading target)
 * Pass condition: TSan reports zero data races; output contains "PASS".
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

// Include model sources directly (test_threading is standalone, no link to
// viz library).  Headers first, then .cpp implementations.
#include "snapshot_buffer.hpp"
#include "playback_cmd.hpp"
#include "save_cmd.hpp"
#include "event_source.hpp"
#include "replay_event.hpp"
#include "ou_event_source.hpp"
#include "sim_engine.hpp"

// Implementation files — included directly so test_threading links standalone.
#include "ou_event_source.cpp"
#include "csv_event_source.cpp"
#include "sim_engine.cpp"

// ETH book implementation — on include path as cpp/eth/cpp
#include "book.cpp"

int main() {
    // -----------------------------------------------------------------------
    // Construct objects
    // -----------------------------------------------------------------------

    viz::model::OUParams params = viz::model::OU_DEFAULT_PARAMS;
    // Use a small event count so the test completes quickly.
    static constexpr std::size_t TEST_EVENT_COUNT = 1000U;

    viz::model::SnapshotBuffer sb;

    viz::model::PlaybackCmd cmd{};
    cmd.state      = viz::model::PlaybackState::RUNNING;   // start running immediately
    cmd._pad[0]    = 0U;
    cmd._pad[1]    = 0U;
    cmd._pad[2]    = 0U;
    cmd.speed_mult = 16.0f;   // run at 16x speed to finish quickly

    std::mutex cmd_mutex;

    viz::model::SaveCmd save_cmd{};
    save_cmd.pending = false;
    std::mutex save_mutex;

    // OUEventSource constructed before SimEngine — SimEngine holds non-owning ptr.
    viz::model::OUEventSource source(params, TEST_EVENT_COUNT);

    viz::model::SimEngine sim_engine(
        &source,
        &sb,
        &cmd_mutex,
        &cmd,
        &save_mutex,
        &save_cmd
    );

    // -----------------------------------------------------------------------
    // Launch sim thread
    // -----------------------------------------------------------------------
    std::thread sim_thread(&viz::model::SimEngine::run, &sim_engine);

    // -----------------------------------------------------------------------
    // Main thread: simulate render thread — read generation counter for ~100ms
    // -----------------------------------------------------------------------
    uint32_t last_gen = 0U;
    uint32_t gen_increments = 0U;

    auto start = std::chrono::steady_clock::now();
    auto end   = start + std::chrono::milliseconds(100);

    while (std::chrono::steady_clock::now() < end) {
        // Triple-buffer consumer protocol: acquire load of published_idx,
        // take the per-slot mutex (TSan synchronisation edge), then read.
        uint32_t pub_idx = sb.published_idx.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> snap_lk(sb.slot_mutex[pub_idx]);
            const viz::model::BookSnapshot& snap = sb.buffers[pub_idx];
            volatile uint32_t dummy = snap.best_bid_tick;
            (void)dummy;
        }

        uint32_t gen = sb.generation.load(std::memory_order_acquire);
        if (gen != last_gen) {
            ++gen_increments;
            last_gen = gen;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    // -----------------------------------------------------------------------
    // Stop sim thread
    // -----------------------------------------------------------------------
    sim_engine.stop();
    sim_thread.join();

    // -----------------------------------------------------------------------
    // Assertions
    // -----------------------------------------------------------------------

    // The sim thread must have published at least one snapshot.
    // With 1000 events at 16x speed, this should always be true.
    if (gen_increments == 0U) {
        std::fprintf(stderr, "FAIL: generation counter never incremented\n");
        return 1;
    }

    std::printf("PASS  gen_increments=%u  final_gen=%u\n",
                static_cast<unsigned int>(gen_increments),
                static_cast<unsigned int>(last_gen));
    return 0;
}
