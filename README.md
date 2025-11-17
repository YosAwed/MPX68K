# MPX68K

A Sharp X68000 computer emulator for macOS platforms, based on the px68k emulator core.
This repository is indirectly a fork of Mr. Hissorii's [px68k](https://github.com/hissorii/px68k). Based on his source code, [Goroman](https://github.com/GOROman) converted it for iOS, and I made it works on MacOS.

## Overview

MPX68K provides authentic Sharp X68000 emulation with modern Swift UI frameworks, bridging low-level C emulation code with SpriteKit for an optimal user experience on Apple silicon platforms.

## Features

### Core Emulation
- **X68000 Hardware Emulation**: CPU, sound, graphics, and I/O
- **M68000 CPU**: Powered by C68K emulator core
- **FM Sound Synthesis**: High-quality audio via fmgen
- **Multiple Disk Formats**: Support for .dim, .xdf, .hdf files

### macOS Enhancements
- **Dual FDD Support**: Menu-driven management for Drive 0 and Drive 1
- **Hard Disk Support**: HDD management with dedicated menu
- **Screen Rotation**: 90-degree rotation support for vertical games (tate mode)
- **Enhanced Joycard**: Keyboard, mouse input
- **Native Menu Integration**: Dedicated FDD, HDD, and Display menus with keyboard shortcuts

### Platform
- **macOS Support**: Native menu bar and keyboard/mouse input
- **iCloud Integration**: Seamless file sync across devices

## System Requirements

### macOS
- macOS 15.0 or later
- Apple Silicon Mac
- Minimum 4GB RAM
- 1GB free disk space for ROM and disk images

## ROM Files Setup

MPX68K requires original SHARP X68000 system ROM files to function properly. These files are **not included** with the emulator and must be obtained separately.

### Required ROM Files

You need the following ROM files from an original X68000 system:

| File | Description | Size |
|------|-------------|------|
| `CGROM.DAT` | Character Generator ROM | 768KB |
| `IPLROM.DAT` | Initial Program Loader ROM | 128KB |

### Installation Locations

Place the ROM files in the location:

1. **iCloud Document folder**:
   ```
   /Users/<username>/Documents/X68000 (/Users/<username>/Library/Containers/NANKIN.X68000/Data/Documents/X68000)
   ├── README.txt (auto generated proper folder)
   ├── CGROM.DAT
   └── IPLROM.DAT
   ```

### Important Notes

- **Legal Notice**: ROM files are copyrighted by SHARP CORPORATION. You must own an original X68000 system to legally use these files.
- **File Names**: ROM file names must be exactly `CGROM.DAT` and `IPLROM.DAT`.
- **File Integrity**: Ensure ROM files are not corrupted. The emulator will display an error if files are missing or invalid.
- **Backup**: Always keep backup copies of your ROM files in a safe location.

### Verification

When ROM files are properly installed, the emulator will:
1. Load without ROM-related error messages
2. Display the characteristic X68000 boot screen
3. Show proper Japanese character rendering

If you see garbled text or boot failures, verify that your ROM files are correctly placed and named.

## Installation

### Building from Source
You need Apple Developer Account for using XCode and certification.

1. **Clone the repository:**
   ```bash
   git clone https://github.com/YosAwed/MPX68K.git
   cd MPX68K
   ```

2. **Open in Xcode:**
   ```bash
   open X68000.xcodeproj
   ```

3. **Build the project:**
   - Select your target platform (macOS)
   - Build and run (⌘+R)

### Dependencies
The project includes a dependency on the c68k CPU emulator which is built automatically.

## Usage

### macOS Controls

#### FDD Management
- **FDD → Open Drive 0...** (⌘1): Insert disk into Drive 0
- **FDD → Open Drive 1...** (⌘2): Insert disk into Drive 1
- **FDD → Eject Drive 0** (⇧⌘1): Eject disk from Drive 0
- **FDD → Eject Drive 1** (⇧⌘2): Eject disk from Drive 1

#### HDD Management
- **HDD → Open Hard Disk...** (⌘H): Insert hard disk image
- **HDD → Eject Hard Disk** (⇧⌘H): Eject hard disk image

