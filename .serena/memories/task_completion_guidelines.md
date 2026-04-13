# Task Completion Guidelines for MPX68K

## When Task is Completed

### Build Verification
```bash
# Always verify builds complete successfully
xcodebuild -project X68000.xcodeproj -scheme "X68000 macOS" -configuration Debug build
```

### Testing Checklist
- **Manual Testing**: Launch app and verify functionality works
- **Menu Integration**: Check menu items appear and respond correctly
- **Input Testing**: Verify input changes work as expected
- **No Automated Tests**: Project does not have unit or integration tests

### Code Quality
- **Zero Warnings**: Ensure no compiler warnings are introduced
- **Logging**: Use X68Logger system instead of print statements
- **Memory Safety**: Verify proper Swift-C interoperability
- **Thread Safety**: Ensure UI updates happen on main thread

### Documentation
- **Do not create** documentation files unless explicitly requested
- **Update CLAUDE.md** only if architectural changes are significant
- **Inline comments** should be minimal and only when necessary

### Version Control
- **No automatic commits** - only commit when explicitly requested
- **Clean working directory** - ensure no temporary files are left behind
- **Descriptive commit messages** if committing

## Specific to Mouse Implementation
- **Test mouse capture/release** functionality
- **Verify menu checkmark** updates correctly  
- **Test cursor visibility** changes
- **Ensure proper cleanup** when switching modes