// AdHocQueue (spec SPEC-core-session.md, "Ad-hoc requests"): a FIFO ring arena of pre-encoded
// chunk frames + read/write payload space, and a fixed-capacity ring of jobs. Every chunk is
// recomputed on demand from a job's own `pointsCompleted` (the same head-offset/step-per-unit
// arithmetic src/core/model/chunk.cpp uses, spec section 8.5) rather than pre-split into a
// stored array, so nothing here allocates past construction.
#include "adhoc_queue.h"

#include "mc/core/device.h"
#include "mc/core/limits.h"

#include <algorithm>

namespace mc::detail {
namespace {

Error queueFullError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Transport;
    e.code = ErrorCode::QueueFull;
    e.message = message;
    return e;
}

Error pointCountError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::PointCount;
    e.message = message;
    return e;
}

// Spec section 2.4 / chunk.cpp: a word-unit step over a bit device advances the device number by
// 16 per word; every other combination steps by 1 per point.
uint32_t stepPerUnitFor(Op op, DeviceKind kind) noexcept {
    bool isWordOp = (op == Op::ReadWords || op == Op::WriteWords);
    return (isWordOp && kind == DeviceKind::Bit) ? 16u : 1u;
}

// The chunk starting at native point offset `pointOffset` (from the job's own head), covering
// `chunkPoints` points, as a Request -- mirrors chunk.cpp's own per-index formula, computed here
// from a running offset instead of a stored array. `chunkData` is the write's own data for this
// chunk (ignored for a read); its size must already match `chunkPoints`/`bitLayout`.
Request chunkRequestAt(const AdHocJob& job, uint32_t pointOffset, uint16_t chunkPoints,
                       ByteView chunkData) noexcept {
    const DeviceInfo& info = deviceInfo(job.head.type);
    uint32_t stepPerUnit = stepPerUnitFor(job.op, info.kind);
    Device chunkHead{job.head.type, job.head.number + pointOffset * stepPerUnit};

    switch (job.op) {
    case Op::ReadWords:
        return Request::readWords(chunkHead, chunkPoints);
    case Op::ReadBits: {
        // Request::readBits() has no bitLayout parameter (unlike writeBits()): it always
        // defaults to BytePerPoint, so the job's own bitLayout must be set explicitly here or a
        // PackedLsbFirst ad-hoc read would silently decode as BytePerPoint instead.
        Request r = Request::readBits(chunkHead, chunkPoints);
        r.bitLayout = job.bitLayout;
        return r;
    }
    case Op::WriteWords:
        return Request::writeWords(chunkHead, chunkData);
    case Op::WriteBits:
        return Request::writeBits(chunkHead, chunkData, job.bitLayout);
    }
    return Request::readWords(chunkHead, chunkPoints); // Unreachable; Op has no other value.
}

// Byte offset, within the write's own data (or the read's own accumulated payload), that point
// offset `pointOffset` starts at -- matches chunk.cpp's own dataOffset formula.
uint64_t byteOffsetFor(Op op, BitLayout bitLayout, uint32_t pointOffset) noexcept {
    if (op == Op::WriteWords || op == Op::ReadWords) {
        return static_cast<uint64_t>(pointOffset) * 2;
    }
    // *Bits: BytePerPoint is one byte per point; PackedLsbFirst is eight points per byte. Every
    // v1 bit limit chunk.cpp's own comment cites is a multiple of 8, so `pointOffset` (always 0
    // or a multiple of maxPoints()) is always byte-aligned here too.
    return (bitLayout == BitLayout::PackedLsbFirst) ? pointOffset / 8 : pointOffset;
}

} // namespace

AdHocQueue::AdHocQueue(const FrameConfig& frame, uint16_t capacity, uint32_t arenaBytes)
    : m_jobs(capacity), m_arena(arenaBytes), m_frame(frame) {}

AdHocJob& AdHocQueue::frontJob() noexcept { return m_jobs[m_jobHead]; }

const AdHocJob& AdHocQueue::frontJob() const noexcept { return m_jobs[m_jobHead]; }

AdHocJob* AdHocQueue::frontUnfinished() noexcept {
    // Bounded by m_jobCount, itself bounded by the configured (fixed, typically small) capacity.
    for (size_t i = 0; i < m_jobCount; ++i) {
        AdHocJob& j = m_jobs[(m_jobHead + i) % m_jobs.size()];
        if (!j.completed) {
            return &j;
        }
    }
    return nullptr;
}

const AdHocJob* AdHocQueue::frontUnfinished() const noexcept {
    return const_cast<AdHocQueue*>(this)->frontUnfinished();
}

bool AdHocQueue::hasWork() const noexcept { return frontUnfinished() != nullptr; }

