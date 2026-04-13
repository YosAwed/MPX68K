# Repository Guidelines

## Project Structure & Module Organization
- `X68000 Shared/`: Cross‑platform Swift code (GameScene, FileSystem, X68Logger, X68Security, px68k core).
- `X68000 macOS/`: macOS UI, app lifecycle, menus, entitlements.
- `c68k/`: Independent M68000 CPU emulator Xcode project (static library dependency).
- `zsrc/`: Standalone C++ X68000Z emulator sources.
- `build/`: Generated archives/packages; do not edit by hand. See `README.md` and `ARCHITECTURE.md` for deeper context.

## Build, Test, and Development Commands
- Open in Xcode: `open X68000.xcodeproj` (primary workflow; run the “X68000 macOS” scheme with ⌘R).
- Build dependency: `xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k" -configuration Debug`.
- Debug build: `xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug build`.
- Release/archiving: adjust `-configuration Release` or add `archive` as needed.

## Coding Style & Naming Conventions
- Swift/C/C++ all use 4‑space indentation, no tabs; match surrounding style and avoid mass reformatting.
- Types use `UpperCamelCase`; functions, properties, and local variables use `lowerCamelCase`.
- Prefer descriptive names over abbreviations; follow existing prefixes such as `X68…`.
- Use `X68Logger` (`debugLog("…", category: .input)`) instead of raw `print`/`NSLog`.

## Testing Guidelines
- There is currently no dedicated automated test target; always build Debug and Release and run the app to exercise core flows (boot, FDD/HDD load, input, rotation).
- When adding tests, use XCTest targets, name suites `*Tests`, and group them logically by feature (e.g., FileSystem, security, input).
- Keep tests deterministic and independent of ROM content paths; document any special setup in the PR description.

## Commit & Pull Request Guidelines
- Use clear, imperative commit messages (e.g., `Fix audio underrun on macOS`, `Refine FDD error handling`).
- Keep PRs focused on a small, coherent set of changes; update `README.md`/`ARCHITECTURE.md` when behavior or architecture changes.
- For UI or UX changes, include short notes and screenshots/video when possible.
- Never commit ROMs or other copyrighted assets; reference expected paths only.

## Security & Agent-Specific Notes
- Always validate user input and file formats; prefer using `X68Security` and existing validation helpers.
- Respect sandboxed file access patterns (FileSystem-based loading) and avoid introducing new global paths.
- AI/code agents should read this file plus `CLAUDE.md` before large refactors and avoid repository-wide rewrites or unsolicited style changes.

