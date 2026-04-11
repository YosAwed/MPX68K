# MPX68K

A Sharp X68000 computer emulator for macOS platforms, based on the px68k emulator core.
This repository is indirectly a fork of Mr. Hissorii's [px68k](https://github.com/hissorii/px68k). Based on his source code, [Goroman](https://github.com/GOROman) converted it for iOS, and I made it works on MacOS.

## Overview

MPX68K provides authentic Sharp X68000 emulation with modern Swift UI frameworks, bridging low-level C emulation code with SpriteKit for an optimal user experience on both Apple Silicon and Intel Macs.

## Features

### Core Emulation
- **X68000 Hardware Emulation**: CPU, sound, graphics, and I/O
- **M68000 CPU**: Powered by C68K emulator core
- **Adjustable Clock Speed**: 1 / 10 / 16 / 24 (default) / 40 / 50 MHz
- **FM Sound Synthesis**: High-quality audio via fmgen (OPM + ADPCM)
- **MIDI Output**: External MIDI with configurable output delay and buffering
- **Multiple Disk Formats**: Floppy (.dim, .xdf, .d88, .hdm) and hard disk (.hdf, .hds)

### Storage
- **Dual FDD Drives**: Independent management of Drive 0 and Drive 1
- **SASI Hard Disk**: Classic internal SASI HDD boot
- **SCSI Hard Disk**: External CZ-6BS1 compatible SCSI boot with runtime bus switching
- **HDD Authoring**: Create empty HDD images directly from the app (format with `FORMAT.X` under Human68k)
- **Disk State Management**: Auto-mount modes (Disabled / Restore Last Session / Smart Load / Manual) with save/restore and session info

### Display
- **CRT Display Pipeline**: Scanlines, bloom, vignette, noise, curvature, chromatic aberration, and persistence
  - Presets: Off / Subtle / Standard / Enhanced
  - Dedicated SwiftUI settings panel (⌘,)
- **Screen Rotation**: 90-degree rotation for vertical games (tate mode)
- **Background Video Superimpose**: Overlay a local video file or a YouTube URL as a luma-keyed background, with adjustable threshold / softness / intensity

### Input
- **Keyboard & Mouse**: Full macOS-native input with SCC compatibility mode for VS.X double-click reliability
- **X68000 Mouse Capture**: Toggle with ⇧⌘M, pointer warping disabled for clean capture behavior
- **JoyportU Integration**: ATARI-style joystick support with Notify / Command modes
- **GameController Framework**: External MFi / Xbox / PlayStation controllers

### System & Connectivity
- **Serial Communication**: Mouse-only (default), PTY terminal access, TCP client, TCP server
- **Session State**: Secure restorable state on macOS
- **iCloud Document Sync**: ROM and disk images sync across your devices

## System Requirements

### macOS
- macOS 12.0 (Monterey) or later
- Apple Silicon or Intel Mac
- Minimum 4GB RAM
- 1GB free disk space for ROM and disk images

## ROM Files Setup

MPX68K requires original SHARP X68000 system ROM files to function properly. These files are **not included** with the emulator and must be obtained separately.

### Required ROM Files

You need the following ROM files from an original X68000 system:

| File | Description | Size | Required |
|------|-------------|------|----------|
| `CGROM.DAT` | Character Generator ROM | 768KB | Yes |
| `IPLROM.DAT` | Initial Program Loader ROM | 128KB | Yes |
| `IPLROM0.DAT` | SASI-era IPL ROM (used when Storage Bus Mode = SASI) | 128KB | Optional |
| `SCSIEXROM.DAT` | External SCSI ROM (CZ-6BS1 compatible, used when Storage Bus Mode = SCSI) | 8KB | Optional (required for authentic SCSI boot) |

> **Note on SCSI ROM variants**: Only the *external* SCSI ROM (`SCSIEXROM.DAT`, "SCSIEX" type) is supported. The internal SCSI ROM (`SCSIINROM.DAT`, "SCSIIN" type) has an incompatible IOCS table layout and will not work.

### Installation Locations

Place the ROM files in the sandboxed Documents folder:

```
~/Library/Containers/NANKIN.X68000/Data/Documents/X68000/
├── README.txt          (auto-generated on first launch)
├── CGROM.DAT           (required)
├── IPLROM.DAT          (required)
├── IPLROM0.DAT         (optional — SASI IPL)
└── SCSIEXROM.DAT       (optional — external SCSI ROM)
```

### Important Notes

- **Legal Notice**: ROM files are copyrighted by SHARP CORPORATION. You must own an original X68000 system to legally use these files.
- **File Names**: ROM file names are matched exactly as shown above (case-sensitive on some file systems).
- **File Integrity**: Ensure ROM files are not corrupted. The emulator will display an error if required files are missing or invalid.
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

### macOS Menu Reference

#### FDD Menu
- **Open Drive 0…** / **Open Drive 1…**: Insert a floppy image
- **Eject Drive 0** / **Eject Drive 1**: Eject the inserted floppy

