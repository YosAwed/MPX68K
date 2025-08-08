//
//  AppDelegate.swift
//  X68000 macOS
//
//  Created by GOROman on 2020/03/28.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Cocoa
import UniformTypeIdentifiers
import os.log

@NSApplicationMain
class AppDelegate: NSObject, NSApplicationDelegate {
    
    // Logger for menu updates
    private let logger = Logger(subsystem: "NANKIN.X68000", category: "MenuUpdate")
    
    // Timer for updating menu items
    private var menuUpdateTimer: Timer?
    
    var gameViewController: GameViewController? {
        // 方法1: 静的参照を使用（最も確実）
        if let shared = GameViewController.shared {
            debugLog("Found GameViewController via shared reference", category: .ui)
            return shared
        }
        
        // 方法2: mainWindow経由
        if let mainWindow = NSApplication.shared.mainWindow,
           let gameVC = mainWindow.contentViewController as? GameViewController {
            debugLog("Found GameViewController via mainWindow", category: .ui)
            return gameVC
        }
        
        // 方法3: keyWindow経由
        if let keyWindow = NSApplication.shared.keyWindow,
           let gameVC = keyWindow.contentViewController as? GameViewController {
            debugLog("Found GameViewController via keyWindow", category: .ui)
            return gameVC
        }
        
        // 方法4: 全windowsを検索
        for window in NSApplication.shared.windows {
            if let gameVC = window.contentViewController as? GameViewController {
                debugLog("Found GameViewController via windows search", category: .ui)
                return gameVC
            }
        }
        
        warningLog("GameViewController not found - mainWindow: \(NSApplication.shared.mainWindow != nil)", category: .ui)
        warningLog("Total windows: \(NSApplication.shared.windows.count)", category: .ui)
        return nil
    }

    func applicationDidFinishLaunching(_ aNotification: Notification) {
        // Insert code here to initialize your application
        setupHDDMenu()
        setupMenuUpdateTimer()
        
        // Test menu update once after app launch  
        DispatchQueue.main.async { [weak self] in
            self?.updateMenuItemTitles()
        }
    }
    
    private func setupHDDMenu() {
        // Find and modify the HDD menu programmatically
        guard let mainMenu = NSApplication.shared.mainMenu else {
            errorLog("Could not find main menu", category: .ui)
            return
        }
        
        // Look for HDD menu item
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu {
                // Search for "HDD" menu or containing HDD-related items
                if submenu.title.contains("HDD") || 
                   submenu.items.contains(where: { $0.title.contains("Hard Disk") || $0.title.contains("ハードディスク") }) {
                    
                    debugLog("Found HDD menu: \(submenu.title)", category: .ui)
                    
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
                    
                    infoLog("Added 'Create Empty HDD...' and 'Save HDD' menu items", category: .ui)
                    return
                }
            }
        }
        
