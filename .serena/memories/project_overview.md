# MPX68K Project Overview

## Purpose
MPX68K is a Sharp X68000 computer emulator for macOS and iOS platforms, based on the px68k emulator core. The project bridges low-level C emulation code with modern Swift UI frameworks using SpriteKit.

## Key Features
- Complete X68000 hardware emulation (CPU, sound, graphics, I/O)
- M68000 CPU powered by C68K emulator core
- FM sound synthesis via fmgen
- Support for multiple disk formats (.dim, .xdf, .hdf, .d88)
- Cross-platform support (macOS/iOS)
- Direct file I/O for HDD and FDD with immediate write-back
- Screen rotation support for vertical games (tate mode)
- iCloud integration for file synchronization
- Native macOS menu integration with keyboard shortcuts

## Recent Major Updates
- Implemented direct file I/O for both HDD and FDD operations
- Removed auto-save system to eliminate processing interruptions
- Added READ CAPACITY command for correct HDD capacity detection
- Enhanced TestFlight distribution capability

## Target Platforms
- macOS 11.0+ (Intel and Apple Silicon)
- iOS (with touch controls and document browser integration)