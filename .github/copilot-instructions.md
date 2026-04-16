# Copilot instructions for MPX68K

Purpose: Give future Copilot sessions repository-specific guidance so PRs, code changes, and builds are safe and effective.

---

## Quick build, test, and (lack of) lint commands

- Open in Xcode (primary workflow):
  open X68000.xcodeproj

- Build dependency (C CPU core) — must be built first:
  xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k" -configuration Debug

- Debug build (macOS):
  xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug build

- Release / archive:
  xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release build
  xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release archive

- Running tests: There is no dedicated test target by default. Use this template to run XCTest from the command line when tests are added:
  xcodebuild test -project X68000.xcodeproj -scheme "X68000 macOS" -destination 'platform=macOS' -only-testing:TargetName/ClassName/testMethodName

- Linting: No linter configured in the repo. Do not add global style reformatters without confirming with maintainers.

---

## High-level architecture (overview)

- Multi-layer design: platform-specific UI (X68000 macOS / iOS) sits atop a shared Swift core ("X68000 Shared") which drives the C/C++ emulation core (px68k) and the independent C68K static CPU library.
- Key pieces:
  - X68000 Shared/: Swift business logic, GameScene (SpriteKit), FileSystem, AudioStream, X68Logger, X68Security, input adapters.
  - px68k/: C/C++ emulation (x68k hardware, FDC, SCSI, ADPCM, graphics).
  - c68k/: standalone M68000 CPU static library (libc68k.a) — built before the main project.
  - Audio bridge: fmgen C++ → AudioStream.swift → AVFoundation.
  - File system: Document-based model with security-scoped access and iCloud integration.

(See ARCHITECTURE.md and CLAUDE.md for diagrams and detailed notes.)

---

## Key repository conventions and constraints

- Indentation & style:
  - 4-space indentation for Swift/C/C++ (no tabs).
  - Types: UpperCamelCase; functions/properties/local variables: lowerCamelCase.
  - Prefixes: use `X68`/`X68000` naming patterns where applicable.

- Build dependency order:
  - c68k must be built first; the main Xcode project links against libc68k.a.

- Interoperability:
  - Swift↔C bridge via bridging headers. When changing C APIs, keep Swift-compatible function signatures and update headers.
  - Use X68Logger for logging (avoid adding raw print/NSLog statements).

- Files & assets:
  - ROM files (CGROM.DAT, IPLROM.DAT, etc.) are NOT included; never commit ROMs or other copyrighted assets.
  - ROMs are loaded from sandboxed Documents locations per README; tests must avoid depending on local ROM files.

- Tests and CI:
  - No CI/test runner present. If adding tests, add an XCTest target and document how to run single tests (use the xcodebuild -only-testing template above).

- Code changes & refactors:
  - Avoid large, repo-wide automatic reformatting or mass renames. Read CLAUDE.md and AGENTS.md before major refactors.

---

## Files to read first (authoritative)
- README.md — user/setup notes and ROM requirements
- ARCHITECTURE.md — diagrams and system design
- CLAUDE.md and AGENTS.md — development guidelines, build order, and repository conventions

---

## Notes for Copilot sessions
- Respect the c68k build-order dependency and do not propose changes that assume the CPU core is removable or replaced without careful validation.
- Do not propose committing ROMs or embedding them in tests/examples.
- For platform-specific changes, prefer small, isolated PRs and include build/run instructions for both Debug and Release flows.

## CI notes
- A macOS GitHub Actions workflow is provided at .github/workflows/macos-test.yml. It runs on macos-latest and builds c68k then the main project and executes xcodebuild test.
- The CI workflow assumes a macOS virtual runner and does not require code signing or provisioning for the test run. Do not add signing steps unless explicitly needed for release artifacts.

