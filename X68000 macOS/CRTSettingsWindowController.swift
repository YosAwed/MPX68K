//
//  CRTSettingsWindowController.swift
//  SwiftUI settings panel hosting for macOS
//

import Cocoa
import SwiftUI

class CRTSettingsWindowController: NSWindowController {
    private weak var gameScene: GameScene?

    convenience init(gameScene: GameScene, settings: CRTSettings, preset: CRTPreset) {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 500, height: 600),
            styleMask: [.titled, .closable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )

        self.init(window: window)

        self.gameScene = gameScene

        window.title = "CRT Display Settings"
        window.titlebarAppearsTransparent = true
        window.titleVisibility = .hidden
        window.isMovableByWindowBackground = true
        window.center()
        window.setFrameAutosaveName("CRTSettingsWindow")

        // Create SwiftUI view with callbacks
        let settingsView = CRTSettingsPanel(
            settings: settings,
            preset: preset,
            onSettingsChanged: { [weak gameScene] newSettings in
                gameScene?.applyCRTSettings(newSettings)
            },
            onPresetChanged: { [weak gameScene] newPreset in
                gameScene?.setCRTDisplayPreset(newPreset)
            }
        )

        // Host SwiftUI view in NSHostingView
        let hostingView = NSHostingView(rootView: settingsView)
        window.contentView = hostingView

        // Window appearance
        window.isReleasedWhenClosed = false
        window.level = .floating

        // Handle close button in SwiftUI (dismiss environment)
        window.standardWindowButton(.closeButton)?.isHidden = true
        window.standardWindowButton(.miniaturizeButton)?.isHidden = true
        window.standardWindowButton(.zoomButton)?.isHidden = true
    }

    func show() {
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }
}
