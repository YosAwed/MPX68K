#!/usr/bin/env python3
"""Check production US symbol translation, generic F11-F20 remapping and migration."""
from pathlib import Path
import subprocess
import re
import tempfile

root = Path(__file__).resolve().parents[2]
scene = (root / 'X68000 Shared/GameScene.swift').read_text()
start = scene.index('    private func usSymbolMapping(')
end = scene.index('\n    private func setGuestShift', start)
function = scene[start:end].replace('private func', 'func', 1)
checks = r'''
let cases: [(Character, UInt32, Bool)] = [
    ("@", 0x40, false), ("\"", 0x32, true), ("'", 0x37, true),
    ("&", 0x36, true), ("(", 0x38, true), (")", 0x39, true),
    ("*", 0x3a, true), ("+", 0x3b, true), ("=", 0x2d, true),
    ("^", 0x5e, false), ("~", 0x5e, true), ("`", 0x40, true),
    ("{", 0x5b, true), ("}", 0x5d, true), ("|", 0x5c, true),
    ("_", 0x5f, true), (";", 0x3b, false), (":", 0x3a, false),
    ("[", 0x5b, false), ("]", 0x5d, false), ("\\", 0x5c, false)
]
for (char, key, shift) in cases {
    guard let result = usSymbolMapping(char), result.0 == key, result.1 == shift else {
        fatalError("wrong mapping for \(char)")
    }
}
precondition(usSymbolMapping("a") == nil)
print("US keyboard symbol mappings passed")
'''
with tempfile.TemporaryDirectory(prefix='mpx68k-keyboard-') as directory:
    source = Path(directory) / 'main.swift'
    source.write_text(function + '\n' + checks)
    subprocess.run(['xcrun', 'swiftc', '-module-cache-path', directory + '/cache',
                    str(source), '-o', directory + '/test'], check=True)
    subprocess.run([directory + '/test'], check=True)

# Check the actual C table behind the Swift table indices, including special keys.
keyboard = (root / 'X68000 Shared/px68k/x11/keyboard.c').read_text(errors='replace')
table = keyboard.split('BYTE KeyTable[KEYTABLE_MAX] = {', 1)[1].split('};', 1)[0]
table = re.sub(r'//[^\n]*', '', table)
values = [0 if token.strip() == 'NC' else int(token.strip(), 0)
          for token in table.split(',') if token.strip()]
for index, code in {0x40: 0x1b, 0x32: 0x03, 0x37: 0x08, 0x36: 0x07,
                    0x38: 0x09, 0x39: 0x0a, 0x3a: 0x28, 0x3b: 0x27,
                    0x2d: 0x0c, 0x5e: 0x0d, 0x5b: 0x1c, 0x5d: 0x29,
                    0x5c: 0x0e, 0x5f: 0x34, 0x1cb: 0x5b, 0x1cc: 0x72, 0x1cd: 0x73, 0x1ce: 0x62, 0x1cf: 0x3f,
                    0x1d0: 0x5a, 0x1d1: 0x5c, 0x1d2: 0x52}.items():
    assert values[index] == code, (index, values[index], code)
print('X68000 key table mappings passed')

