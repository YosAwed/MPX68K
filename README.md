# X68Mac

A Sharp X68000 computer emulator for macOS and iOS platforms, based on the px68k emulator core.
This repository is indirectly a fork of Mr. Hissorii's [px68k](https://github.com/hissorii/px68k). Based on his source code, [Goroman](https://github.com/GOROman) converted it for iOS, and I made it work on MacOS.

## Overview

X68Mac provides authentic Sharp X68000 emulation with modern Swift UI frameworks, bridging low-level C emulation code with SpriteKit for an optimal user experience on Apple platforms.

## Features

### Core Emulation
- **Complete X68000 Hardware Emulation**: CPU, sound, graphics, and I/O
- **M68000 CPU**: Powered by C68K emulator core
- **FM Sound Synthesis**: High-quality audio via fmgen
- **Multiple Disk Formats**: Support for .dim, .xdf, .hdf files

### macOS Enhancements
- **Dual FDD Support**: Menu-driven management for Drive A and Drive B
- **Hard Disk Support**: Complete HDD management with dedicated menu
- **Screen Rotation**: 90-degree rotation support for vertical games (tate mode)
- **Enhanced Joycard**: Keyboard, mouse, and GameController input
- **Drag & Drop**: Multi-file support with automatic drive assignment
- **Native Menu Integration**: Dedicated FDD, HDD, and Display menus with keyboard shortcuts

### Cross-Platform
- **macOS Support**: Native menu bar and keyboard/mouse input
- **iCloud Integration**: Seamless file sync across devices
- **Universal Binary**: Optimized for both Intel and Apple Silicon

## System Requirements

### macOS
- macOS 11.0 or later
- Intel or Apple Silicon Mac

## ROM Files Setup

X68Mac requires original SHARP X68000 system ROM files to function properly. These files are **not included** with the emulator and must be obtained separately.

### Required ROM Files

You need the following ROM files from an original X68000 system:

| File | Description | Size |
|------|-------------|------|
| `CGROM.DAT` | Character Generator ROM | 768KB |
| `IPLROM.DAT` | Initial Program Loader ROM | 128KB |

### Installation Locations

Place the ROM files in the location:

1. **Document folder**:
   ```
   /Users/<username>/Documents/
   ├── CGROM.DAT
   └── IPLROM.DAT
   ```

### Important Notes

- **Legal Notice**: ROM files are copyrighted by SHARP CORPORATION. You must own an original X68000 system to legally use these files.
- **File Names**: ROM file names are case-sensitive and must be exactly `CGROM.DAT` and `IPLROM.DAT`.
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

1. **Clone the repository:**
   ```bash
   git clone https://github.com/YosAwed/X68Mac.git
   cd X68Mac
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
- **FDD → Open Drive A...** (⌘1): Insert disk into Drive A
- **FDD → Open Drive B...** (⌘2): Insert disk into Drive B
- **FDD → Eject Drive A** (⇧⌘1): Eject disk from Drive A
- **FDD → Eject Drive B** (⇧⌘2): Eject disk from Drive B

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
- **Mouse**: Click on visual joycard buttons

#### Drag & Drop
- Single floppy disk → Drive A
- Two floppy disks → Drive A and Drive B automatically
- Hard disk images (.hdf, .hdm) → HDD loading
- Mixed file types → Automatic assignment by file type

## File Formats

| Extension | Description | Type | Platform Support |
|-----------|-------------|------|-------------------|
| .dim | Standard disk image | Floppy | macOS |
| .xdf | Extended disk format | Floppy | macOS |
| .hdf | Hard disk format | Hard Disk | macOS |

### Hard Disk Usage

Hard disk images provide faster access and larger storage capacity compared to floppy disks:

1. **Loading HDD Images**: Use **HDD → Open Hard Disk...** menu or drag .hdf files
2. **Boot Priority**: When both FDD and HDD are present, X68000 typically boots from HDD
3. **Performance**: HDDs offer significantly faster loading times for large applications
4. **Capacity**: Support for larger disk images suitable for complex software suites
5. **Persistence**: Changes to HDD images are automatically saved

#### Recommended Usage
- **System Boot**: Install X68000 system on HDD for faster startup
- **Applications**: Store large software packages on HDD
- **Development**: Use HDD for compilers and development tools
- **Games**: Multi-disk games can be consolidated onto HDD

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

### Language Stack
- **Swift**: UI layer, device management, file system
- **C**: Core emulation engine (CPU, hardware)
- **C++**: Sound generation, emulation components

### Key Components
- **GameScene.swift**: Main emulation viewport using SpriteKit
- **FileSystem.swift**: File handling and disk image management
- **JoyController.swift**: GameController integration
- **X68JoyCard.swift**: Virtual joycard implementation
- **px68k/**: Complete X68000 hardware emulation in C/C++

## Recent Updates

### Version 2024.1
- ✅ **Dual FDD Drive Support**: Independent Drive A/B management
- ✅ **Hard Disk Drive Support**: Complete HDD management with menu integration
- ✅ **Screen Rotation Support**: 90-degree rotation for vertical games (tate mode)
- ✅ **Enhanced macOS Joycard**: Keyboard and mouse input support
- ✅ **Menu Integration**: Native macOS menu bar with FDD, HDD, and Display shortcuts
- ✅ **Multi-file Drag & Drop**: Automatic drive assignment by file type
- ✅ **Visual Feedback**: Real-time button highlighting
- ✅ **File Format Expansion**: Added .hdf support for hard disks
- ✅ **Window Management**: Automatic window resizing for optimal display

## Development

### Project Structure
```
X68Mac/
├── X68000 Shared/          # Cross-platform Swift code
│   ├── px68k/              # C/C++ emulation core
│   └── *.swift             # Swift UI and logic
├── X68000 iOS/             # iOS-specific code
├── X68000 macOS/           # macOS-specific code
├── c68k/                   # M68000 CPU emulator
└── CLAUDE.md               # Development guidelines
```

### Building
```bash
# Build macOS version
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS"
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test on both platforms
5. Submit a pull request

Please refer to `CLAUDE.md` for detailed development guidelines and architectural notes.

## License

This project is based on px68k emulator. Please refer to the original px68k license terms.

## Acknowledgments

- **px68k**: Original X68000 emulator core
- **C68K**: M68000 CPU emulator
- **fmgen**: FM sound synthesis library
- **Sharp**: Original X68000 computer system

## Links

- [GitHub Repository](https://github.com/YosAwed/X68Mac)
- [Issues & Bug Reports](https://github.com/YosAwed/X68Mac/issues)

---

*SHARP X68000 is a trademark of SHARP CORPORATION. This project is not affiliated with SHARP CORPORATION.*
