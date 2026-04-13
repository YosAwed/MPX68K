//
//  MonitorWindowController.swift
//  X68000 macOS
//
//  Floating panel that hosts the Machine Monitor SwiftUI view.
//  Pattern mirrors CRTSettingsWindowController.swift.
//

import Cocoa
import SwiftUI

class MonitorWindowController: NSWindowController, NSWindowDelegate {

    convenience init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 720, height: 520),
            styleMask: [.titled, .closable, .resizable, .miniaturizable],
            backing: .buffered,
            defer: false
        )
        self.init(window: window)

        window.title = "Machine Monitor"
        window.isReleasedWhenClosed = false
        window.setFrameAutosaveName("MachineMonitorWindow")
        window.contentView = NSHostingView(rootView: MonitorView())
        window.level = .floating
        window.delegate = self
        window.center()
    }

    func show() {
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func windowWillClose(_ notification: Notification) {
        // Resume emulation if monitor is closed while paused
        X68000_Monitor_SetPaused(0)
    }
}
