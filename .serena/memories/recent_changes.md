# Recent Changes and Current State

## Version 4.1.0 (Build 908) - Direct File I/O Implementation

### Major Changes Implemented
1. **Direct File I/O for HDD (SASI)**
   - Replaced memory buffer system with immediate file writes
   - Added READ CAPACITY command (0x25) for correct HDD capacity detection
   - Modified SASI_Flush() and SASI_Seek() to use fopen/fwrite/fread
   - Fixed HDD size restoration after reset to prevent save failures

2. **Direct File I/O for FDD**
   - **DIM format**: Immediate sector writes with fallback to eject-time saves
   - **XDF format**: Similar direct write implementation
   - **D88 format**: Dirty flag system with optimized eject-time saving

3. **Performance Improvements**
   - Removed timer-based auto-save system from GameScene.swift
   - Eliminated processing interruptions during emulation
   - Added comprehensive logging for file operations

### Technical Implementation Details
- **SASI Changes**: Direct file operations bypass SASI macros
- **FDD Changes**: Each format has tailored write-back strategy
- **Error Handling**: Proper fallback mechanisms for failed direct writes
- **Memory Management**: Maintained compatibility while adding direct I/O

### Files Modified
- `X68000 Shared/px68k/x68k/sasi.c` - Direct HDD I/O
- `X68000 Shared/px68k/x68k/disk_dim.c` - DIM direct writes
- `X68000 Shared/px68k/x68k/disk_xdf.c` - XDF direct writes  
- `X68000 Shared/px68k/x68k/disk_d88.c` - D88 dirty flag system
- `X68000 Shared/GameScene.swift` - Removed auto-save timer
- `X68000 macOS/Info.plist` - Version update to 4.1.0

### TestFlight Status
- Archive created successfully (X68000.xcarchive)
- Export completed (TestFlight/X68000.app)
- Ready for TestFlight distribution via Xcode Organizer or Transporter

## Current State
- All direct file I/O implementations tested and working
- Build process verified with Release configuration
- Version incremented and committed to git
- Ready for TestFlight submission