        // If HDD menu not found, check File menu as backup
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu,
               submenu.title.contains("File") || submenu.title.contains("ファイル") {
                
                debugLog("Adding HDD creation to File menu as fallback", category: .ui)
                
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
                
                infoLog("Added 'Create Empty HDD...' and 'Save HDD' to File menu", category: .ui)
                return
            }
        }
        
        errorLog("Could not find suitable menu to add HDD creation item", category: .ui)
    }
    
    private func setupMenuUpdateTimer() {
        // Update menu items every 2 seconds to show current mounted filenames
        logger.debug("Setting up menu update timer")
        menuUpdateTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            self?.logger.debug("Timer fired - updating menu titles")
            self?.updateMenuItemTitles()
        }
    }
    
    private func updateMenuItemTitles() {
        DispatchQueue.main.async { [weak self] in
            self?.updateMenuTitles()
        }
    }
    
    private func updateMenuTitles() {
        guard let mainMenu = NSApplication.shared.mainMenu else { 
            logger.debug("Could not get main menu")
            return 
        }
        
        logger.debug("Updating menu titles - found \(mainMenu.items.count) menu items")
        
        // Find FDD and HDD menus
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu {
                logger.debug("Found submenu: '\(submenu.title)'")
                if submenu.title == "FDD" {
                    logger.debug("Updating FDD menu")
                    self.updateFDDMenuTitles(submenu: submenu)
                } else if submenu.title == "HDD" {
                    logger.debug("Updating HDD menu")
                    self.updateHDDMenuTitles(submenu: submenu)
                }
            } else {
                logger.debug("Menu item '\(menuItem.title)' has no submenu")
            }
        }
    }
    
    private func updateFDDMenuTitles(submenu: NSMenu) {
        logger.debug("UpdateFDDMenuTitles - found \(submenu.items.count) items")
        for item in submenu.items {
            let itemId = item.identifier?.rawValue ?? ""
            let itemTitle = item.title
            logger.debug("FDD Menu item: '\(itemTitle)' with ID: '\(itemId)'")
            
            // Update FDD Drive A menu items (using title matching as fallback)
            if itemId == "FDD-open-drive-A" || itemTitle.contains("Open Drive A") || itemTitle.contains("Drive A:") {
                logger.debug("Updating Drive A open item")
                if X68000_IsFDDReady(0) != 0 {
                    logger.debug("Drive A is ready")
                    if let filename = X68000_GetFDDFilename(0) {
                        let name = String(cString: filename)
                        logger.debug("Raw filename for Drive A: '\(name)'")
                        
                        // Check if we have a valid filename
                        if !name.isEmpty && name != "/" && name.count > 1 {
                            let displayName: String
                            if name.hasPrefix("file://") {
                                displayName = URL(string: name)?.lastPathComponent ?? name
                            } else {
                                displayName = URL(fileURLWithPath: name).lastPathComponent
                            }
                            if !displayName.isEmpty {
                                item.title = "Drive A: \(displayName)"
                                logger.debug("Set Drive A title to: Drive A: \(displayName)")
                            } else {
                                item.title = "Drive A: [Mounted]"
                                logger.debug("Set Drive A title to: Drive A: [Mounted] (empty displayName)")
                            }
                        } else {
                            item.title = "Drive A: [Mounted]"
                            logger.debug("Set Drive A title to: Drive A: [Mounted] (invalid name: '\(name)')")
                        }
                    } else {
                        item.title = "Drive A: [Mounted]"
                        logger.debug("Set Drive A title to: Drive A: [Mounted] (nil filename)")
                    }
                } else {
                    logger.debug("Drive A not ready")
                    item.title = "Open Drive A..."
                }
            } else if itemId == "FDD-eject-drive-A" || itemTitle.contains("Eject Drive A") {
                logger.debug("Updating Drive A eject item")
                if X68000_IsFDDReady(0) != 0 {
                    if let filename = X68000_GetFDDFilename(0) {
                        let name = String(cString: filename)
                        if !name.isEmpty && name != "/" && name.count > 1 {
                            let displayName: String
                            if name.hasPrefix("file://") {
                                displayName = URL(string: name)?.lastPathComponent ?? name
                            } else {
                                displayName = URL(fileURLWithPath: name).lastPathComponent
                            }
                            if !displayName.isEmpty {
                                item.title = "Eject Drive A (\(displayName))"
                            } else {
                                item.title = "Eject Drive A"
                            }
                        } else {
                            item.title = "Eject Drive A"
                        }
                    } else {
                        item.title = "Eject Drive A"
                    }
                    item.isEnabled = true
                } else {
                    item.title = "Eject Drive A"
                    item.isEnabled = false
                }
            }
            // Update FDD Drive B menu items
            else if itemId == "FDD-open-drive-B" || itemTitle.contains("Open Drive B") || itemTitle.contains("Drive B:") {
                logger.debug("Updating Drive B open item")
                if X68000_IsFDDReady(1) != 0 {
                    logger.debug("Drive B is ready")
                    if let filename = X68000_GetFDDFilename(1) {
                        let name = String(cString: filename)
                        logger.debug("Raw filename for Drive B: '\(name)'")
                        
                        // Check if we have a valid filename
                        if !name.isEmpty && name != "/" && name.count > 1 {
                            let displayName: String
                            if name.hasPrefix("file://") {
                                displayName = URL(string: name)?.lastPathComponent ?? name
                            } else {
                                displayName = URL(fileURLWithPath: name).lastPathComponent
                            }
                            if !displayName.isEmpty {
                                item.title = "Drive B: \(displayName)"
                                logger.debug("Set Drive B title to: Drive B: \(displayName)")
                            } else {
                                item.title = "Drive B: [Mounted]"
                                logger.debug("Set Drive B title to: Drive B: [Mounted] (empty displayName)")
                            }
                        } else {
                            item.title = "Drive B: [Mounted]"
                            logger.debug("Set Drive B title to: Drive B: [Mounted] (invalid name: '\(name)')")
                        }
                    } else {
                        item.title = "Drive B: [Mounted]"
                        logger.debug("Set Drive B title to: Drive B: [Mounted] (nil filename)")
                    }
                } else {
                    logger.debug("Drive B not ready")
                    item.title = "Open Drive B..."
                }
            } else if itemId == "FDD-eject-drive-B" || itemTitle.contains("Eject Drive B") {
                logger.debug("Updating Drive B eject item")
                if X68000_IsFDDReady(1) != 0 {
                    if let filename = X68000_GetFDDFilename(1) {
                        let name = String(cString: filename)
                        if !name.isEmpty && name != "/" && name.count > 1 {
                            let displayName: String
                            if name.hasPrefix("file://") {
                                displayName = URL(string: name)?.lastPathComponent ?? name
                            } else {
                                displayName = URL(fileURLWithPath: name).lastPathComponent
                            }
                            if !displayName.isEmpty {
                                item.title = "Eject Drive B (\(displayName))"
                            } else {
                                item.title = "Eject Drive B"
                            }
                        } else {
                            item.title = "Eject Drive B"
                        }
                    } else {
                        item.title = "Eject Drive B"
                    }
                    item.isEnabled = true
                } else {
                    item.title = "Eject Drive B"
                    item.isEnabled = false
                }
            }
        }
    }
    
    private func updateHDDMenuTitles(submenu: NSMenu) {
        logger.debug("UpdateHDDMenuTitles - found \(submenu.items.count) items")
        for item in submenu.items {
            let itemId = item.identifier?.rawValue ?? ""
            let itemTitle = item.title
            logger.debug("HDD Menu item: '\(itemTitle)' with ID: '\(itemId)'")
            
            if itemId == "HDD-open" || itemTitle.contains("Open Hard Disk") || itemTitle.contains("HDD:") {
                logger.debug("Updating HDD open item")
                if X68000_IsHDDReady() != 0 {
                    logger.debug("HDD is ready")
                    if let filename = X68000_GetHDDFilename() {
                        let name = String(cString: filename)
                        let displayName: String
                        if name.hasPrefix("file://") {
                            displayName = URL(string: name)?.lastPathComponent ?? name
                        } else {
                            displayName = URL(fileURLWithPath: name).lastPathComponent
                        }
                        item.title = "HDD: \(displayName)"
                        logger.debug("Set HDD title to: HDD: \(displayName)")
                    } else {
                        item.title = "HDD: [Mounted]"
                        logger.debug("Set HDD title to: HDD: [Mounted]")
                    }
                } else {
                    logger.debug("HDD not ready")
                    item.title = "Open Hard Disk..."
                }
            } else if itemId == "HDD-eject" || itemTitle.contains("Eject Hard Disk") {
                logger.debug("Updating HDD eject item")
                if X68000_IsHDDReady() != 0 {
                    if let filename = X68000_GetHDDFilename() {
                        let name = String(cString: filename)
                        let displayName: String
                        if name.hasPrefix("file://") {
                            displayName = URL(string: name)?.lastPathComponent ?? name
                        } else {
                            displayName = URL(fileURLWithPath: name).lastPathComponent
                        }
                        item.title = "Eject Hard Disk (\(displayName))"
                    } else {
                        item.title = "Eject Hard Disk"
                    }
                    item.isEnabled = true
                } else {
                    item.title = "Eject Hard Disk"
                    item.isEnabled = false
                }
            }
        }
    }

    func applicationWillTerminate(_ aNotification: Notification) {
        // Save SRAM data before terminating
        debugLog("AppDelegate.applicationWillTerminate - saving SRAM", category: .x68mac)
        gameViewController?.saveSRAM()
        
        // Stop the menu update timer
        menuUpdateTimer?.invalidate()
        menuUpdateTimer = nil
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
    
    func applicationWillHide(_ notification: Notification) {
        debugLog("AppDelegate.applicationWillHide - saving data", category: .x68mac)
        gameViewController?.saveSRAM()
    }
    
    func applicationWillResignActive(_ notification: Notification) {
        debugLog("AppDelegate.applicationWillResignActive - saving data", category: .x68mac)
        gameViewController?.saveSRAM()
    }
    
    // ファイルメニューからの「開く」アクション
    @IBAction func openDocument(_ sender: Any) {
        debugLog("AppDelegate.openDocument() called", category: .fileSystem)
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
            debugLog("File dialog response: \(response == .OK ? "OK" : "Cancel")", category: .fileSystem)
            if response == .OK, let url = openPanel.url {
                debugLog("Selected file: \(url.lastPathComponent)", category: .fileSystem)
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

