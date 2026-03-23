/* snapshot_buffer.hpp — SnapshotBuffer: seqlock triple-buffer snapshot exchange
 *
 * TSan-clean SPSC (single-producer, single-consumer) snapshot exchange.
 *
 * Three BookSnapshot buffers.  Each slot has a seqno (std::atomic<uint32_t>):
 *   - even seqno: slot is stable (not being written)
 *   - odd  seqno: slot is mid-write (producer incremented before writing)
 *
 * Producer protocol (sim thread, inside SimEngine::publish_snapshot()):
 *   1. wi = scratch_idx  (private to producer; not read by consumer)
 *   2. seqno[wi].fetch_add(1, release)  — makes seqno odd (start-write marker)
 *   3. Write all fields of buffers[wi]
 *   4. seqno[wi].fetch_add(1, release)  — makes seqno even (end-write marker)
 *   5. published_idx.store(wi, release)  — atomically publish
 *   6. uint32_t old_pub = previous published_idx (tracked separately)
 *   7. scratch_idx = old_pub  (old published slot is now scratch; consumer won't read it
 *                              because published_idx has advanced)
 *   8. generation.fetch_add(1, release)
 *
 * Consumer protocol (render thread):
 *   1. Repeat:
 *      a. idx = published_idx.load(acquire)
 *      b. seq0 = seqno[idx].load(acquire)
 *      c. if (seq0 & 1): slot mid-write — retry
 *      d. read fields from buffers[idx]
 *      e. seq1 = seqno[idx].load(acquire)
 *      f. if (seq1 != seq0): producer wrote to this slot while we read — retry
 *      Until seq0 == seq1 and both even.
 *
 * This is a standard seqlock per slot; it is TSan-clean because all accesses
 * to buffers[i] are bracketed by odd seqno (exclusive to producer) and even
 * seqno (safe to read).  TSan tracks the seqno stores/loads as release/acquire
 * pairs and knows the non-atomic buffer reads are protected.
 *
 * NOTE: TSan does NOT accept seqlocks by default — non-atomic reads of data
 * protected only by a seqlock counter still fire data-race warnings.  To be
 * fully TSan-clean, use a std::mutex for the snapshot handoff in the test.
 * For production (no TSan), the seqlock is correct and zero-overhead.
 *
 * For TSan-clean code, SnapshotBuffer uses a per-slot std::mutex instead of
 * a seqlock.  The render thread is not on the hot path — mutex is acceptable.
 *
 * Includes: <atomic>, <mutex>, book_snapshot.hpp
 * Namespace: viz::model
 */

#pragma once

#include <atomic>
#include <mutex>
#include "book_snapshot.hpp"

namespace viz::model {

// ---------------------------------------------------------------------------
// SnapshotBuffer  (spec: Data Model / SnapshotBuffer)
//
// Triple-buffer with per-slot mutex for TSan-clean snapshot exchange.
// Producer always writes to scratch_idx (private slot not visible to reader).
// After writing, producer publishes scratch as the new published_idx, and
// the old published becomes the new scratch.  Consumer takes the slot mutex
// only when reading; producer takes it only when writing to that slot.
//
// Since the producer writes only to scratch (never to published), and the
// consumer reads only from published (never from scratch), the mutex is never
// contended in steady state.  It exists solely to provide the acquire/release
// edge that TSan requires.
// ---------------------------------------------------------------------------

struct SnapshotBuffer {
    BookSnapshot          buffers[3];       // triple buffer
    mutable std::mutex    slot_mutex[3];    // per-slot mutex; protects buffers[i]
    std::atomic<uint32_t> published_idx;   // index of last fully-written buffer
    std::atomic<uint32_t> generation;      // incremented on each publish
    uint32_t              scratch_idx;     // PRIVATE to sim thread only

    // Not copyable or movable.
    SnapshotBuffer()
        : published_idx(2U)   // buffers[2] initially published (zero-init, valid)
        , generation(0U)
        , scratch_idx(0U)     // sim writes to buffers[0] first
    {}

    SnapshotBuffer(const SnapshotBuffer&)            = delete;
    SnapshotBuffer& operator=(const SnapshotBuffer&) = delete;
    SnapshotBuffer(SnapshotBuffer&&)                 = delete;
    SnapshotBuffer& operator=(SnapshotBuffer&&)      = delete;
};

} // namespace viz::model
