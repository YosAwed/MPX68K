# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

X68iOS is a Sharp X68000 computer emulator for iOS and macOS platforms, based on the px68k emulator core. The project bridges low-level C emulation code with modern Swift UI frameworks using SpriteKit.

## Build Commands

### Building the Project
```bash
# Open in Xcode (primary build method)
open X68000.xcodeproj

# Build from command line (if needed)
xcodebuild -project X68000.xcodeproj -scheme "X68000 iOS"
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS"
```

### Dependencies
The project has a dependency on the c68k CPU emulator:
```bash
# Build C68K core first if needed
xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k"
```

## Architecture

### Core Components
- **X68000 Shared/**: Shared Swift code and C emulation core
  - `GameScene.swift` - Main emulation viewport using SpriteKit
  - `px68k/` - Complete X68000 hardware emulation in C/C++
  - `px68k/m68000/` - M68000 CPU emulation using C68K
  - `px68k/fmgen/` - FM sound synthesis
- **X68000 iOS/**: iOS-specific UI and document handling
- **X68000 macOS/**: macOS-specific UI
- **c68k/**: Separate M68000 CPU emulator project

### Language Stack
- **Swift**: UI layer, device management, file system
- **C**: Core emulation engine (CPU, hardware)
- **C++**: Sound generation, some emulation components

### Key Frameworks
- **SpriteKit**: Main rendering engine
- **GameplayKit**: Controller support
- **AVFoundation**: Audio processing
- **Document-based app**: Supports .dim, .xdf, .d88, .hdm, .hdf files

## File System Integration

The app uses iOS/macOS document model with:
- iCloud container integration (`iCloud.GOROman.X68000.1`)
- Custom UTI declarations for X68000 disk images
- Document browser support

## Development Notes

### Swift-C Interoperability
The project extensively uses bridging headers to interface Swift with the C emulation core. When modifying the C code, ensure compatibility with Swift calling conventions.

### ROM Integration
System ROMs (CGROM.DAT, IPLROM.DAT) are embedded in the app bundle. Recent commits show work on loading these from external files rather than embedding them.

### Platform Differences
The shared codebase supports both iOS and macOS with platform-specific UI code in separate target directories. Input handling differs significantly between touch controls and traditional mouse/keyboard.

### Audio Architecture
Audio uses AVFoundation with custom AudioStream class bridging the C++ sound generation to Swift audio APIs.