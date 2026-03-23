/* kdb_event_source.cpp — KdbEventSource implementation
 *
 * POSIX sockets only; no new link dependencies.
 * Wire format: 48-byte KDB+ IPC frame; ReplayEvent at bytes [16..47].
 * Little-endian assumption: KDB+/q on Linux x86-64 == C++ x86-64 layout.
 * No byte-swap required.
 *
 * Build flags: -std=c++17 -O2 -march=native -Wall -Wextra
 *              -Wconversion -Wsign-conversion -Werror -fno-exceptions
 *
 * Pitfalls addressed:
 *   Pitfall 4  — no virtual on hot-path structs (KdbEventSource is final).
 *   Pitfall 5  — no exceptions; failures call std::terminate() or return false.
 *   Pitfall 7  — no inadvertent copies; ReplayEvent passed by const& to try_push.
 *   Pitfall 10 — recv() returns ssize_t; all comparisons use ssize_t to avoid
 *                signed/unsigned mismatch; no implicit promotions.
 */

#include "kdb_event_source.hpp"

#include <arpa/inet.h>       // htons
#include <cerrno>
#include <cstdio>            // fprintf, stderr
#include <exception>         // std::terminate
#include <cstring>           // memcpy, strerror
#include <limits>            // numeric_limits
#include <netinet/in.h>      // sockaddr_in, INADDR_ANY
#include <sys/socket.h>      // socket, setsockopt, bind, listen, accept, recv, shutdown
#include <unistd.h>          // close

namespace viz::model {

// Verify ReplayEvent layout at this TU's compilation (belt-and-suspenders;
// static_assert already present in replay_event.hpp).
static_assert(sizeof(ReplayEvent)  == 32U, "ReplayEvent layout changed");
static_assert(alignof(ReplayEvent) ==  8U, "ReplayEvent alignment changed");

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

KdbEventSource::KdbEventSource(KdbEventSourceConfig cfg)
    : cfg_(cfg)
    , queue_()
    , listen_fd_(-1)
    , accept_fd_(-1)
    , stop_flag_(false)
    , recv_thread_()
    , events_dropped_(0U)
{
    // Create TCP socket.
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "KdbEventSource: socket() failed: %s\n",
                     std::strerror(errno));
        std::terminate();
    }

    // SO_REUSEADDR: allow rapid restart without TIME_WAIT delay.
    // setsockopt takes socklen_t for optlen; cast sizeof explicitly.
    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                     static_cast<socklen_t>(sizeof(opt))) < 0) {
        std::fprintf(stderr, "KdbEventSource: setsockopt(SO_REUSEADDR) failed: %s\n",
                     std::strerror(errno));
        ::close(fd);
        std::terminate();
    }

    // Bind to INADDR_ANY:port.
    // sin_family is sa_family_t (uint16_t); AF_INET is an int constant.
    // static_cast avoids -Wconversion on the assignment.
    struct sockaddr_in addr{};
    addr.sin_family      = static_cast<sa_family_t>(AF_INET);
    addr.sin_port        = ::htons(cfg_.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(fd, reinterpret_cast<const struct sockaddr*>(&addr),
               static_cast<socklen_t>(sizeof(addr))) < 0) {
        std::fprintf(stderr, "KdbEventSource: bind(port=%u) failed: %s\n",
                     static_cast<unsigned>(cfg_.port), std::strerror(errno));
        ::close(fd);
        std::terminate();
    }

    // listen() with backlog 1 — one KDB+ connection expected.
    if (::listen(fd, 1) < 0) {
        std::fprintf(stderr, "KdbEventSource: listen() failed: %s\n",
                     std::strerror(errno));
        ::close(fd);
        std::terminate();
    }

    listen_fd_ = fd;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

KdbEventSource::~KdbEventSource() {
    stop();   // idempotent; safe if already called
}

// ---------------------------------------------------------------------------
// start — launch receive thread
// ---------------------------------------------------------------------------

void KdbEventSource::start() {
    recv_thread_ = std::thread(&KdbEventSource::receive_loop, this);
}

// ---------------------------------------------------------------------------
// next_event — consumer side; called from sim thread only
//
// Does NOT spin.  Calls front() once; returns false if empty.
// The sim thread's existing 5 ms sleep on empty provides back-off.
// ---------------------------------------------------------------------------

bool KdbEventSource::next_event(ReplayEvent& out) {
    ReplayEvent* p = queue_.front();
    if (p == nullptr) {
        return false;
    }
    out = *p;
    queue_.pop();
    return true;
}

// ---------------------------------------------------------------------------
// reset — no-op for live feed
// ---------------------------------------------------------------------------

