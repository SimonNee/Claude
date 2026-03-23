/* snapshot_buffer.hpp — SnapshotBuffer: atomic double-buffer for snapshots
 *
 * The sim thread writes to the inactive buffer then increments the generation
 * counter (release).  The render thread reads from the buffer indexed by
 * generation & 1 (acquire).  No mutex on the hot path.
 *
 * Producer protocol (sim thread, inside SimEngine::publish_snapshot()):
 *   1. wi = (generation.load(relaxed) + 1) & 1
 *   2. Write all fields of buffers[wi]
 *   3. generation.fetch_add(1, release)
 *
 * Consumer protocol (render thread, inside RenderLoop::read_snapshot()):
 *   1. gen = generation.load(acquire)
 *   2. Read from buffers[gen & 1]
 *   3. No lock required.  A one-frame-old snapshot is acceptable.
 *
 * SnapshotBuffer is not copyable or movable.  Construct once in main.cpp.
 *
 * Includes: <atomic>, book_snapshot.hpp
 * Namespace: viz::model
 */

#pragma once

#include <atomic>
#include "book_snapshot.hpp"

namespace viz::model {

// ---------------------------------------------------------------------------
// SnapshotBuffer  (spec: Data Model / SnapshotBuffer)
// ---------------------------------------------------------------------------

struct SnapshotBuffer {
    BookSnapshot          buffers[2];    // two statically allocated snapshots
    std::atomic<uint32_t> generation;   // incremented by sim thread after each write

    // Not copyable or movable.
    SnapshotBuffer() : generation(0U) {}
    SnapshotBuffer(const SnapshotBuffer&)            = delete;
    SnapshotBuffer& operator=(const SnapshotBuffer&) = delete;
    SnapshotBuffer(SnapshotBuffer&&)                 = delete;
    SnapshotBuffer& operator=(SnapshotBuffer&&)      = delete;
};

} // namespace viz::model
