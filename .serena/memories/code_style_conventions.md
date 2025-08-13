# MPX68K Code Style and Conventions

## Swift Conventions
- **Naming**: PascalCase for classes, camelCase for methods/variables
- **Indentation**: 4 spaces (standard Xcode Swift formatting)
- **Logging**: Use X68Logger system with categories instead of print statements
- **Comments**: Minimal comments, prefer self-documenting code

## Logging System
- **Categories**: .ui, .fileSystem, .input, .audio, .emulation, .x68mac, .network
- **Functions**: debugLog(), infoLog(), warningLog(), errorLog()
- **Format**: `debugLog("message", category: .category)`

## File Organization
- **Shared Code**: X68000 Shared/ for cross-platform logic
- **Platform Specific**: X68000 macOS/ for macOS-specific UI/menu code
- **Bridge Headers**: For Swift-C interoperability

## Menu Integration Pattern
- **Storyboard Definition**: Main.storyboard defines static menu structure
- **Dynamic Updates**: AppDelegate handles menu title updates via timer
- **Action Routing**: Menu actions → AppDelegate → GameViewController
- **Identifiers**: Use consistent ID patterns like "menu-action-item"

## Error Handling
- **Logging**: Comprehensive error logging with categories
- **Graceful Degradation**: Handle missing files/features gracefully
- **User Feedback**: Provide clear error messages through UI