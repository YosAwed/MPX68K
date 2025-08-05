# Code Structure and Architecture

## Project Directory Layout
```
MPX68K/
├── X68000.xcarchive/           # Release archives
├── TestFlight/                 # Export directory for TestFlight
├── X68000.xcodeproj/           # Main Xcode project
├── X68000 Shared/              # Cross-platform Swift code
│   ├── px68k/                  # C/C++ emulation core
│   │   ├── x68k/              # Hardware components (FDC, SCSI, ADPCM, graphics)
│   │   ├── m68000/            # M68000 CPU wrapper
│   │   ├── fmgen/             # FM sound synthesis engine (C++)
│   │   └── x11/               # Platform abstraction and main emulation loop
│   ├── GameScene.swift        # Main SpriteKit-based emulation viewport
│   ├── FileSystem.swift       # Document-based file management with iCloud
│   ├── AudioStream.swift      # AVFoundation bridge for C++ audio
│   ├── X68JoyCard.swift       # Virtual joycard with cross-platform input
│   └── *.swift                # Other Swift UI and logic files
├── X68000 iOS/                 # iOS-specific code
├── X68000 macOS/               # macOS-specific code
│   ├── Info.plist             # Version info and app configuration
│   ├── AppDelegate.swift      # macOS app lifecycle and menu handling
│   └── GameViewController.swift # macOS window and view management
├── c68k/                       # M68000 CPU emulator (separate project)
│   └── c68k.xcodeproj         # C68K static library project
├── CLAUDE.md                   # Development guidelines
├── README.md                   # Project documentation
└── ARCHITECTURE.md             # Detailed architecture documentation
```

## Key Components

### Swift Layer (UI and Integration)
- **GameScene.swift**: Main emulation viewport using SpriteKit, input handling
- **FileSystem.swift**: File handling, disk image management, iCloud integration
- **AudioStream.swift**: Bridge between C++ fmgen and AVFoundation
- **X68JoyCard.swift**: Virtual joycard implementation with multi-input support

### C/C++ Emulation Core (px68k/)
- **x68k/**: Hardware component emulation
  - `sasi.c`: SASI hard disk controller with direct file I/O
  - `fdc.c`, `fdd.c`: Floppy disk controller and drive management
  - `disk_*.c`: Disk format handlers (DIM, XDF, D88) with direct writes
- **fmgen/**: FM sound synthesis engine (C++)
- **x11/**: Platform abstraction layer

### Platform-Specific Code
- **X68000 macOS/**: Menu integration, window management, drag & drop
- **X68000 iOS/**: Touch controls, document browser integration

## Data Flow Architecture
1. **Swift UI** → **Bridging Headers** → **C Emulation Core**
2. **File I/O**: Direct writes bypass memory buffers for immediate persistence
3. **Audio**: C++ fmgen → AudioStream → AVFoundation
4. **Input**: Multiple sources → X68JoyCard → C emulation layer