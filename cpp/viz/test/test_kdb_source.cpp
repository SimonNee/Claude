/* test_kdb_source.cpp — KdbEventSource end-to-end test
 *
 * Simulates a KDB+ sender using a raw POSIX TCP socket from the test process.
 * Constructs 48-byte KDB+ IPC frames manually (fixed format per spec), sends
 * 10 events, and verifies all 10 are received with correct field values.
 * Tests stop() and destructor for clean exit (no hang, no double-join).
 *
 * No KDB+ required.  No test framework.
 *
 * Build flags: -std=c++17 -O2 -march=native -Wall -Wextra
 *              -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *              -fsanitize=thread
 *
 * Output: "PASS  10 events received and verified\n" on success (CTest match).
 *         "FAIL: reason\n" to stderr, return 1, on any failure.
 */

#include <arpa/inet.h>        // htons, htonl, INADDR_LOOPBACK
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <netinet/in.h>       // sockaddr_in
#include <sys/socket.h>       // socket, connect, send
#include <thread>
#include <unistd.h>           // close

#include "kdb_event_source.hpp"
#include "replay_event.hpp"

// ---------------------------------------------------------------------------
// Test port: high unprivileged port to avoid conflicts with default 7890.
// ---------------------------------------------------------------------------
static constexpr uint16_t TEST_PORT = 17890U;

// ---------------------------------------------------------------------------
// 16-byte KDB+ IPC envelope for a 32-byte byte vector (little-endian, async,
// uncompressed).  Fixed format — see spec Data Model / KDB+ IPC Frame Layout.
// ---------------------------------------------------------------------------
static const uint8_t KDB_ENVELOPE[16] = {
    0x01U, 0x00U, 0x00U, 0x00U,   // bytes  0-3:  LE magic, async, uncompressed, reserved
    0x30U, 0x00U, 0x00U, 0x00U,   // bytes  4-7:  total message length = 48 (0x30) LE
    0x04U, 0x00U, 0x00U, 0x00U,   // bytes  8-11: type=4 (byte vector), attr=0
    0x20U, 0x00U, 0x00U, 0x00U    // bytes 12-15: count = 32 (0x20) LE
};