#### Display Management
- **Display → Rotate Screen** (⌘R): Rotate screen between landscape and portrait modes
- **Display → Landscape Mode**: Set to standard horizontal orientation
- **Display → Portrait Mode (90°)**: Set to vertical orientation for tate games

#### Joycard Input
- **Arrow Keys** or **WASD**: 8-direction movement
- **Space** or **J**: Button A
- **Z** or **K**: Button B

## File Formats

| Extension | Description | Type | Platform Support | Security |
|-----------|-------------|------|-------------------|----------|
| .dim | Standard disk image | Floppy | macOS | Validated |
| .xdf | Extended disk format | Floppy | macOS | Validated |
| .hdf | Hard disk format | Hard Disk | macOS | Validated |

### Hard Disk Usage

Hard disk images provide faster access and larger storage capacity compared to floppy disks:
You can make HDD image using this application, but it requires initialization by using FORMAT.X running by Human68k on the emulator.

1. **Loading HDD Images**: Use **HDD → Open Hard Disk...** menu or drag .hdf files
2. **Boot Priority**: When both FDD and HDD are present, X68000 boots from FDD first (if a bootable disk is inserted), then falls back to HDD
3. **Performance**: HDDs offer significantly faster loading times for large applications
4. **Capacity**: Support for larger disk images suitable for complex software suites
5. **Persistence**: Changes to HDD images are automatically saved

#### Boot Order Behavior
The X68000 follows the authentic boot sequence:
1. **FDD First**: Always checks Drive 0 for bootable disk
2. **HDD Fallback**: If no bootable FDD is present, boots from HDD
3. **System Disk Override**: Insert a system floppy to boot from FDD even with HDD installed

#### Recommended Usage
- **System Boot**: Install X68000 system on HDD for faster startup when no FDD is inserted
- **Applications**: Store large software packages on HDD
- **Development**: Use HDD for compilers and development tools
- **Games**: Multi-disk games can be consolidated onto HDD
- **Boot Override**: Use system floppies to boot specific programs or perform maintenance

### Screen Rotation (Tate Mode)

The X68000 emulator supports 90-degree screen rotation for vertical games, commonly known as "tate mode":

#### Supported Games
- **Dragon Spirit**: Classic vertical shooter requiring portrait orientation
- **Other Vertical Games**: Any X68000 game designed for vertical play

#### Usage
1. **Rotate Screen**: Use **Display → Rotate Screen** (⌘R) to toggle between orientations
2. **Direct Selection**: Choose **Display → Portrait Mode (90°)** for vertical games
3. **Window Adjustment**: macOS automatically resizes the window for optimal display
4. **Control Consistency**: Joycard controls remain unchanged regardless of rotation

#### Technical Features
- **Smooth Rotation**: SpriteKit-based rotation with proper aspect ratio maintenance
- **Persistent Settings**: Rotation preference is saved and restored between sessions
- **Optimal Scaling**: Automatic scaling ensures games fill the available screen space
- **No Performance Impact**: Rotation processing doesn't affect emulation performance

## Architecture

MPX68K uses a multi-layered architecture that bridges modern Swift UI frameworks with low-level C/C++ emulation code.

### Language Stack
- **Swift**: UI layer, device management, file system, logging infrastructure
- **C**: Core emulation engine (CPU, hardware, memory management) 
- **C++**: Sound generation (fmgen), emulation components
- **Objective-C**: Bridge layer for legacy compatibility

