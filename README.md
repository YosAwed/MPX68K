# X68Mac

A Sharp X68000 computer emulator for macOS and iOS platforms, based on the px68k emulator core.

## Overview

X68Mac provides authentic Sharp X68000 emulation with modern Swift UI frameworks, bridging low-level C emulation code with SpriteKit for an optimal user experience on Apple platforms.

## Features

### Core Emulation
- **Complete X68000 Hardware Emulation**: CPU, sound, graphics, and I/O
- **M68000 CPU**: Powered by C68K emulator core
- **FM Sound Synthesis**: High-quality audio via fmgen
- **Multiple Disk Formats**: Support for .dim, .xdf, .d88, .hdm, .hdf files

### macOS Enhancements
- **Dual FDD Support**: Menu-driven management for Drive A and Drive B
- **Enhanced Joycard**: Keyboard, mouse, and GameController input
- **Drag & Drop**: Multi-file support with automatic drive assignment
- **Native Menu Integration**: Dedicated FDD menu with keyboard shortcuts

### Cross-Platform
- **iOS Support**: Touch controls and document-based interface
- **macOS Support**: Native menu bar and keyboard/mouse input
- **iCloud Integration**: Seamless file sync across devices
- **Universal Binary**: Optimized for both Intel and Apple Silicon

## System Requirements

### macOS
- macOS 11.0 or later
- Intel or Apple Silicon Mac

### iOS
- iOS 13.4 or later
- iPhone or iPad

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
   - Select your target platform (macOS or iOS)
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

#### Joycard Input
- **Arrow Keys** or **WASD**: 8-direction movement
- **Space** or **J**: Button A
- **Z** or **K**: Button B
- **Mouse**: Click on visual joycard buttons

#### Drag & Drop
- Single floppy disk → Drive A
- Two floppy disks → Drive A and Drive B automatically
- HDD images → Standard loading

### iOS Controls
- **Touch Interface**: Native touch controls for joycard
- **Document Browser**: Access files from various sources
- **GameController**: MFi controller support

## File Formats

| Extension | Description | Platform Support |
|-----------|-------------|-------------------|
| .dim | Standard disk image | macOS, iOS |
| .xdf | Extended disk format | macOS, iOS |
| .d88 | D88 disk format | macOS, iOS |
| .hdm | Hard disk image (Mac) | macOS, iOS |
| .hdf | Hard disk format | macOS, iOS |

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
- ✅ **Enhanced macOS Joycard**: Keyboard and mouse input support
- ✅ **Menu Integration**: Native macOS menu bar with shortcuts
- ✅ **Multi-file Drag & Drop**: Automatic drive assignment
- ✅ **Visual Feedback**: Real-time button highlighting

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

# Build iOS version
xcodebuild -project X68000.xcodeproj -scheme "X68000 iOS"
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

*Sharp X68000 is a trademark of Sharp Corporation. This project is not affiliated with Sharp Corporation.*