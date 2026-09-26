# Doc comment style — Doxygen `/** */` + tags

The written rule for how class/struct/enum/function doc comments are written in
C++ headers. §1–§12 are portable by design: nothing in them references this
project's files, modules, or paths — they can be copied as-is into another
project's `docs/rules/` (or equivalent) and applied unchanged. §13 is this
project's profile: which symbols must be documented and what `mc` adds. Assumes
a Doxygen-generated reference (any `Doxyfile` works; nothing here depends on
non-default settings — see §9).

Ported on 2026-09-27 from the owner's reference rule
(`reference_source/docs/rules/doc_comment_style.md`, owner decision: `/** */`
form). Changes from the source: this paragraph, the provenance wording in §10,
and §13. Where §13 is stricter or more specific than §1–§12, §13 wins.

Authority: this doc owns doc-comment MECHANICS (which comment form, which tags,
when). It does not own what the prose should say — writing good, honest,
non-obvious explanations is a separate skill this doc assumes rather than teaches.

## §1 Scope

Applies to every C++ header (`.h`/`.hpp`) meant to be parsed by Doxygen: file
headers, namespaces, classes/structs/enums, functions/methods, and member
variables/constants. `.cpp` files are explicitly OUT of scope for doc comments
(see §6) — they get plain implementation comments only.

## §2 The three comment forms, and when each applies

Doxygen only treats specific comment markers as documentation. Plain `//` and
plain `/* */` are invisible to it — content written that way will never appear
in the generated reference, no matter how well written. This is the single most
common way a doc comment silently fails: it compiles, it reads fine in the
source, and it produces an empty page.

1. **`/** ... *​/` (with tags)** — the default for anything with real content:
   every class/struct/enum/namespace, and every function/method that takes a
   parameter, returns something non-trivial, or has a behavior worth explaining.
   Opens the door to `@param`, `@return`, `@pre`/`@post`, `@see`, `@note` (§3).
2. **`///` (one or a few lines, no tags)** — for a brief that genuinely needs no
   structure: a zero-argument function whose name + one sentence says everything,
   or a short multi-line note above a member variable. If you catch yourself
   wanting to add `@param` or `@return`, that is the signal to switch to form 1.
3. **`///<` (trailing, same line, always exactly one line)** — member variables,
   enum values, and constants. Goes after the declaration on the same line.

Never use plain `//`/`/* */` for anything meant to reach the generated docs.
Reserve plain `//` for local, implementation-only remarks inside a function body
(mainly in `.cpp` files — see §6) — notes that are true only because of how the
code is currently written, not part of the type/function's public contract.

Do not write two competing descriptions of the same thing (e.g. a decorative
`//` banner above a class AND a separate `///` brief below it). Say it once, in
the form Doxygen actually reads. A second, undocumented copy drifts out of sync
silently and just wastes a reader's time figuring out which one is current.

## §3 Tag reference

| Tag | Use for |
|---|---|
| `@file` | One per header, top of file: what this file is / contains (§4). |
| `@brief` | First line of every `/** */` block. Always explicit — do not rely on an auto-brief setting inferring it from the first sentence; explicit `@brief` reads the same regardless of Doxygen config. |
| `@class` / `@struct` / `@enum` / `@namespace` | Optional but recommended immediately above the matching declaration when the doc block is long enough to want a heading in the generated page. |
| `@param[in]` / `@param[out]` / `@param[in,out]` | One per parameter, always with a direction. A function with 3 parameters gets 3 `@param` lines, never a single sentence covering all three. |
| `@return` | General description of the return value. |
| `@retval <value>` | Use instead of / alongside `@return` when specific return values each mean something distinct (e.g. an enum, a sentinel, true/false with different reasons) — one `@retval` line per meaningful value. |
| `@pre` | A precondition the caller must satisfy before calling. |
| `@post` | A postcondition guaranteed after the call returns — including "what fires asynchronously as a result" for callback/event/signal-driven APIs (the call dispatches work, and the real result surfaces later through a callback/event/signal named here). |
| `@throws` | An exception type the function may throw, and when. |
| `@see` | Cross-reference to a related type/function. Use liberally — this is what makes a generated reference feel connected instead of a flat alphabetical list. |
| `@note` | A non-obvious fact worth calling out separately from the flowing prose — renders as a distinct highlighted block. Good home for threading/ownership/lifetime rules. |
| `@warning` | Same as `@note` but for something that causes real damage if ignored (data loss, security, a crash) — renders more prominently. |
| `@code` / `@endcode` | A short usage example, only when the calling pattern is not obvious from the signature + brief alone. |
| `@deprecated` | Marks a symbol as on its way out, with what to use instead. |

