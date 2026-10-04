// tests/common/alloc_counter.h — a counting override of every standard global operator
// new/delete form, shared by every zero-allocation test (ALC-*, starting with ALC-01, T-011).
//
// IMPORTANT: include this header from exactly ONE .cpp per test binary. It DEFINES the global
// operator new/delete overloads below, not just declares them; the standard allows exactly one
// definition of each of these "replaceable" functions per program, so a second translation unit
// including this header in the same binary fails to link with a duplicate-symbol error — that is
// the enforcement mechanism for "exactly one", not a bug to work around.
//
// Includes the C++17 aligned-new forms (operator new/new[] taking std::align_val_t, and the
// matching deletes), backed by _aligned_malloc/_aligned_free rather than std::aligned_alloc: the
// MSVC CRT does not provide std::aligned_alloc, while _aligned_malloc/_aligned_free are available
// on both CRTs this project builds against (MSVC's own CRT and MinGW-w64's, which targets the
// same Windows CRT). _aligned_malloc's allocation MUST be freed with _aligned_free specifically,
// never plain free() — mixing the two is undefined behaviour, so every aligned overload below
// pairs with _aligned_free and every plain overload keeps pairing with free().
//
// Not thread-safe: the counter is a plain size_t, matching this project's test binaries, which
// run doctest's test cases sequentially on one thread. Add synchronization here first if that
// ever changes.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <malloc.h> // _aligned_malloc / _aligned_free (MSVC and MinGW-w64)
#include <new>

namespace mc::test {

// Defined here, not just declared: safe only because this header is included by exactly one TU
// per binary (see the file banner above).
inline size_t g_allocCount = 0;

/// Resets the counter to 0. Call this right before the code under test, after any one-time
/// warm-up allocation (e.g. a lazily-initialized static) the code under test might do only on
/// its first call ever.
inline void resetAllocCount() noexcept { g_allocCount = 0; }

/// @return The number of operator new calls (any form below) since the last resetAllocCount().
inline size_t allocCount() noexcept { return g_allocCount; }

// Written through a volatile pointer so the optimiser cannot elide the new/delete pair in
// probeAllocCount() (C++14 [expr.new] allows eliding an allocation nobody observes; GCC -O2 and
// up does, which would make the positive control read 0 in Release).
inline int* volatile g_probe = nullptr;

/// Performs one deliberate, unelidable `new` and returns the counter right after it; the
/// positive control of every ALC file ("the counter is live") checks it is at least 1.
inline size_t probeAllocCount() {
    resetAllocCount();
    g_probe = new int(42);
    const size_t count = allocCount();
    delete g_probe;
    g_probe = nullptr;
    return count;
}

} // namespace mc::test

void* operator new(size_t size) {
    ++mc::test::g_allocCount;
    void* p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    ++mc::test::g_allocCount;
    return std::malloc(size == 0 ? 1 : size);
}

void* operator new[](size_t size) {
    ++mc::test::g_allocCount;
    void* p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    ++mc::test::g_allocCount;
    return std::malloc(size == 0 ? 1 : size);
}

// GCC (-O2 and up) inlines these deletes into the caller next to the inlined replaced operator new
// above, sees malloc-memory reach free() through "operator new"/"operator delete" and reports
// -Wmismatched-new-delete. That is a false positive: this header IS the allocator, and its plain
// new forms use malloc, so its plain delete forms must use free (the aligned forms below keep
// _aligned_malloc/_aligned_free). Suppressed only around these six definitions.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }

void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// C++17 aligned forms: for a type whose alignment exceeds __STDCPP_DEFAULT_NEW_ALIGNMENT__ (an
// alignas(64) struct, for instance), `new`/`new[]` calls these instead of the plain overloads
// above. Every one of these still counts, so an over-aligned type cannot slip past ALC-01 as a
// silent false pass.

void* operator new(size_t size, std::align_val_t align) {
    ++mc::test::g_allocCount;
    void* p = _aligned_malloc(size == 0 ? 1 : size, static_cast<size_t>(align));
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void* operator new(size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    ++mc::test::g_allocCount;
    return _aligned_malloc(size == 0 ? 1 : size, static_cast<size_t>(align));
}

void* operator new[](size_t size, std::align_val_t align) {
    ++mc::test::g_allocCount;
    void* p = _aligned_malloc(size == 0 ? 1 : size, static_cast<size_t>(align));
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void* operator new[](size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    ++mc::test::g_allocCount;
    return _aligned_malloc(size == 0 ? 1 : size, static_cast<size_t>(align));
}

void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    _aligned_free(p);
}

void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    _aligned_free(p);
}
