//
//  AppDelegate.swift
//  X68000 macOS
//
//  Created by GOROman on 2020/03/28.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Cocoa
import UniformTypeIdentifiers

@NSApplicationMain
class AppDelegate: NSObject, NSApplicationDelegate {
    
    var gameViewController: GameViewController? {
        // 方法1: 静的参照を使用（最も確実）
        if let shared = GameViewController.shared {
            print("🐛 Found GameViewController via shared reference")
            return shared
        }
        
        // 方法2: mainWindow経由
        if let mainWindow = NSApplication.shared.mainWindow,
           let gameVC = mainWindow.contentViewController as? GameViewController {
            print("🐛 Found GameViewController via mainWindow")
            return gameVC
        }
        
        // 方法3: keyWindow経由
        if let keyWindow = NSApplication.shared.keyWindow,
           let gameVC = keyWindow.contentViewController as? GameViewController {
            print("🐛 Found GameViewController via keyWindow")
            return gameVC
        }
        
        // 方法4: 全windowsを検索
        for window in NSApplication.shared.windows {
            if let gameVC = window.contentViewController as? GameViewController {
                print("🐛 Found GameViewController via windows search")
                return gameVC
            }
        }
        
        print("🐛 GameViewController not found - mainWindow: \(NSApplication.shared.mainWindow != nil)")
        print("🐛 Total windows: \(NSApplication.shared.windows.count)")
        return nil
    }

    func applicationDidFinishLaunching(_ aNotification: Notification) {
        // Insert code here to initialize your application
        setupHDDMenu()
    }
    
    private func setupHDDMenu() {
        // Find and modify the HDD menu programmatically
        guard let mainMenu = NSApplication.shared.mainMenu else {
            print("❌ Could not find main menu")
            return
        }
        
        // Look for HDD menu item
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu {
                // Search for "HDD" menu or containing HDD-related items
                if submenu.title.contains("HDD") || 
                   submenu.items.contains(where: { $0.title.contains("Hard Disk") || $0.title.contains("ハードディスク") }) {
                    
                    print("🔧 Found HDD menu: \(submenu.title)")
                    
                    // Add separator if not already present
                    let separator = NSMenuItem.separator()
                    submenu.addItem(separator)
                    
                    // Add "Create Empty HDD..." menu item
                    let createHDDItem = NSMenuItem(
                        title: "Create Empty HDD...",
                        action: #selector(createEmptyHDD(_:)),
                        keyEquivalent: ""
                    )
                    createHDDItem.target = self
                    submenu.addItem(createHDDItem)
                    
                    // Add "Save HDD" menu item
                    let saveHDDItem = NSMenuItem(
                        title: "Save HDD",
                        action: #selector(saveHDD(_:)),
                        keyEquivalent: "s"
                    )
                    saveHDDItem.keyEquivalentModifierMask = [.command, .shift]
                    saveHDDItem.target = self
                    submenu.addItem(saveHDDItem)
                    
                    print("✅ Added 'Create Empty HDD...' and 'Save HDD' menu items")
                    return
                }
            }
        }
        
