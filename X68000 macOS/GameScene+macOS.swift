//
//  GameScene+macOS.swift
//  macOS-specific extensions for GameScene
//

import Foundation
import SpriteKit
import AppKit

extension GameScene {
    /// Show modern SwiftUI CRT settings panel (macOS only)
    @objc func showModernCRTSettings() {
        // Close existing window if open
        if let windowController = crtSettingsWindowController as? CRTSettingsWindowController {
            windowController.window?.close()
            crtSettingsWindowController = nil
            return
        }

        let current = currentCRTSettings()
        let windowController = CRTSettingsWindowController(
            gameScene: self,
            settings: current,
            preset: crtPreset
        )
        crtSettingsWindowController = windowController
        windowController.show()
    }

    /// Helper method to get current CRT preset (needed from macOS extension)
    @objc var currentCRTPreset: CRTPreset {
        return crtPreset
    }
}