int main() {
    // -----------------------------------------------------------------------
    // Step 1: construct KdbEventSource
    // -----------------------------------------------------------------------
    viz::model::KdbEventSourceConfig cfg;
    cfg.port         = TEST_PORT;
    cfg.drop_on_full = true;

    viz::model::KdbEventSource source(cfg);
    // Constructor has called bind()+listen(); source is in LISTENING state.

    // -----------------------------------------------------------------------
    // Step 2: start()
    // -----------------------------------------------------------------------
    source.start();
    // Receive thread launched; blocking on accept() internally.

    // -----------------------------------------------------------------------
    // Step 3: connect from test process (simulating KDB+)
    // -----------------------------------------------------------------------
    int sender_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sender_fd < 0) {
        std::fprintf(stderr, "FAIL: socket() for sender: %s\n", std::strerror(errno));
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family      = static_cast<sa_family_t>(AF_INET);
    addr.sin_port        = ::htons(TEST_PORT);
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);

    int rc = ::connect(sender_fd,
                       reinterpret_cast<const struct sockaddr*>(&addr),
                       static_cast<socklen_t>(sizeof(addr)));
    if (rc != 0) {
        std::fprintf(stderr, "FAIL: connect() to port %u: %s\n",
                     static_cast<unsigned>(TEST_PORT), std::strerror(errno));
        ::close(sender_fd);
        return 1;
    }

    // Allow receive_loop's accept() to complete before sending.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // -----------------------------------------------------------------------
    // KDB+ IPC handshake: send null-terminated capability string, read reply
    // -----------------------------------------------------------------------
    {
        const char handshake[] = "user:pass\3";   // null-terminator from array
        if (::send(sender_fd, handshake, sizeof(handshake), 0)
                != static_cast<ssize_t>(sizeof(handshake))) {
            std::fprintf(stderr, "FAIL: handshake send failed\n");
            ::close(sender_fd); return 1;
        }
        char cap = '\0';
        if (::recv(sender_fd, &cap, 1, 0) != 1) {
            std::fprintf(stderr, "FAIL: handshake recv failed\n");
            ::close(sender_fd); return 1;
        }
    }

    // -----------------------------------------------------------------------
    // Step 4: send 10 events as 48-byte KDB+ IPC frames
    // -----------------------------------------------------------------------
    for (uint32_t i = 0U; i < 10U; ++i) {
        viz::model::ReplayEvent ev{};
        ev.timestamp_ns = static_cast<uint64_t>(i + 1U) * 1000000ULL;
        ev.tick         = 2000U + i;
        ev._pad         = 0U;
        ev.qty          = 100000000ULL * static_cast<uint64_t>(i + 1U);
        ev.side         = static_cast<uint8_t>(i % 2U);
        // _pad2 zero-initialised by ev{}

        char frame[48];
        std::memcpy(frame,       KDB_ENVELOPE, 16);
        std::memcpy(frame + 16,  &ev,          32);

        ssize_t sent = ::send(sender_fd, frame, 48, 0);
        if (sent != 48) {
            std::fprintf(stderr, "FAIL: send() returned %zd (expected 48)\n", sent);
            ::close(sender_fd);
            return 1;
        }
    }

    // -----------------------------------------------------------------------
    // Step 5: receive and verify 10 events
    // -----------------------------------------------------------------------
    uint32_t received = 0U;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    while (received < 10U &&
           std::chrono::steady_clock::now() < deadline) {

        viz::model::ReplayEvent out{};
        if (source.next_event(out)) {
            // Verify all fields.
            uint64_t expected_ts   = static_cast<uint64_t>(received + 1U) * 1000000ULL;
            uint32_t expected_tick = 2000U + received;
            uint64_t expected_qty  = 100000000ULL * static_cast<uint64_t>(received + 1U);
            uint8_t  expected_side = static_cast<uint8_t>(received % 2U);

            if (out.timestamp_ns != expected_ts) {
                std::fprintf(stderr, "FAIL: event %d timestamp_ns mismatch"
                             " (got %llu, want %llu)\n",
                             received,
                             static_cast<unsigned long long>(out.timestamp_ns),
                             static_cast<unsigned long long>(expected_ts));
                ::close(sender_fd);
                return 1;
            }
            if (out.tick != expected_tick) {
                std::fprintf(stderr, "FAIL: event %d tick mismatch"
                             " (got %u, want %u)\n",
                             received, out.tick, expected_tick);
                ::close(sender_fd);
                return 1;
            }
            if (out._pad != 0U) {
                std::fprintf(stderr, "FAIL: event %d _pad != 0\n", received);
                ::close(sender_fd);
                return 1;
            }
            if (out.qty != expected_qty) {
                std::fprintf(stderr, "FAIL: event %d qty mismatch"
                             " (got %llu, want %llu)\n",
                             received,
                             static_cast<unsigned long long>(out.qty),
                             static_cast<unsigned long long>(expected_qty));
                ::close(sender_fd);
                return 1;
            }
            if (out.side != expected_side) {
                std::fprintf(stderr, "FAIL: event %d side mismatch"
                             " (got %u, want %u)\n",
                             received,
                             static_cast<unsigned>(out.side),
                             static_cast<unsigned>(expected_side));
                ::close(sender_fd);
                return 1;
            }

            ++received;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (received != 10) {
        std::fprintf(stderr, "FAIL: received %d events, expected 10\n", received);
        ::close(sender_fd);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Step 6: verify event_count() and is_live()
    // -----------------------------------------------------------------------
    if (source.event_count() != std::numeric_limits<std::size_t>::max()) {
        std::fprintf(stderr, "FAIL: event_count() != SIZE_MAX\n");
        ::close(sender_fd);
        return 1;
    }
    if (!source.is_live()) {
        std::fprintf(stderr, "FAIL: is_live() returned false\n");
        ::close(sender_fd);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Step 7: stop and cleanup
    // -----------------------------------------------------------------------
    ::close(sender_fd);      // peer disconnect; helps receive_loop exit cleanly
    source.stop();           // must return without hanging

    // ~KdbEventSource() is called at end of scope; must not double-join.

    // -----------------------------------------------------------------------
    // Step 8: verify reset() is a no-op (safe to call after stop())
    // -----------------------------------------------------------------------
    source.reset();   // must return immediately

    // -----------------------------------------------------------------------
    // Step 9: print PASS
    // -----------------------------------------------------------------------
    std::printf("PASS  10 events received and verified\n");
    return 0;
}