        // If HDD menu not found, check File menu as backup
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu,
               submenu.title.contains("File") || submenu.title.contains("ファイル") {
                
                print("🔧 Adding HDD creation to File menu as fallback")
                
                let separator = NSMenuItem.separator()
                submenu.addItem(separator)
                
                let createHDDItem = NSMenuItem(
                    title: "Create Empty HDD...",
                    action: #selector(createEmptyHDD(_:)),
                    keyEquivalent: ""
                )
                createHDDItem.target = self
                submenu.addItem(createHDDItem)
                
                let saveHDDItem = NSMenuItem(
                    title: "Save HDD",
                    action: #selector(saveHDD(_:)),
                    keyEquivalent: "s"
                )
                saveHDDItem.keyEquivalentModifierMask = [.command, .shift]
                saveHDDItem.target = self
                submenu.addItem(saveHDDItem)
                
                print("✅ Added 'Create Empty HDD...' and 'Save HDD' to File menu")
                return
            }
        }
        
        print("❌ Could not find suitable menu to add HDD creation item")
    }

    func applicationWillTerminate(_ aNotification: Notification) {
        // Save SRAM data before terminating
        print("🐛 AppDelegate.applicationWillTerminate - saving SRAM")
        gameViewController?.saveSRAM()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
    
    func applicationWillHide(_ notification: Notification) {
        print("🐛 AppDelegate.applicationWillHide - saving data")
        gameViewController?.saveSRAM()
    }
    
    func applicationWillResignActive(_ notification: Notification) {
        print("🐛 AppDelegate.applicationWillResignActive - saving data")
        gameViewController?.saveSRAM()
    }
    
    // ファイルメニューからの「開く」アクション
    @IBAction func openDocument(_ sender: Any) {
        print("🐛 AppDelegate.openDocument() called")
        let openPanel = NSOpenPanel()
        if #available(macOS 11.0, *) {
            openPanel.allowedContentTypes = [
                UTType(filenameExtension: "dim") ?? .data,
                UTType(filenameExtension: "xdf") ?? .data,
                UTType(filenameExtension: "d88") ?? .data,
                UTType(filenameExtension: "hdm") ?? .data,
                UTType(filenameExtension: "hdf") ?? .data
            ]
        } else {
            openPanel.allowedFileTypes = ["dim", "xdf", "d88", "hdm", "hdf"]
        }
        openPanel.allowsMultipleSelection = false
        openPanel.canChooseDirectories = false
        openPanel.canChooseFiles = true
        
        openPanel.begin { response in
            print("🐛 File dialog response: \(response == .OK ? "OK" : "Cancel")")
            if response == .OK, let url = openPanel.url {
                print("🐛 Selected file: \(url.lastPathComponent)")
                self.gameViewController?.load(url)
            }
        }
    }
    
    // アプリケーションレベルでのファイルオープン処理（ダブルクリックで開いた場合）
    func application(_ sender: NSApplication, openFile filename: String) -> Bool {
        let url = URL(fileURLWithPath: filename)
        gameViewController?.load(url)
        return true
    }
    
    // より新しいファイルオープン処理
    func application(_ application: NSApplication, open urls: [URL]) {
        for url in urls {
            gameViewController?.load(url)
        }
    }
    
    // MARK: - FDD Menu Actions
    @IBAction func openFDDDriveA(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.openFDDDriveA called")
        gameViewController?.openFDDDriveA(sender)
    }
    
    @IBAction func openFDDDriveB(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.openFDDDriveB called")
        gameViewController?.openFDDDriveB(sender)
    }
    
    @IBAction func ejectFDDDriveA(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.ejectFDDDriveA called")
        gameViewController?.ejectFDDDriveA(sender)
    }
    
    @IBAction func ejectFDDDriveB(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.ejectFDDDriveB called")
        gameViewController?.ejectFDDDriveB(sender)
    }
    
    // MARK: - HDD Menu Actions
    @IBAction func openHDD(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.openHDD called")
        gameViewController?.openHDD(sender)
    }
    
    @IBAction func ejectHDD(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.ejectHDD called")
        gameViewController?.ejectHDD(sender)
    }
    
    @IBAction func createEmptyHDD(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.createEmptyHDD called")
        gameViewController?.createEmptyHDD(sender)
    }
    
    @IBAction func saveHDD(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.saveHDD called")
        gameViewController?.gameScene?.saveHDD()
    }
    
    // MARK: - Screen Rotation Menu Actions
    @IBAction func rotateScreen(_ sender: Any) {
        print("🐛 AppDelegate.rotateScreen called")
        gameViewController?.rotateScreen(sender)
    }
    
    @IBAction func setLandscapeMode(_ sender: Any) {
        print("🐛 AppDelegate.setLandscapeMode called")
        gameViewController?.setLandscapeMode(sender)
    }
    
    @IBAction func setPortraitMode(_ sender: Any) {
        print("🐛 AppDelegate.setPortraitMode called")
        gameViewController?.setPortraitMode(sender)
    }
    
    // MARK: - System Menu Actions
    @IBAction func resetSystem(_ sender: Any) {
        print("🐛 AppDelegate.resetSystem called")
        gameViewController?.resetSystem(sender)
    }

}