Expected<RequestId> AdHocQueue::submit(const Request& r) noexcept {
    auto countResult = chunkCount(r, m_frame);
    if (!countResult.hasValue()) {
        return Expected<RequestId>(countResult.error());
    }
    size_t n = countResult.value();

    if (m_jobCount >= m_jobs.size()) {
        return Expected<RequestId>(queueFullError("adHocCapacity reached"));
    }

    const DeviceInfo& info = deviceInfo(r.head.type);
    uint16_t maxPts = maxPoints(m_frame, r.op, info.kind);
    McProtocol proto(m_frame);

    // Pass 1: sum every chunk's own encoded frame size. validate()'s own data-size check (rule 7,
    // writes only) only looks at ByteView::size, never dereferences the pointer, so a dummy (but
    // correctly sized) view into this queue's own arena is enough here -- the real per-chunk data
    // view is only needed once framesLength (and so dataStart) is known, in pass 2 below.
    AdHocJob probeJob{};
    probeJob.op = r.op;
    probeJob.head = r.head;
    probeJob.count = r.count;
    probeJob.bitLayout = r.bitLayout;
    probeJob.isWrite = r.isWrite();

    uint64_t framesLength = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t off = static_cast<uint32_t>(i * static_cast<size_t>(maxPts));
        uint16_t chunkPoints =
            static_cast<uint16_t>(std::min<size_t>(r.count - off, maxPts));
        ByteView dummyData{m_arena.data(), r.isWrite() ? r.data.size : 0};
        Request chunkReq = chunkRequestAt(probeJob, off, chunkPoints, dummyData);
        auto sizeResult = proto.encodedSize(chunkReq);
        if (!sizeResult.hasValue()) {
            return Expected<RequestId>(sizeResult.error());
        }
        framesLength += sizeResult.value();
    }

    uint64_t dataLength = r.isWrite() ? r.data.size : proto.payloadSize(r);
    uint64_t needed = framesLength + dataLength;
    if (needed > m_arena.size()) {
        return Expected<RequestId>(
            pointCountError("request needs more bytes than the whole ad-hoc arena"));
    }

    uint64_t physicalTail = m_arenaTail % m_arena.size();
    uint64_t freeAtTail = m_arena.size() - physicalTail;
    uint64_t gap = (needed > freeAtTail) ? freeAtTail : 0;
    uint64_t totalReserve = gap + needed;
    uint64_t used = m_arenaTail - m_arenaHead;
    if (used + totalReserve > m_arena.size()) {
        return Expected<RequestId>(queueFullError("ad-hoc arena is full"));
    }

    uint64_t reserveStart = m_arenaTail;
    uint64_t framesStart = reserveStart + gap;
    uint64_t dataStart = framesStart + framesLength;

    AdHocJob job{};
    job.id = m_nextId++;
    job.op = r.op;
    job.head = r.head;
    job.count = r.count;
    job.bitLayout = r.bitLayout;
    job.isWrite = r.isWrite();
    job.reserveStart = reserveStart;
    job.reserveLength = totalReserve;
    job.framesStart = framesStart;
    job.framesLength = framesLength;
    job.dataStart = dataStart;
    job.dataLength = dataLength;

    // Pass 2: encode every chunk's real frame, now that framesStart/dataStart are fixed. A
    // write's own data is copied into the arena's data region first, so each chunk's own slice of
    // it can be referenced directly (Session::submit()'s own doc: "r.data is not kept").
    if (job.isWrite && dataLength > 0) {
        size_t physicalData = static_cast<size_t>(dataStart % m_arena.size());
        std::copy(r.data.data, r.data.data + r.data.size, m_arena.data() + physicalData);
    }

    size_t frameCursor = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t off = static_cast<uint32_t>(i * static_cast<size_t>(maxPts));
        uint16_t chunkPoints =
            static_cast<uint16_t>(std::min<size_t>(r.count - off, maxPts));
        ByteView chunkData{};
        if (job.isWrite) {
            uint64_t byteOff = byteOffsetFor(r.op, r.bitLayout, off);
            size_t physicalData = static_cast<size_t>((dataStart + byteOff) % m_arena.size());
            size_t chunkDataSize = (r.op == Op::WriteWords)
                                       ? static_cast<size_t>(chunkPoints) * 2
                                       : (r.bitLayout == BitLayout::PackedLsbFirst
                                              ? (static_cast<size_t>(chunkPoints) + 7) / 8
                                              : static_cast<size_t>(chunkPoints));
            chunkData = ByteView{m_arena.data() + physicalData, chunkDataSize};
        }
        Request chunkReq = chunkRequestAt(job, off, chunkPoints, chunkData);

        size_t physicalFrame = static_cast<size_t>((framesStart + frameCursor) % m_arena.size());
        size_t frameCapacity = m_arena.size() - physicalFrame;
        auto encodeResult = proto.encode(
            chunkReq, MutableByteView{m_arena.data() + physicalFrame, frameCapacity});
        if (!encodeResult.hasValue()) {
            // Never expected: chunkCount()/pass 1 already validated this exact chunk against the
            // same frame. No arena state has been committed yet (m_arenaTail untouched), so
            // returning here leaks nothing.
            return Expected<RequestId>(encodeResult.error());
        }
        frameCursor += encodeResult.value();
    }

    m_arenaTail = reserveStart + totalReserve;
    m_jobs[(m_jobHead + m_jobCount) % m_jobs.size()] = job;
    ++m_jobCount;
    return Expected<RequestId>(job.id);
}

