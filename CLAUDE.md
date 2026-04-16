# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MPX68K is a Sharp X68000 computer emulator for iOS and macOS platforms, based on the px68k emulator core. The project bridges low-level C emulation code with modern Swift UI frameworks using SpriteKit. This is a multi-platform document-based application with deep integration into Apple's ecosystem.

## Build Commands

### Building the Project
```bash
# Open in Xcode (primary build method)
open X68000.xcodeproj

# List available schemes and targets
xcodebuild -list -project X68000.xcodeproj

# Build macOS version (current primary target)
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release

# Build iOS version (if needed)
xcodebuild -project X68000.xcodeproj -scheme "X68000 iOS" -configuration Debug

# Clean build artifacts
xcodebuild clean -project X68000.xcodeproj -scheme "X68000 macOS"
```

### Dependencies
The project has a critical dependency on the c68k CPU emulator that must be built first:
```bash
# Build C68K static library (required dependency)
xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k" -configuration Debug
xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k mac" -configuration Debug

# The main project links against libc68k.a from the c68k build output
```

## Architecture

### Multi-Platform Design Pattern
The project uses a shared core with platform-specific presentation layers:
- **X68000 Shared/**: Cross-platform business logic and emulation core
- **X68000 iOS/**: iOS-specific UI, touch controls, and app lifecycle
- **X68000 macOS/**: macOS-specific UI, menu integration, and window management  
- **c68k/**: Independent M68000 CPU emulator built as static library

### Core Components
- **GameScene.swift**: Main SpriteKit-based emulation viewport and input handling
- **FileSystem.swift**: Document-based file management with iCloud integration
- **AudioStream.swift**: AVFoundation bridge for C++ audio output
- **X68JoyCard.swift**: Virtual joycard with cross-platform input support
- **px68k/**: Complete X68000 hardware emulation written in C/C++
  - `x68k/`: Hardware components (FDC, SCSI, ADPCM, graphics, CRTC)
  - `m68000/`: M68000 CPU wrapper using C68K static library
  - `fmgen/`: FM sound synthesis engine (C++)
  - `x11/`: Platform abstraction and main emulation loop

### Language Stack and Interoperability
- **Swift**: UI frameworks, file system, audio bridge, input management
- **C**: Core emulation engine (CPU, hardware peripherals, timing)
- **C++**: FM sound synthesis (fmgen), some emulation components
- **Bridging**: Platform-specific bridging headers expose C APIs to Swift
- **Static Linking**: C68K CPU emulator built as separate static library

### Key Frameworks
- **SpriteKit**: Hardware-accelerated rendering and scene management
- **GameplayKit**: GameController integration for external controllers
- **AVFoundation**: Audio processing and output via AudioStream
- **UniformTypeIdentifiers**: Custom UTI declarations for X68000 file formats
- **CloudKit**: iCloud document synchronization (`iCloud.GOROman.X68000.1`)

### Document Architecture
- **File Format Support**: .dim, .xdf, .d88 (floppy), .hdf, .hdm (hard disk)
- **Multi-Location Search**: Documents, Inbox, legacy paths for ROM discovery
- **Security-Scoped Resources**: Proper sandboxed file access on macOS
- **ROM Management**: Flexible loading from embedded or external ROM files

## Platform-Specific Implementation

### iOS Platform
- **Touch Controls**: Virtual joycard with touch input handling
- **Document Browser**: iOS document picker integration for file selection
- **App Store Distribution**: Configured for iOS App Store with proper entitlements
- **iCloud Integration**: Automatic document synchronization

### macOS Platform  
- **Native Menu Integration**: File menu with FDD/HDD loading/ejecting shortcuts
- **Window Management**: Standard macOS document-based app architecture
- **Keyboard/Mouse Input**: Full support for traditional input methods
- **Drag & Drop**: Multi-file support with automatic drive assignment
- **Screen Rotation**: 90-degree rotation support for vertical games (tate mode)

## Development Guidelines

### Swift-C Interoperability
- Bridging headers expose C emulation APIs to Swift code
- When modifying C code, ensure function signatures remain Swift-compatible
- Use `@objc` annotations for Swift functions called from C
- Memory management across Swift-C boundary requires careful attention

### Build Dependencies
- **Critical**: C68K static library must be built before main project
- The main project fails to build without libc68k.a dependency
- Clean builds require rebuilding both c68k and main project

### File System Architecture
- **ROM Loading**: Multi-path search strategy for CGROM.DAT/IPLROM.DAT
- **Document Model**: All disk images managed through document-based architecture
- **Security**: Sandboxed file access with security-scoped resources on macOS
- **iCloud**: Ubiquitous container integration with automatic downloading

### Audio System
- **AudioStream Class**: Bridges C++ fmgen sound synthesis to AVFoundation
- **Real-time Audio**: Low-latency audio processing for accurate emulation
- **Cross-Platform**: Same audio architecture works on both iOS and macOS

### Input Handling
- **Unified Input**: Single input system supports keyboard, mouse, touch, and gamepad
- **Platform-Specific**: Touch controls on iOS, traditional input on macOS
- **Joycard Emulation**: Virtual joycard provides consistent interface across platforms