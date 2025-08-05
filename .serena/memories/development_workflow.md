# Development Workflow and Guidelines

## Code Style and Conventions

### Swift
- Standard Swift naming conventions (camelCase for variables/functions, PascalCase for types)
- Use `@objc` annotations for Swift functions called from C
- Bridging headers expose C APIs to Swift code
- Memory management across Swift-C boundary requires careful attention

### C/C++
- Traditional C naming conventions
- Function signatures must remain Swift-compatible when exposed
- Direct file I/O implementation for disk operations
- Error handling with proper return codes

## When Task is Completed

### Build Verification
1. **Clean and build both dependencies**:
   ```bash
   xcodebuild clean -project c68k/c68k.xcodeproj -scheme "c68k mac"
   xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k mac" -configuration Debug
   xcodebuild clean -project X68000.xcodeproj -scheme "X68000 macOS"
   xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug
   ```

2. **Test functionality**: Launch emulator and verify changes work correctly

3. **Version management**: Update version numbers in Info.plist when appropriate

### Git Workflow
1. **Stage changes**: `git add .`
2. **Commit with descriptive message**: Include 🤖 footer for Claude-generated commits
3. **Push to remote**: `git push`

### Release Process (TestFlight)
1. **Update version**: Increment CFBundleShortVersionString and CFBundleVersion in Info.plist
2. **Create release archive**: Use Release configuration
3. **Export for distribution**: Create .app bundle for TestFlight
4. **Upload**: Use Xcode Organizer or Transporter app

## Testing Approach
- **No specific test framework**: Manual testing with emulator
- **Focus areas**: File I/O operations, disk image handling, cross-platform compatibility
- **Regression testing**: Verify existing functionality still works after changes

## Critical Dependencies
- **C68K library must be built first**: Main project will fail without libc68k.a
- **ROM files required**: CGROM.DAT and IPLROM.DAT needed for emulator to function
- **File format support**: .dim, .xdf, .d88 (floppy), .hdf, .hdm (hard disk)