# Exercise actual key-down/key-up code with fake events and a recording core.
start = scene.index('    private func usSymbolMapping(')
end = scene.index('    func getMacKeyToX68KeyTableIndex(', start)
methods = scene[start:end].replace('private func', 'func').replace('override func', 'func')
config_start = scene.index('    static let configurableFunctionKeys:')
config_end = scene.index('    // Translate the character', config_start)
config = scene[config_start:config_end]
harness = r'''
import Foundation
struct Flags: OptionSet {
    let rawValue: Int
    static let shift = Flags(rawValue: 1)
}
struct NSEvent {
    let keyCode: UInt16
    let characters: String?
    let modifierFlags: Flags
}
var events: [String] = []
func X68000_Key_Down(_ key: UInt32) { events.append("down:\(key)") }
func X68000_Key_Up(_ key: UInt32) { events.append("up:\(key)") }
class Joycard {
    func handleKeyDown(_ code: UInt16, _ pressed: Bool) {}
    func handleCharacterInput(_ text: String, _ pressed: Bool) {}
}
class Keyboard {
    enum InputMode { case keyboard, joycard }
    var currentInputMode: InputMode = .keyboard
    var joycard: Joycard? = Joycard()
    var isShiftKeyPressed = false
    var mappedPressedKeys: [UInt16: (index: UInt32, shift: Bool)] = [:]
    func handleX68KeyboardInput(_ event: NSEvent, isKeyDown: Bool) {
        _ = handleMappedKeyboardInput(event, isKeyDown: isKeyDown)
    }
'''+config+methods+r'''
}
let suiteName = "MPX68K.KeyboardTests.\(UUID().uuidString)"
let testDefaults = UserDefaults(suiteName: suiteName)!
defer { testDefaults.removePersistentDomain(forName: suiteName) }
testDefaults.set(true, forKey: "USKeyboardLayout")
let keyboard = Keyboard()
let down = NSEvent(keyCode: 19, characters: "@", modifierFlags: .shift)
let up = NSEvent(keyCode: 19, characters: "2", modifierFlags: [])
precondition(keyboard.handleMappedKeyboardInput(down, isKeyDown: true))
precondition(keyboard.handleMappedKeyboardInput(up, isKeyDown: false))
precondition(events == ["down:64", "up:64"])
events = []
let quote = NSEvent(keyCode: 39, characters: "'", modifierFlags: [])
precondition(keyboard.handleMappedKeyboardInput(quote, isKeyDown: true))
precondition(keyboard.handleMappedKeyboardInput(quote, isKeyDown: false))
precondition(events == ["down:481", "down:55", "up:55", "up:481"])
events = []
let roman = NSEvent(keyCode: 107, characters: nil, modifierFlags: [])
precondition(keyboard.handleMappedKeyboardInput(roman, isKeyDown: true))
precondition(keyboard.handleMappedKeyboardInput(roman, isKeyDown: false))
precondition(events == ["down:459", "up:459"])
events = []
UserDefaults.standard.set("opt2", forKey: "KeyboardRemapping.F14")
precondition(keyboard.handleMappedKeyboardInput(roman, isKeyDown: true))
precondition(keyboard.handleMappedKeyboardInput(roman, isKeyDown: false))
precondition(events == ["down:461", "up:461"])
UserDefaults.standard.set("roman", forKey: "KeyboardRemapping.F14")
events = []
let f12 = NSEvent(keyCode: 111, characters: nil, modifierFlags: [])
precondition(keyboard.handleMappedKeyboardInput(f12, isKeyDown: true))
precondition(events.isEmpty)
UserDefaults.standard.set("roman", forKey: "KeyboardRemapping.F12")
precondition(keyboard.handleMappedKeyboardInput(f12, isKeyDown: true))
UserDefaults.standard.set("opt1", forKey: "KeyboardRemapping.F12")
precondition(keyboard.handleMappedKeyboardInput(f12, isKeyDown: false))
precondition(events == ["down:459", "up:459"])
UserDefaults.standard.set("disabled", forKey: "KeyboardRemapping.F12")
events = []
let f20 = NSEvent(keyCode: 90, characters: nil, modifierFlags: [])
precondition(keyboard.handleMappedKeyboardInput(f20, isKeyDown: true))
precondition(events.isEmpty)
UserDefaults.standard.set("insert", forKey: "KeyboardRemapping.F20")
precondition(keyboard.handleMappedKeyboardInput(f20, isKeyDown: true))
precondition(keyboard.handleMappedKeyboardInput(f20, isKeyDown: false))
precondition(events == ["down:354", "up:354"])
UserDefaults.standard.removeObject(forKey: "KeyboardRemapping.F20")
events = []
let overlap = Keyboard()
let semicolon = NSEvent(keyCode: 41, characters: ";", modifierFlags: [])
precondition(overlap.handleMappedKeyboardInput(quote, isKeyDown: true))
precondition(overlap.handleMappedKeyboardInput(semicolon, isKeyDown: true))
overlap.keyUp(with: semicolon)
precondition(overlap.handleMappedKeyboardInput(quote, isKeyDown: true))
precondition(events == ["down:481", "down:55", "up:481", "down:59", "up:59", "down:481", "down:55"])
overlap.keyUp(with: quote)
precondition(overlap.mappedPressedKeys.isEmpty && !overlap.isShiftKeyPressed)

// Route releases through the actual keyUp implementation after a mode change.
events = []
let switched = Keyboard()
precondition(switched.handleMappedKeyboardInput(quote, isKeyDown: true))
precondition(switched.handleMappedKeyboardInput(roman, isKeyDown: true))
switched.currentInputMode = .joycard
switched.keyUp(with: roman)
switched.keyUp(with: quote)
precondition(events == ["down:481", "down:55", "up:481", "down:459", "up:459", "up:55"])
precondition(switched.mappedPressedKeys.isEmpty && !switched.isShiftKeyPressed)
// Function remapping stays active with US symbol translation disabled.
events = []
UserDefaults.standard.set(false, forKey: "USKeyboardLayout")
UserDefaults.standard.set("roman", forKey: "KeyboardRemapping.F14")
let generic = Keyboard()
precondition(generic.handleMappedKeyboardInput(roman, isKeyDown: true))
UserDefaults.standard.set("opt1", forKey: "KeyboardRemapping.F14")
precondition(generic.handleMappedKeyboardInput(roman, isKeyDown: false))
precondition(events == ["down:459", "up:459"])
precondition(!generic.handleMappedKeyboardInput(quote, isKeyDown: true))
UserDefaults.standard.set("default", forKey: "KeyboardRemapping.F12")
precondition(!generic.handleMappedKeyboardInput(f12, isKeyDown: true))
UserDefaults.standard.set("disabled", forKey: "KeyboardRemapping.F12")
precondition(generic.handleMappedKeyboardInput(f12, isKeyDown: true))
UserDefaults.standard.set("unknown", forKey: "KeyboardRemapping.F12")
precondition(Keyboard.functionKeyAction(for: "F12", defaultAction: 0) == Keyboard.defaultFunctionKeyAction)
// A legacy JPN setup must retain normal defaults despite stored inactive US settings.
UserDefaults.standard.removeObject(forKey: "KeyboardRemapping.Migrated")
for key in Keyboard.configurableFunctionKeys {
    UserDefaults.standard.removeObject(forKey: "KeyboardRemapping." + key.name)
}
UserDefaults.standard.set(2, forKey: "USKeyboard.F14")
Keyboard.migrateFunctionKeyMappings()
precondition(Keyboard.functionKeyAction(for: "F14", defaultAction: 2) == Keyboard.defaultFunctionKeyAction)
// Two remapped sources sharing a guest key release it only after the last source.
events = []
UserDefaults.standard.set("roman", forKey: "KeyboardRemapping.F14")
UserDefaults.standard.set("roman", forKey: "KeyboardRemapping.F20")
let shared = Keyboard()
precondition(shared.handleMappedKeyboardInput(roman, isKeyDown: true))
precondition(shared.handleMappedKeyboardInput(f20, isKeyDown: true))
precondition(shared.handleMappedKeyboardInput(roman, isKeyDown: false))
precondition(events == ["down:459"])
precondition(shared.handleMappedKeyboardInput(f20, isKeyDown: false))
precondition(events == ["down:459", "up:459"])
print("Keyboard remapping event handling and migration passed")
'''
harness = harness.replace('UserDefaults.standard', 'testDefaults')
with tempfile.TemporaryDirectory(prefix='mpx68k-keyboard-events-') as directory:
    source = Path(directory) / 'main.swift'
    source.write_text(harness)
    subprocess.run(['xcrun', 'swiftc', '-module-cache-path', directory + '/cache',
                    str(source), '-o', directory + '/test'], check=True)
    subprocess.run([directory + '/test'], check=True)