## §4 File-level doc

Every header gets a `@file` block at the very top (before includes, or after —
be consistent within the project):

```cpp
/**
 * @file connection_manager.h
 * @brief Pooled network connections with async connect/query calls.
 */
```

One line is enough. Its job is to make the "Files" index in the generated
reference useful instead of a bare list of filenames.

## §5 Type-level doc (class / struct / enum / namespace)

`@brief` one line, then a blank comment line, then as much detail as the type
actually needs: what it owns, its lifecycle, its threading contract, its state
model if it has one. End with `@see` for closely related types.

```cpp
/**
 * @class ConnectionManager
 * @brief Owns a pool of reusable connections and dispatches queries across them.
 *
 * Thread-safe for connect()/query() calls; teardown (close()) must run on the
 * thread that constructed the manager.
 *
 * @see Connection, QueryResult
 */
class ConnectionManager {
```

Enums get the same treatment at the enum level; each enumerator still gets its
own `///<` (§7) — the enum-level `@brief` explains what the enum as a whole
represents, the per-value comments explain each case.

## §6 Function / method doc

Lives in the header, immediately above the declaration. `.cpp` definitions do
NOT repeat it — Doxygen's brief/detail from the header is what's shown; a
`.cpp` gets plain `//` comments only, and only for something non-obvious about
*how* it's implemented (an algorithm choice, a workaround, a "why not the
obvious way" — never a restatement of what the header already documents).

```cpp
/**
 * @brief Runs @p sql against the pool and returns the result asynchronously.
 *
 * Borrows an idle connection (opens a new one if the pool has room), then
 * queues @p sql on it and invokes @p onDone once it completes.
 *
 * @param[in]  sql    Query text.
 * @param[in]  onDone Callback invoked with the result. Called at most once.
 * @return false when the pool is full and no connection is available;
 *         true once the query has been dispatched.
 * @pre Must not be called after close().
 * @post On a true return, @p onDone(result) fires exactly once, from a
 *       worker thread.
 * @see QueryResult
 */
bool query(const std::string &sql, std::function<void(QueryResult)> onDone);
```

A trivial one-line accessor does not need the full block — form 2 (`///`) is
enough:

```cpp
/// @return Number of currently open connections.
int openCount() const;
```

The dividing line: once a doc comment needs `@param` or more than one `@return`
case, use form 1. A single self-evident line never needs a tag at all.

## §7 Member variable / constant / enum value doc

Always `///<`, always trailing, always one line:

```cpp
int m_maxConnections;           ///< Upper bound on pooled connections.
std::vector<Connection> m_pool; ///< Currently open connections (idle or busy).
```

If a member genuinely needs more than one line of explanation, put a `///`
block directly above it instead of forcing it onto one line or wrapping it in
`/** */` — member docs never get the tag treatment, they are always prose:

```cpp
/// Guards the pool during resize(): held for the shortest span that keeps
/// openCount() and m_pool consistent for a concurrent query().
std::mutex m_poolMutex;
```

## §8 Grouping related members

Use Doxygen's own member-group syntax instead of a plain-`//` visual divider —
the plain-`//` version is invisible to the generator (§2); this version renders
as a labeled section in the generated page:

```cpp
/// @name Pool lifecycle
/// @{
/// @brief Closes every pooled connection. Idempotent.
void close();
/// @return Number of currently open connections.
int openCount() const;
/// @}
```

## §9 Formatting conventions

- `/**` on its own line, each continuation line starts with a single space + `*`
  aligned under the second `*` of `/**`, closing `*/` on its own line (see the
  examples above) — this is the layout Doxygen's own examples use and most
  editors auto-continue it.
- One blank `*` line between the `@brief` line and the detail paragraph(s) that
  follow it, and between the detail paragraph(s) and the tag block (`@param`
  onward) — keeps the brief from bleeding into the detail in the rendered page.
- Wrap comment prose at the same column width the project already uses for code.
- This style does not require any particular Doxygen config. `@brief` is always
  written explicitly (§3), so it renders correctly whether or not the project's
  `Doxyfile` has an auto-brief-from-first-sentence setting turned on — the rule
  does not depend on it either way.

## §10 No icons in comments

Comments carry no emoji or pictographic icons — no `⚠️`, `✅`, `❌`, `🔥`, `📌`.
Not in doc comments, not in implementation comments, not in comment banners.

