---
name: cpp-review
description: Thorough C++ code review for c3console. Use before finishing a milestone, after a refactor, or when asked to "review the code". Checks correctness, project safety invariants, const correctness, noexcept, copy/move semantics, duplication, and clarity for other developers; then fixes what it finds and re-verifies on both toolchains. Args: paths, `--diff` (default: uncommitted changes), `--all`, `--report-only`.
---

# C++ review for c3console

You are reviewing code for a hobby project on a valuable car. Correctness and reversibility
beat cleverness. Read AGENTS.md first if it is not already in context; its safety invariants
and conventions are the standard this review enforces.

## 1. Scope

- No args or `--diff`: everything `git status --short` reports as modified or untracked under
  `src/`, `tests/`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tools/`.
- Paths: those files or directories.
- `--all`: every first-party file under `src/` and `tests/`.
- Never review `third_party/` or anything under `build*/`.

Read every in-scope file in full with `cat -n`. Do not review from memory of an earlier
version: clang-format may have changed the text since you last saw it.

## 2. Verify before judging

Run the real checks and read the output. Never claim a pass you did not see.

```bash
cmake --build build -j 2>&1 | grep -E "error|warning:"
ctest --test-dir build --output-on-failure 2>&1 | grep -E "tests passed|failed"
tools/docker/build.sh drm --tidy > /tmp/review-tidy.log 2>&1; echo "exit=$?"
grep -cE '^/src/(src|tests)/.*warning:' /tmp/review-tidy.log          # tidy findings
grep -E '^/src/(src|tests)/.*warning:' /tmp/review-tidy.log | sed 's|^/src/||' | sort -u
```

If the container run rebuilt zero of the in-scope translation units, `touch` them and run
again: clang-tidy only reports on files it compiles. Under the Ninja generator the tidy
hook lives in `CMakeFiles/rules.ninja`, not `build.ninja`.

## 3. Checklist

Work through every section for every file. Note a finding only when you can name the line
and the concrete failure or the concrete reader confusion.

### Correctness
- Buffer lengths: any `snprintf`/`std::copy`/`memcpy` where the length used afterwards can
  exceed the buffer. `snprintf` returns the would-be length, not the written length.
- Integer conversions: unsigned underflow (`a - b` where `b > a`), `int * int` widened after
  overflow, `size_t` vs signed loops, `std::from_chars` end-pointer checks.
- Clock and time: monotonic assumed but not guaranteed by the injected source; catch-up
  loops bounded; `dt` clamped.
- Enum switches exhaustive without `default`, so a new enumerator is a compiler warning.
- Error paths: every `std::optional`/status return checked at the call site; error messages
  name the offending input and the accepted values.
- Locale: `strtod`, `tolower`, `isspace` assume the C locale; say so where used.

### Safety invariants (AGENTS.md)
- No threshold or warning decision on the Pi for display purposes. Simulated MCU logic is
  fenced, named, and commented as scaffolding.
- Stale telemetry never rendered as live. Anything that caches a `Frame` has a stale-out path.
- No heap allocation, exceptions, or locks in `poll()`, the render loop, or anything they
  call. `noexcept` on those paths is not optional.
- Calibration values only from `config/calibration.json`; the placeholder flag is honoured.
- HVAC hold-last-state and lamp-parallel assumptions not contradicted by code or comments.

### const correctness
- Locals that never change are `const`. Range-for over non-trivial types is `const auto&`.
- Member functions that do not mutate are `const`; those that neither mutate nor read state
  are `static` or free functions.
- Parameters read but not copied: `const T&` (or `std::string_view`, `std::span<const T>`).
- Pointers to data that is not mutated are `const T*`.
- `constexpr` for anything computable at compile time: tables, `to_string`, small helpers.
- No `const` on data members (it deletes move assignment) and no `const` on by-value return
  types (it blocks move construction at the call site).

### noexcept
- Hot-path interfaces and their implementations: `noexcept` on the virtuals and the overrides.
- Move constructors, move assignment, swap, destructors: `noexcept` (destructors are by
  default; do not add `noexcept(false)`).
- Accessors that only read trivially copyable state: `noexcept`.
- Never mark `noexcept` something that allocates, throws on bad input, or calls unknown
  callbacks. `std::terminate` is not error handling.

### Copy and move semantics
- Sink parameters (stored into a member or container): take by value and `std::move` in, or
  `T&&`. Do not take `const T&` and copy.
- `std::move` on the last use of a local passed to a sink; never on a `const` object (it is a
  silent copy); never on a returned local (it defeats RVO).
- `return {...}` / return by value for aggregates; `try_emplace`/`emplace_back` over
  construct-then-insert.
- Rule of zero where possible. Types that own a resource or are polymorphic interfaces: all
  five special members declared, copy deleted, move deleted or `noexcept`.
- `std::string_view` and `std::span` are never returned to or stored past the lifetime of
  what they view. A view of a temporary `std::string` is a bug.
- Lambda captures: `[&]` in a lambda that outlives the scope is a bug; by-value captures of
  large objects are a smell.
- `std::optional<T>` returned by value, not `bool` plus an out-parameter.

### Duplication and structure
- The same lookup, switch, or validation written more than once for different enums or
  fields: hoist into a helper (`util::parse_enum`, `toml::get_scalar<T>`).
- Hand-typed lists that mirror an enum (names, counts): derive from one exhaustive array.
- One-data-member classes that only carry context into methods: usually free functions with
  an extra parameter.
- Two anonymous namespaces in one file; helpers declared far from their only use.
- Logic in `main()` beyond parse → resolve → run. Overlay and validation belong in `app/`.

### Clarity for the next developer
- File header comment states what the file is for and the one non-obvious constraint.
- Comments explain why, not what. A comment that restates the code is noise; a comment that
  records a deviation from the spec or the reference mockup is required.
- Names say what they hold: `checked_int` not `to_int`, `kOilLowPsi` not `kThresh1`.
- Stale text: comments, NOLINTs, usage strings, README placeholders that describe a previous
  version of the code. Grep for names that no longer exist.
- Magic numbers carry a unit in the name or the comment (`kPeriodMs`, `speed_mph`).
- `[[nodiscard]]` on anything whose result is the point of calling it.
- NOLINT only with the check name and a reason on the same line.
- Tidy noise silenced at the right layer: tests-only checks in `tests/.clang-tidy`,
  third-party excluded via per-target hooks, never by disabling a check project-wide.

### Tests
- Every pure function touched has a test; behaviour changes have a test that would have
  failed before. Tests use designated initialisers and a fake clock, not sleeps.
- Test names state the behaviour, not the function.

## 4. Fix, then re-verify

Unless `--report-only`: apply every finding you are confident in. Prefer restructuring over
suppressing. Use index- or regex-based edits: comment spacing and line breaks move under
clang-format, so exact multi-line anchors fail. After editing:

```bash
clang-format -i <every file you touched>
cmake --build build -j && ctest --test-dir build --output-on-failure
tools/docker/build.sh drm --tidy   # must end with zero findings in our code
```

Run `clang-format --dry-run --Werror` on the touched files and report if the tree drifted
from `.clang-format`; do not chase a style file that is being edited.

## 5. Report

Lead with the outcome: builds, tests, tidy count, on both toolchains, from real output.
Then findings grouped as: bugs (with the concrete failure), semantics (const/noexcept/
copy-move), duplication, clarity. One or two sentences each, file and line named. State
what you fixed and what you left with a reason. Flag anything that changes a documented
decision (AGENTS.md, spec, `docs/milestones.md`) so it can be recorded there.