void KdbEventSource::reset() {
    // Intentional no-op.  Rewind is meaningless for a live push feed.
    // SimEngine calls reset() when a finite source is exhausted; for
    // KdbEventSource, next_event() returning false means the queue is
    // momentarily empty — not permanently exhausted.
}

// ---------------------------------------------------------------------------
// event_count — returns SIZE_MAX for unbounded live feed
// ---------------------------------------------------------------------------

std::size_t KdbEventSource::event_count() const {
    return std::numeric_limits<std::size_t>::max();
}

// ---------------------------------------------------------------------------
// stop — signal receive thread to exit; close fds; join thread
//
// Idempotent: stop_flag_.exchange(true) acts as a guard.
// ---------------------------------------------------------------------------

void KdbEventSource::stop() {
    bool already_stopped = stop_flag_.exchange(true, std::memory_order_acq_rel);
    if (already_stopped) {
        return;
    }

    // shutdown() the accepted fd to unblock recv() in the receive thread.
    // Do NOT close() here — the receive thread closes the fd in its own
    // cleanup step after recv() returns.  Calling close() from two threads
    // on the same fd is a data race under TSan.
    int afd = accept_fd_.load(std::memory_order_acquire);
    if (afd != -1) {
        ::shutdown(afd, SHUT_RDWR);
        // accept_fd_ left as-is; receive_loop step 3 exchanges it to -1 and closes.
    }

    // Close listen fd: also unblocks accept() if the receive thread is still
    // waiting there (race window: between start() and accept() completing).
    if (listen_fd_ != -1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    // Join receive thread.  Belt-and-suspenders: check joinable() first.
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
}

// ---------------------------------------------------------------------------
// receive_loop — receive thread entry point (private)
// ---------------------------------------------------------------------------

void KdbEventSource::receive_loop() {
    // Step 1: accept one connection.
    int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) {
        // accept() failed.  If we're stopping, this is expected (listen_fd_
        // was closed by stop()).  Otherwise, it's unrecoverable.
        if (!stop_flag_.load(std::memory_order_acquire)) {
            std::fprintf(stderr, "KdbEventSource: accept() failed: %s\n",
                         std::strerror(errno));
        }
        return;
    }
    // Store accepted fd with release ordering so stop() can observe it.
    accept_fd_.store(fd, std::memory_order_release);

    // Step 2: receive loop — one 48-byte IPC frame per event.
    while (!stop_flag_.load(std::memory_order_acquire)) {
        char ipc_buf[KDB_IPC_FRAME_SIZE];   // 48 bytes on the stack
        int  cur_fd = accept_fd_.load(std::memory_order_relaxed);
        if (!recv_exact(cur_fd, ipc_buf, KDB_IPC_FRAME_SIZE)) {
            break;   // disconnect, error, or stop
        }

        ReplayEvent ev{};
        std::memcpy(&ev, ipc_buf + KDB_IPC_PAYLOAD_OFFSET, sizeof(ReplayEvent));

        if (!queue_.try_push(ev)) {
            // Queue full — drop frame.  events_dropped_ is a diagnostic counter.
            events_dropped_.fetch_add(1U, std::memory_order_relaxed);
        }
    }

    // Step 3: cleanup — store -1 to accept_fd_ so stop() knows the fd is gone.
    int old_fd = accept_fd_.exchange(-1, std::memory_order_acq_rel);
    if (old_fd != -1) {
        ::close(old_fd);
    }
}

// ---------------------------------------------------------------------------
// recv_exact — loop until exactly n bytes received (private)
//
// recv() returns ssize_t.  All size comparisons use ssize_t to avoid
// -Wsign-conversion warnings.  'n' is std::size_t but the cast to ssize_t is
// safe: KDB_IPC_FRAME_SIZE (48) is far within ssize_t range.
// ---------------------------------------------------------------------------

bool KdbEventSource::recv_exact(int fd, void* buf, std::size_t n) {
    char*    ptr       = static_cast<char*>(buf);
    ssize_t  remaining = static_cast<ssize_t>(n);   // 48 — safe cast

    while (remaining > 0) {
        // Check stop flag at the top of each iteration.
        if (stop_flag_.load(std::memory_order_acquire)) {
            return false;
        }

        ssize_t nread = ::recv(fd, ptr, static_cast<std::size_t>(remaining), 0);

        if (nread == 0) {
            // Clean disconnect: peer closed connection.
            return false;
        }
        if (nread < 0) {
            if (errno == EINTR) {
                // Signal interrupted recv — retry.
                continue;
            }
            // Real error (ECONNRESET, EBADF from stop()'s close, etc.).
            return false;
        }

        ptr       += nread;
        remaining -= nread;
    }

    return true;
}

} // namespace viz::model