Emphasis is expressed with the tags that already exist for it: `@warning` for
something that causes real damage if ignored, `@note` for a non-obvious fact
worth separating from the prose (§3). Both render as distinct highlighted blocks
in the generated reference, which is exactly what an icon is reaching for — and
unlike an icon they survive being read as plain text, get indexed, and mean the
same thing to every reader.

```cpp
// Wrong — decoration that Doxygen renders as a literal character:
/// ⚠️ Must not be called after close().

// Right:
/// @warning Must not be called after close().
```

An icon in a plain `//` implementation comment (§6) has no tag to convert to;
there, delete it. If the sentence needed a picture to be taken seriously, the
sentence needs rewriting — say what breaks and when, and it will carry itself.

Three practical reasons beyond taste, in a codebase that ships to a machine:

1. **They do not survive the toolchain.** Comment text reaches build logs, `grep`
   output, terminals and editors with varying encodings. The project this rule
   comes from lost a file's entire comment set to a UTF-8/Shift-JIS round trip;
   multi-byte decoration is the first thing to turn into `笞・`.
2. **They are unsearchable.** `grep -n "@warning"` finds every warning in the tree.
   Nothing finds "the paragraphs I marked important" once the marker is a glyph
   that renders differently everywhere.
3. **They inflate.** One icon marks the genuinely dangerous thing; twenty mark
   nothing at all, and a reader learns to skip them — which costs exactly the
   attention the first one was buying.

This applies to comments. It does not apply to user-facing strings, where a glyph
may be the right UI element (a `✕` on a close button), nor to Doxygen's own
member-group dividers or box-drawing used as a section rule.

## §11 Anti-patterns (things that silently produce empty docs)

- **Plain `//` where Doxygen expects `///`/`/** */`.** Compiles fine, reads
  fine in the source, generates nothing. If a class or method's generated page
  shows no description at all, this is almost always why — check the comment
  marker before assuming the prose is missing.
- **A decorative banner comment duplicating a real doc comment.** Only one of
  them is real; the other is dead weight that will eventually say something
  different from the one Doxygen actually reads.
- **One sentence covering multiple parameters.** Doxygen can only build a
  Parameters table from one `@param` per parameter; a combined sentence in the
  detail text does not populate it.
- **Undirected `@param`.** Always `@param[in]`, `@param[out]`, or
  `@param[in,out]` — omitting the direction loses information a reader (or a
  future maintainer skimming signatures) would otherwise get for free.
- **An icon standing in for `@warning`/`@note`.** Renders as a literal glyph,
  cannot be grepped, and does not survive an encoding round trip (§10).
- **A long explanation under `///` instead of `/** */`.** Form 2 is for a brief
  that needs no structure (§2). Once the prose runs to a paragraph, or wants to
  say something about a parameter, a return value or a precondition, it has
  outgrown form 2 — and the tags it should be using are exactly what makes it
  navigable in the generated page instead of a wall of text.

## §12 Worked example (complete, generic)

```cpp
/**
 * @file connection_manager.h
 * @brief Pooled network connections with async connect/query calls.
 */

namespace app::net {

/**
 * @class ConnectionManager
 * @brief Owns a pool of reusable connections and dispatches queries across them.
 *
 * Thread-safe for connect()/query() calls; teardown (close()) must run on the
 * thread that constructed the manager.
 *
 * @see Connection, QueryResult
 */
class ConnectionManager {
public:
    /**
     * @brief Constructs the manager; connections open lazily on first use.
     * @param[in] maxConnections Upper bound on pooled connections. Must be > 0.
     */
    explicit ConnectionManager(int maxConnections);

    /**
     * @brief Runs @p sql against the pool and returns the result asynchronously.
     *
     * Borrows an idle connection (opens a new one if the pool has room), then
     * queues @p sql on it and invokes @p onDone once it completes.
     *
     * @param[in] sql    Query text.
     * @param[in] onDone Callback invoked with the result. Called at most once.
     * @return false when the pool is full and no connection is available;
     *         true once the query has been dispatched.
     * @pre Must not be called after close().
     * @post On a true return, @p onDone(result) fires exactly once, from a
     *       worker thread.
     * @see QueryResult
     */
    bool query(const std::string &sql, std::function<void(QueryResult)> onDone);

    /// @name Pool lifecycle
    /// @{
    /// @brief Closes every pooled connection. Idempotent.
    void close();
    /// @return Number of currently open connections.
    int openCount() const;
    /// @}

private:
    int m_maxConnections;            ///< Upper bound on pooled connections.
    std::vector<Connection> m_pool;  ///< Currently open connections (idle or busy).
};

} // namespace app::net
```

