# MPX68K Project Overview

## Purpose
MPX68K is a Sharp X68000 computer emulator for macOS platforms, based on the px68k emulator core. It provides authentic X68000 emulation using modern Swift UI frameworks and SpriteKit for optimal performance.

## Tech Stack
- **Swift**: UI layer, file system, input handling, menu integration
- **C**: Core X68000 hardware emulation
- **C++**: FM sound synthesis (fmgen)
- **SpriteKit**: Hardware-accelerated rendering and scene management
- **AVFoundation**: Audio processing and output
- **GameplayKit**: GameController integration
- **Xcode 15.0+**: Primary development environment

## Project Structure
```
MPX68K/
├── X68000 Shared/          # Cross-platform Swift code
│   ├── px68k/              # C/C++ emulation core
│   ├── GameScene.swift     # Main emulation viewport
│   ├── X68Logger.swift     # Professional logging system
│   ├── X68MouseController.swift # Existing mouse functionality
│   └── FileSystem.swift    # Secure file management
├── X68000 macOS/           # macOS-specific code
│   ├── AppDelegate.swift   # Menu integration and system handling
│   └── GameViewController.swift # Main view controller
└── c68k/                   # M68000 CPU emulator (static lib)
```

## Key Components
- **AppDelegate.swift**: Handles menu creation/updates, FDD/HDD menu actions
- **GameViewController.swift**: Main controller with emulation logic
- **X68MouseController.swift**: Existing mouse input handling
- **GameScene.swift**: SpriteKit scene for rendering and input

## Current Mouse Implementation
- X68MouseController class exists with position tracking and click handling
- Uses direct position mapping to X68000 screen coordinates
- Has button state management and click detection
- Called via X68000_Mouse_SetDirect() function

## Menu System Architecture
- Main menu defined in Main.storyboard
- Dynamic menu updating via AppDelegate timer (every 2 seconds)
- Existing menus: FDD, HDD, Display, System
- Menu actions routed through AppDelegate to GameViewController