Request AdHocQueue::nextChunkRequest() const noexcept {
    const AdHocJob& job = *frontUnfinished();
    const DeviceInfo& info = deviceInfo(job.head.type);
    uint16_t maxPts = maxPoints(m_frame, job.op, info.kind);
    uint16_t chunkPoints =
        static_cast<uint16_t>(std::min<uint32_t>(job.count - job.pointsCompleted, maxPts));

    ByteView chunkData{};
    if (job.isWrite) {
        uint64_t byteOff = byteOffsetFor(job.op, job.bitLayout, job.pointsCompleted);
        size_t physicalData = static_cast<size_t>((job.dataStart + byteOff) % m_arena.size());
        size_t chunkDataSize = (job.op == Op::WriteWords)
                                   ? static_cast<size_t>(chunkPoints) * 2
                                   : (job.bitLayout == BitLayout::PackedLsbFirst
                                          ? (static_cast<size_t>(chunkPoints) + 7) / 8
                                          : static_cast<size_t>(chunkPoints));
        chunkData = ByteView{m_arena.data() + physicalData, chunkDataSize};
    }
    return chunkRequestAt(job, job.pointsCompleted, chunkPoints, chunkData);
}

ByteView AdHocQueue::nextChunkFrame() const noexcept {
    const AdHocJob& job = *frontUnfinished();
    McProtocol proto(m_frame);
    size_t frameSize = proto.encodedSize(nextChunkRequest()).value(); // Validated at submit().
    // job.framesSent is this job's own cursor within its frames region (updated in
    // markChunkSent(), one call per chunk): the next not-yet-sent chunk's frame always starts
    // there, so no per-call re-summation of every earlier chunk's own size is needed.
    size_t physicalFrame =
        static_cast<size_t>((job.framesStart + job.framesSent) % m_arena.size());
    return ByteView{m_arena.data() + physicalFrame, frameSize};
}

void AdHocQueue::markChunkSent(uint16_t chunkPoints) noexcept {
    AdHocJob& job = *frontUnfinished();
    McProtocol proto(m_frame);
    job.framesSent += proto.encodedSize(nextChunkRequest()).value();
    job.inFlightChunkPoints = chunkPoints;
}

MutableByteView AdHocQueue::chunkPayloadDest() const noexcept {
    const AdHocJob& job = *frontUnfinished();
    if (job.isWrite) {
        return MutableByteView{};
    }
    uint64_t byteOff = byteOffsetFor(job.op, job.bitLayout, job.pointsCompleted);
    size_t physical = static_cast<size_t>((job.dataStart + byteOff) % m_arena.size());
    McProtocol proto(m_frame);
    size_t size = proto.payloadSize(nextChunkRequest());
    // AdHocQueue's own const_cast: m_arena is logically mutable "scratch" storage the queue owns
    // outright: exposing a non-const view of a caller-requested slice of it is exactly the
    // arena's own job, not a violation of this object's own const-correctness.
    auto* mutableArena = const_cast<uint8_t*>(m_arena.data());
    return MutableByteView{mutableArena + physical, size};
}

std::optional<AdHocCompletion> AdHocQueue::completeInFlightChunk(bool ok, Error err) noexcept {
    AdHocJob& job = *frontUnfinished();
    uint16_t chunkPoints = job.inFlightChunkPoints;
    job.inFlightChunkPoints = 0;

    if (!ok) {
        job.completed = true;
        AdHocCompletion c{};
        c.id = job.id;
        c.error = err;
        c.payload = ByteView{};
        return c;
    }

    job.pointsCompleted += chunkPoints;
    if (job.pointsCompleted < job.count) {
        return std::nullopt; // More chunks remain.
    }

    job.completed = true;
    AdHocCompletion c{};
    c.id = job.id;
    c.error = Error{};
    if (!job.isWrite && job.dataLength > 0) {
        size_t physical = static_cast<size_t>(job.dataStart % m_arena.size());
        c.payload = ByteView{m_arena.data() + physical, static_cast<size_t>(job.dataLength)};
    }
    return c;
}

bool AdHocQueue::completeAllForLinkDown(Error err, AdHocCompletion& out) noexcept {
    AdHocJob* job = frontUnfinished();
    if (job == nullptr) {
        return false;
    }
    job->completed = true;
    job->inFlightChunkPoints = 0;
    out.id = job->id;
    out.error = err;
    out.payload = ByteView{};
    return true;
}

void AdHocQueue::confirmDrained() noexcept {
    const AdHocJob& job = frontJob();
    m_arenaHead = job.reserveStart + job.reserveLength;
    m_jobHead = (m_jobHead + 1) % m_jobs.size();
    --m_jobCount;
}

} // namespace mc::detail
