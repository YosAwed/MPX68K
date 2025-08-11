# Suggested Commands for MPX68K Development

## Building the Project

### Primary Build Method
```bash
# Open in Xcode (recommended)
open X68000.xcodeproj
```

### Command Line Build
```bash
# List available schemes and targets
xcodebuild -list -project X68000.xcodeproj

# Build macOS version (Debug)
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug

# Build macOS version (Release)
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Release

# Clean build artifacts
xcodebuild clean -project X68000.xcodeproj -scheme "X68000 macOS"
```

### Dependencies
```bash
# Build C68K static library (required dependency)
xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k" -configuration Debug
xcodebuild -project c68k/c68k.xcodeproj -scheme "c68k mac" -configuration Debug
```

## Development Utilities

### File System Operations
```bash
ls -la                    # List files and directories
find . -name "*.swift"    # Find Swift files
grep -r "pattern" .       # Search for patterns in code
```

### Git Operations
```bash
git status               # Check repository status
git add .               # Stage changes
git commit -m "message" # Commit changes
```

## Testing and Quality Assurance
- **No specific test commands defined** - manual testing required
- **No linting tools configured** - code quality managed through Xcode warnings
- **No formatting tools** - Swift formatting handled by Xcode