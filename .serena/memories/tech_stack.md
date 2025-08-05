# Technology Stack

## Languages
- **Swift**: UI frameworks, file system, audio bridge, input management
- **C**: Core emulation engine (CPU, hardware peripherals, timing)
- **C++**: FM sound synthesis (fmgen), some emulation components

## Key Frameworks
- **SpriteKit**: Hardware-accelerated rendering and scene management
- **GameplayKit**: GameController integration for external controllers
- **AVFoundation**: Audio processing and output via AudioStream
- **UniformTypeIdentifiers**: Custom UTI declarations for X68000 file formats
- **CloudKit**: iCloud document synchronization

## Architecture Components
- **Multi-platform design**: Shared core with platform-specific presentation layers
- **Static linking**: C68K CPU emulator built as separate static library
- **Bridging headers**: Platform-specific bridging headers expose C APIs to Swift
- **Document-based architecture**: File management with security-scoped resources

## Build System
- **Xcode projects**: X68000.xcodeproj (main), c68k/c68k.xcodeproj (dependency)
- **Critical dependency**: c68k static library must be built before main project
- **Target platforms**: macOS and iOS with separate schemes