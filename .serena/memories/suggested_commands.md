# Essential Development Commands

## Building the Project

### Primary Build Method
```bash
# Open in Xcode (recommended)
open X68000.xcodeproj
```

### Command Line Builds
```bash
# List available schemes and targets
xcodebuild -list -project X68000.xcodeproj

# Build macOS version (Debug)
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug

# Build macOS version (Release)
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release

# Build iOS version
xcodebuild -project X68000.xcodeproj -scheme "X68000 iOS" -configuration Debug

# Clean build artifacts
xcodebuild clean -project X68000.xcodeproj -scheme "X68000 macOS"
```

### Dependencies (Critical)
```bash
# Build C68K static library FIRST (required dependency)
xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k" -configuration Debug
xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k mac" -configuration Debug
```

## TestFlight/Release
```bash
# Create archive for TestFlight
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release archive -archivePath ./X68000.xcarchive

# Export for distribution
xcodebuild -exportArchive -archivePath ./X68000.xcarchive -exportPath ./TestFlight -exportOptionsPlist ./ExportOptions.plist
```

## Git Workflow
```bash
# Standard git operations
git status
git add .
git commit -m "commit message"
git push

# Check recent commits
git log --oneline -5
```

## File Operations (macOS)
```bash
# List files and directories
ls -la

# Search for files
find . -name "*.swift" -type f
find . -name "*.c" -type f

# Search in files
grep -r "pattern" --include="*.swift" .
rg "pattern" --type swift  # ripgrep (preferred)

# Directory navigation
cd path/to/directory
pwd
```

## Development Verification
```bash
# Verify archive structure
ls -la ./X68000.xcarchive/

# Check Info.plist version
cat "X68000 macOS/Info.plist" | grep -A1 "CFBundleShortVersionString\|CFBundleVersion"
```