#### HDD Menu
- **Open Hard Disk…** / **Eject Hard Disk**: Mount or eject an HDD image
- **Create Empty HDD…**: Generate a new empty `.hdf` image (format later with `FORMAT.X` under Human68k)
- **Save HDD** (⌘S): Persist the current HDD state
- **Storage Bus Mode → SASI / SCSI**: Switch the hard disk bus at runtime
- **SCSI Devices → Open SCSI (ID 0)… / Eject SCSI (ID 0)**: Manage the external SCSI device

#### Clock Menu
- **1 / 10 / 16 / 24 (Default) / 40 / 50 MHz** (keys 1-6): Change CPU clock speed

#### Display Menu
- **Rotate Screen** (⌘R): Toggle between landscape and portrait
- **Landscape Mode** / **Portrait Mode**: Set orientation directly
- **CRT Settings…** (⌘,): Open the SwiftUI CRT configuration panel
- **CRT Display Mode → Off / Subtle / Standard / Enhanced**: Quick CRT preset selection
- **Background Video → Set Video File… / Set YouTube URL…**: Pick a background source
- **Background Video → Superimpose**: Toggle luma-key overlay on/off
- **Background Video → Threshold / Softness / Intensity**: Fine-tune the luma key

#### System Menu
- **Reset System**: Hard-reset the emulator
- **Use X68000 Mouse** (⇧⌘M): Toggle X68000 mouse capture
- **Toggle Input Mode**: Switch between input modes
- **MIDI Output Delay…**: Configure MIDI output delay
- **Delete IPLROM.DAT…**: Remove a cached IPL ROM
- **Serial Communication → Mouse Only / PTY / TCP Connection… / TCP Server… / Disconnect**: Select serial backend
- **Serial Communication → Original Mouse SCC (Compat)**: Enable SCC compatibility mode for VS.X double-click
- **Disk State Management → Auto-Mount Mode**: Disabled / Restore Last Session / Smart Load / Manual Selection
- **Disk State Management → Save Current State / Clear Saved State / Show State Information**
- **JoyportU Settings → Disabled / Notify Mode / Command Mode**

#### Joycard Input
- **Arrow Keys** or **WASD**: 8-direction movement
- **Space** or **J**: Button A
- **Z** or **K**: Button B

## File Formats

| Extension | Description | Type | Notes |
|-----------|-------------|------|-------|
| `.dim` | Standard disk image | Floppy | Most common X68000 format |
| `.xdf` | Extended disk format | Floppy | Extended capacity |
| `.d88` | D88 disk image | Floppy | Common retro-emulator format |
| `.hdm` | Human68k disk image | Floppy | Legacy Human68k format |
| `.hdf` | Generic hard disk image | Hard Disk | SASI / generic HDD |
| `.hds` | SCSI hard disk image | Hard Disk | SCSI HDD — auto-switches Storage Bus Mode to SCSI on open / drop |

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
2. **Direct Selection**: Choose **Display → Portrait Mode** for vertical games
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

### Version 4.2

#### New Features
- **SASI / SCSI Dual-Boot**: Switch the hard disk bus between SASI and SCSI at runtime. SCSI boot uses an IPL-ROM-first architecture with the external CZ-6BS1 ROM mapped at `$EA0000-$EA1FFF`
- **CRT Display Pipeline**: Full shader chain with scanlines, bloom, vignette, noise, curvature, chromatic aberration, and persistence — plus a dedicated SwiftUI settings panel (⌘,) and Off / Subtle / Standard / Enhanced presets
- **Background Video Superimpose**: Overlay a local video file or a YouTube URL as a luma-keyed background, with adjustable threshold / softness / intensity
- **Adjustable CPU Clock**: 1 / 10 / 16 / 24 / 40 / 50 MHz selection from the Clock menu
- **Serial Communication**: Mouse-only (default), PTY terminal access, TCP client, and TCP server backends
- **MIDI Output with Delay**: External MIDI output with configurable delay and buffering
- **JoyportU Support**: ATARI-style joystick integration with Notify / Command modes
- **Disk State Management**: Auto-mount modes (Disabled / Restore Last Session / Smart Load / Manual) with save/restore and session info
- **HDD Authoring**: Create empty `.hdf` images from the app, then format under Human68k with `FORMAT.X`
- **Additional Disk Formats**: Added `.d88` and `.hdm` floppy image support

#### Fixes & Improvements
- **Mouse Reliability**: Capture stability, Y-inversion fix, drift/inertia tuning, and SCC compatibility mode for VS.X double-click
- **Rendering**: Skip unchanged frames to reduce GPU load
- **Audio**: ADPCM / OPM ring-buffer mixing rework, ADPCM timing refinement
- **Build System**: macOS deployment target lowered to 12.0, x86_64 re-enabled for Intel Macs
- **Secure Restorable State**: Enabled on macOS
- **Compiler Warnings**: Legacy dead-code SCSI helpers removed, keeping the build warning-free

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
- Xcode 15.0 or later (with the macOS SDK)
- macOS 12.0 (Monterey) or later as the deployment target
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