## §13 Project profile — `mc`

This section is specific to this repository. It decides what §1–§12 leave to
the project and adds what the library's contracts need.

### §13.1 What must carry a doc comment

**Mandatory** — review checks it on every diff:

- Every header under `include/mc/` has a `@file` block (§4).
- Everything a consumer can name from `include/mc/`: every class, struct,
  enum, type alias and free function; every public and protected member
  function, including constructors (defaulted or deleted special members
  excepted); every public field, enumerator and constant; every Qt signal.
- The `mc` namespace gets one `@namespace` block, in `include/mc/version.h`.

**Only when not evident** — private members of public classes, headers under
`src/`, anything in `mc::detail`: a doc comment only when a reader cannot get
the fact from the name and the type — an invariant, ownership, lifetime, a
threading rule, a unit, or why the obvious approach is not used. When one is
written, it uses the forms of §2. No comment is acceptable here; a comment
that restates the name is not.

Tests, examples and `tools/` need no doc comments; plain `//` where a reader
needs help.

### §13.2 Complexity and allocation (`core-*` modules)

Every public function of `core-model`, `core-protocol` and `core-session` (the
`mc_core` target) states its time complexity and its heap-allocation behaviour
in a `@par Complexity` paragraph, placed after the return/precondition tags and
before `@see`. Where the module spec's complexity table lists the function, the
comment states the same figures.

```cpp
 * @par Complexity
 * O(len); no allocation.
```

Allocation wording is one of: `no allocation`; `allocates <what> when
<condition>`; `amortised O(1); may allocate when <container> grows`.

### §13.3 Errors are values, never exceptions

The library throws nothing (intent decision 1), so `@throws` never appears.
`noexcept` lives in the signature and is not repeated in prose. A function that
returns `Expected<T>` or `Error` documents the success value with `@return` and
each `ErrorCode` it can produce with one `@retval` line, saying when:

```cpp
 * @return The parsed device.
 * @retval ErrorCode::InvalidDevice Unknown symbol, no number, or a digit
 *         outside the symbol's radix.
```

### §13.4 Results that arrive later

A function whose result surfaces later — a `Session` input whose effect is a
later `Output`, a `McDevice` call answered by a signal — names that output or
signal in `@post` (§3). Qt signals are documented like functions; their
parameters are `@param[out]`, because the values flow from the emitter to the
receiver.

### §13.5 Layout and language

- The `@file` block is the first thing in a header, before `#pragma once` and
  the includes (§4 leaves the choice to the project).
- Prose wraps at 100 columns, the `ColumnLimit` of `.clang-format`
  (`SPEC-build-packaging.md`, Code Style).
- Comments are in English (intent decision 3), and §10 applies without
  exception.

### §13.6 Header sketches in the specs

The header sketches in `docs/spec/` use short `///` comments to state
contracts compactly. They are not style examples. When a sketch becomes a real
header, its comments are rewritten in this document's form and every fact in
them (complexity, allocation, errors, ownership) carries over.

### §13.7 Worked example (`mc`)

```cpp
/**
 * @file device.h
 * @brief PLC device addresses: the device table, parsing and formatting.
 */
#pragma once

#include "mc/core/result.h"
#include <cstdint>
#include <string_view>

namespace mc {

/**
 * @struct Device
 * @brief One PLC device address: a symbol such as D or X plus its number.
 *
 * Value type, trivially copyable, no invariants beyond what parseDevice()
 * enforces.
 *
 * @see parseDevice, formatDevice
 */
struct Device {
    DeviceType type{DeviceType::D}; ///< Symbol, indexes the device table.
    uint32_t number{0};             ///< Device number in the symbol's own radix.
};

/**
 * @brief Parses "D100", "x1F", "TN10" into a Device.
 *
 * Symbol match is case-insensitive and longest-first; the number is parsed in
 * the symbol's radix. Field width is not checked for any frame family; that is
 * validate().
 *
 * @param[in] text Device text without surrounding spaces.
 * @return The parsed device.
 * @retval ErrorCode::InvalidDevice Unknown symbol, no number, or a digit
 *         outside the symbol's radix.
 * @par Complexity
 * O(len); no allocation.
 * @see formatDevice
 */
Expected<Device> parseDevice(std::string_view text) noexcept;

} // namespace mc
```
