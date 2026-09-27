// src/core/session/drain_violation.h -- Session's own drain-contract violation hook (spec
// SPEC-core-session.md, "Drain contract": "An input call made with outputs still pending is a
// precondition violation: debug builds assert; release builds discard the pending outputs and
// log at Error."). Private implementation detail: not part of mc_core's public surface (no public
// API, nothing under include/) -- only session.cpp and this module's own tests reach it.
//
// Session::checkDrained() (session.cpp) calls notifyDrainViolation() whenever an input call finds
// the output ring still non-empty. In a debug build (!NDEBUG) that invokes the *current* handler
// (defaultDrainViolationHandler() unless setDrainViolationHandler() installed another one); in a
// release build (NDEBUG defined) it is a no-op -- the handler is never called at all. Either way,
// checkDrained() itself unconditionally discards the pending outputs and logs at Error right
// after notifyDrainViolation() returns (the spec's own "release" behaviour, in both builds): a
// test that installs a handler which records instead of asserting therefore observes both the
// handler call *and* the discard+log in the very same (debug) build it runs in, which is exactly
// what makes the spec's own debug/release split testable without a live assert() aborting the
// test process itself (owner decision, T-026 rework: keep the spec's literal debug/release split,
// reachable through this replaceable handler).
#pragma once

namespace mc::detail {

/// A drain-contract violation handler: takes no arguments, returns nothing. The default
/// (installed from the start, and restored by `setDrainViolationHandler(nullptr)`) asserts.
using DrainViolationHandler = void (*)();

/// Installs `handler` as the current drain-violation handler (debug builds only ever call it --
/// see `notifyDrainViolation()`). `nullptr` restores the default (assert) handler. Not
/// thread-safe, mirroring the rest of `Session` (single-threaded use only). Tests that install a
/// handler must restore the default before returning (e.g. via an RAII guard), so a later test
/// never inherits a stale one.
void setDrainViolationHandler(DrainViolationHandler handler) noexcept;

/// Debug builds (`!NDEBUG`) only: invokes the current handler. A no-op in a release build
/// (`NDEBUG` defined) -- `Session::checkDrained()` is what unconditionally discards the pending
/// outputs and logs at `Error` in both builds, after this returns.
void notifyDrainViolation() noexcept;

} // namespace mc::detail
