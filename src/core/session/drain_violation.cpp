// src/core/session/drain_violation.cpp -- see drain_violation.h's own doc comment for the full
// design rationale (owner decision, T-026 rework).
#include "drain_violation.h"

#include <cassert>

namespace mc::detail {
namespace {

void defaultDrainViolationHandler() noexcept {
    assert(false && "Session: input call with outputs still pending (drain contract)");
}

// A function-local static (rather than a namespace-scope global) avoids any static-init-order
// question for what is, in effect, one process-wide mutable slot -- there is exactly one Session
// drain-violation handler at a time, matching the single-threaded, one-engine-per-process use this
// whole module assumes throughout.
DrainViolationHandler& currentHandler() noexcept {
    static DrainViolationHandler handler = &defaultDrainViolationHandler;
    return handler;
}

} // namespace

void setDrainViolationHandler(DrainViolationHandler handler) noexcept {
    currentHandler() = (handler != nullptr) ? handler : &defaultDrainViolationHandler;
}

void notifyDrainViolation() noexcept {
#ifndef NDEBUG
    currentHandler()();
#endif
}

} // namespace mc::detail