### Key Components
- **GameScene.swift**: Main emulation viewport using SpriteKit
- **FileSystem.swift**: Secure file handling and disk image management  
- **X68Logger.swift**: Professional logging system with categorization
- **X68Security.swift**: Input validation and security functions
- **JoyController.swift**: GameController integration
- **X68JoyCard.swift**: Virtual joycard implementation
- **px68k/**: Complete X68000 hardware emulation in C/C++
- **c68k/**: Independent M68000 CPU emulator (static library)

For detailed architecture documentation with diagrams, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Recent Updates

### Version 4.1.0 (Build 910) - August 2025

#### ✅ New Features
- **Dual FDD Drive Support**: Independent Drive 0/1 management
- **Hard Disk Drive Support**: Complete HDD management with menu integration  
- **Screen Rotation Support**: 90-degree rotation for vertical games (tate mode)
- **Enhanced macOS Joycard**: Keyboard and mouse input support
- **Menu Integration**: Native macOS menu bar with FDD, HDD, and Display shortcuts
- **Window Management**: Automatic window resizing for optimal display

#### 🔒 Security & Stability Improvements
- **Memory Safety**: Enhanced buffer bounds checking and validation
- **Input Validation**: Comprehensive file format validation with security checks
- **Sandboxed File Access**: Secure file system operations with proper scoping

#### 🛠️ Code Quality & Performance
- **Professional Logging System**: Replaced 200+ print statements with X68Logger
  - Categorized logging (FileSystem, UI, Audio, Input, Emulation, Network)
  - Debug logs automatically excluded from release builds
  - Apple's unified logging system integration
  - Performance monitoring and profiling capabilities
- **Compiler Warnings Eliminated**: Zero warnings in release builds
- **Memory Management**: Optimized Swift-C interoperability
- **Build System**: Enhanced Xcode project configuration

## Development

### Project Structure
```
MPX68K/
├── X68000 Shared/          # Cross-platform Swift code
│   ├── px68k/              # C/C++ emulation core
│   │   ├── x68k/           # X68000 hardware components
│   │   ├── fmgen/          # FM sound synthesis (C++)
│   │   ├── m68000/         # CPU wrapper
│   │   └── x11/            # Platform abstraction
│   ├── *.swift             # Swift UI and business logic
│   ├── X68Logger.swift     # Professional logging system
│   ├── X68Security.swift   # Security and validation
│   └── FileSystem.swift    # Secure file management
├── X68000 macOS/           # macOS-specific code
│   ├── AppDelegate.swift   # Menu integration
│   └── GameViewController.swift # Main view controller
├── c68k/                   # M68000 CPU emulator (static lib)
│   └── c68k.xcodeproj      # Independent build system
├── CLAUDE.md               # Development guidelines
├── ARCHITECTURE.md         # Detailed architecture docs
└── Settings.bundle/        # App settings configuration
```

### Building

#### Prerequisites
- Xcode 15.0 or later
- macOS 15.0+ SDK
- Swift 5.9+

#### Build Commands
```bash
# Clean build
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug clean build

# Release build
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release build

# Archive for distribution
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release archive
```

#### Dependencies Build Order
1. **C68K Library**: Built automatically as dependency
2. **Main Project**: Links against libc68k.a
3. **Package Dependencies**: swift-crypto, swift-asn1 (managed by SPM)

#### Code Quality
- **Zero Compiler Warnings**: Clean builds with no warnings
- **Memory Safety**: Comprehensive bounds checking 
- **Thread Safety**: Race condition prevention
- **Performance Profiling**: Built-in logging for performance monitoring

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test on both platforms
5. Submit a pull request

Please refer to `CLAUDE.md` for detailed development guidelines and architectural notes.

## License

This project is based on px68k emulator. Please refer to the original px68k license terms.

# Third-Party Notices

- **px68k** (hissorii/px68k) — upstream emulator core. See upstream license files.
- **c68k** — MC68000 CPU emulator (via px68k). See upstream.
- **fmgen** by cisc — FM/PSG implementation (via px68k). See upstream.

No SHARP ROMs are distributed. Users must supply legally-owned ROMs.

## Acknowledgments

- **px68k**: Original X68000 emulator core
- **C68K**: M68000 CPU emulator
- **fmgen**: FM sound synthesis library
- **Sharp**: Original X68000 computer system

## Links

- [GitHub Repository](https://github.com/YosAwed/MPX68K)
- [Issues & Bug Reports](https://github.com/YosAwed/MPX68K/issues)

---

*SHARP X68000 is a trademark of SHARP CORPORATION. This project is not affiliated with SHARP CORPORATION.*
