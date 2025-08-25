//
//  AppDelegate.swift
//  X68000 macOS
//
//  Created by GOROman on 2020/03/28.
//  Copyright © 2020 GOROman/Awed. All rights reserved.
//

import Cocoa
import UniformTypeIdentifiers
import os.log

@NSApplicationMain
class AppDelegate: NSObject, NSApplicationDelegate, NSMenuItemValidation {
    
    // Logger for menu updates
    private let logger = Logger(subsystem: "NANKIN.X68000", category: "MenuUpdate")
    
    // Timer for updating menu items
    private var menuUpdateTimer: Timer?
    
    // Mouse mode state tracking
    private var isMouseCaptureEnabled = false
    
    var gameViewController: GameViewController? {
        // 方法1: 静的参照を使用（最も確実）
        if let shared = GameViewController.shared {
            // debugLog("Found GameViewController via shared reference", category: .ui)
            return shared
        }
        
        // 方法2: mainWindow経由
        if let mainWindow = NSApplication.shared.mainWindow,
           let gameVC = mainWindow.contentViewController as? GameViewController {
            // debugLog("Found GameViewController via mainWindow", category: .ui)
            return gameVC
        }
        
        // 方法3: keyWindow経由
        if let keyWindow = NSApplication.shared.keyWindow,
           let gameVC = keyWindow.contentViewController as? GameViewController {
            // debugLog("Found GameViewController via keyWindow", category: .ui)
            return gameVC
        }
        
        // 方法4: 全windowsを検索
        for window in NSApplication.shared.windows {
            if let gameVC = window.contentViewController as? GameViewController {
                // debugLog("Found GameViewController via windows search", category: .ui)
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
        setupSettingsMenu()
        
        // Initial menu update once after app launch  
        DispatchQueue.main.async { [weak self] in
            self?.updateMenuTitles()
        }
        
        // Listen for disk image loading notifications to update menus
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(diskImageLoadedNotification),
            name: .diskImageLoaded,
            object: nil
        )
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
                    
                    // debugLog("Found HDD menu: \(submenu.title)", category: .ui)
                    
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
                
                // debugLog("Adding HDD creation to File menu as fallback", category: .ui)
                
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
    
    private func setupSettingsMenu() {
        guard let mainMenu = NSApplication.shared.mainMenu else {
            errorLog("Could not find main menu for settings", category: .ui)
            return
        }
        
        // Look for Settings or Options menu, or add to main menu if not found
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu {
                // Check for Settings, Preferences, Options, or X68000 menu
                if submenu.title.contains("Settings") || 
                   submenu.title.contains("Preferences") ||
                   submenu.title.contains("Options") ||
                   submenu.title.contains("X68000") {
                    
                    // debugLog("Found settings menu: \(submenu.title)", category: .ui)
                    addAutoMountMenuItem(to: submenu)
                    return
                }
            }
        }
        
        // If no settings menu found, add to the app menu (first menu)
        if let firstMenuItem = mainMenu.items.first,
           let firstSubmenu = firstMenuItem.submenu {
            // debugLog("Adding auto-mount setting to app menu as fallback", category: .ui)
            addAutoMountMenuItem(to: firstSubmenu)
        }
    }
    
    private func addAutoMountMenuItem(to menu: NSMenu) {
        // Separator will be added automatically before Quit menu
        
        // Create Disk State Management submenu
        let diskStateSubmenu = NSMenu(title: "Disk State Management")
        let diskStateMenuItem = NSMenuItem(
            title: "Disk State Management",
            action: nil,
            keyEquivalent: ""
        )
        diskStateMenuItem.submenu = diskStateSubmenu
        
        // Auto-Mount Mode submenu
        let autoMountSubmenu = NSMenu(title: "Auto-Mount Mode")
        let autoMountMenuItem = NSMenuItem(
            title: "Auto-Mount Mode",
            action: nil,
            keyEquivalent: ""
        )
        autoMountMenuItem.submenu = autoMountSubmenu
        
        // Auto-Mount Mode options
        let disabledItem = NSMenuItem(
            title: "Disabled",
            action: #selector(setAutoMountDisabled(_:)),
            keyEquivalent: ""
        )
        disabledItem.target = self
        disabledItem.identifier = NSUserInterfaceItemIdentifier("AutoMount-disabled")
        
        let lastSessionItem = NSMenuItem(
            title: "Restore Last Session",
            action: #selector(setAutoMountLastSession(_:)),
            keyEquivalent: ""
        )
        lastSessionItem.target = self
        lastSessionItem.identifier = NSUserInterfaceItemIdentifier("AutoMount-lastSession")
        
        let smartLoadItem = NSMenuItem(
            title: "Smart Load",
            action: #selector(setAutoMountSmartLoad(_:)),
            keyEquivalent: ""
        )
        smartLoadItem.target = self
        smartLoadItem.identifier = NSUserInterfaceItemIdentifier("AutoMount-smartLoad")
        
        let manualItem = NSMenuItem(
            title: "Manual Selection",
            action: #selector(setAutoMountManual(_:)),
            keyEquivalent: ""
        )
        manualItem.target = self
        manualItem.identifier = NSUserInterfaceItemIdentifier("AutoMount-manual")
        
        // Add mode items to submenu
        autoMountSubmenu.addItem(disabledItem)
        autoMountSubmenu.addItem(lastSessionItem)
        autoMountSubmenu.addItem(smartLoadItem)
        autoMountSubmenu.addItem(manualItem)
        
        // State management actions
        let separator1 = NSMenuItem.separator()
        let saveStateItem = NSMenuItem(
            title: "Save Current State",
            action: #selector(saveDiskState(_:)),
            keyEquivalent: ""
        )
        saveStateItem.target = self
        saveStateItem.keyEquivalent = "s"
        saveStateItem.keyEquivalentModifierMask = [.command, .option]
        
        let clearStateItem = NSMenuItem(
            title: "Clear Saved State",
            action: #selector(clearDiskState(_:)),
            keyEquivalent: ""
        )
        clearStateItem.target = self
        
        let showStateItem = NSMenuItem(
            title: "Show State Information",
            action: #selector(showDiskStateInfo(_:)),
            keyEquivalent: ""
        )
        showStateItem.target = self
        
        // Add items to disk state submenu
        diskStateSubmenu.addItem(autoMountMenuItem)
        diskStateSubmenu.addItem(separator1)
        diskStateSubmenu.addItem(saveStateItem)
        diskStateSubmenu.addItem(clearStateItem)
        diskStateSubmenu.addItem(showStateItem)
        
        // Insert before Quit menu item (find Quit and insert before it)  
        var insertIndex = menu.items.count
        for (index, item) in menu.items.enumerated() {
            if item.keyEquivalent == "q" && item.action == #selector(NSApplication.terminate(_:)) {
                insertIndex = index
                break
            }
        }
        
        // Add separator before Disk State Management if not already present
        if insertIndex > 0 && !menu.items[insertIndex - 1].isSeparatorItem {
            let separator = NSMenuItem.separator()
            menu.insertItem(separator, at: insertIndex)
            insertIndex += 1  // Adjust for the separator we just added
        }
        
        menu.insertItem(diskStateMenuItem, at: insertIndex)
        
        infoLog("Added 'Disk State Management' menu with AutoMountMode options", category: .ui)
    }
    
    // Manual menu update when files are opened/closed
    func updateMenuOnFileOperation() {
        // Add small delay to allow C functions to complete disk operations
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in
            self?.updateMenuTitles()
        }
    }
    
    // Auto-save disk state after disk operations (mount/eject)
    func autoSaveDiskStateIfNeeded() {
        let stateManager = DiskStateManager.shared
        
        // Only auto-save if mode supports it
        switch stateManager.autoMountMode {
        case .lastSession, .smartLoad:
            // Add delay to ensure disk operation completes before saving state
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                let currentState = stateManager.createCurrentState()
                stateManager.saveState(currentState)
                // debugLog("Auto-saved disk state after operation", category: .fileSystem)
            }
        case .disabled, .manual:
            // No auto-save for these modes
            break
        }
    }
    
    private func clearMenuTitles() {
        guard let mainMenu = NSApplication.shared.mainMenu else { return }
        
        // Clear FDD and HDD menu titles using existing loop pattern
        for menuItem in mainMenu.items {
            guard let submenu = menuItem.submenu else { continue }
            
            if menuItem.title == "FDD" {
                // Clear FDD menu titles
                for i in 0..<2 {
                    if let fddItem = submenu.item(withTag: i) {
                        fddItem.title = "FDD\(i): (None)"
                    }
                }
            } else if menuItem.title == "HDD" {
                // Clear HDD menu titles
                for i in 0..<2 {
                    if let hddItem = submenu.item(withTag: i) {
                        hddItem.title = "HDD\(i): (None)"
                    }
                }
            }
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
                logger.debug("Found submenu for menu item: '\(menuItem.title)'")
                if menuItem.title == "FDD" {
                    logger.debug("Updating FDD menu")
                    self.updateFDDMenuTitles(submenu: submenu)
                } else if menuItem.title == "HDD" {
                    logger.debug("Updating HDD menu")
                    self.updateHDDMenuTitles(submenu: submenu)
                } else if menuItem.title == "Display" {
                    logger.debug("Updating Display menu")
                    self.updateMouseMenuCheckmark()
                }
            } else {
                logger.debug("Menu item '\(menuItem.title)' has no submenu")
            }
        }
    }
    
    func updateMenusAfterStateRestore() {
        infoLog("Updating menus after state restoration", category: .ui)
        updateMenuTitles()
    }
    
    @objc private func diskImageLoadedNotification() {
        infoLog("Disk image loaded notification received - updating menus", category: .ui)
        updateMenuTitles()
    }
    
    private func updateFDDMenuTitles(submenu: NSMenu) {
        logger.debug("UpdateFDDMenuTitles - found \(submenu.items.count) items")
        for item in submenu.items {
            let itemId = item.identifier?.rawValue ?? ""
            let itemTitle = item.title
            logger.debug("FDD Menu item: '\(itemTitle)' with ID: '\(itemId)'")
            
            // Update FDD Drive 0 menu items (using title matching as fallback)
            if itemId == "FDD-open-drive-A" || itemTitle.contains("Open Drive 0") || itemTitle.contains("Drive 0:") {
                logger.debug("Updating Drive 0 open item")
                if X68000_IsFDDReady(0) != 0 {
                    logger.debug("Drive 0 is ready")
                    if let filename = X68000_GetFDDFilename(0) {
                        let name = String(cString: filename)
                        logger.debug("Raw filename for Drive 0: '\(name)'")
                        
                        // Check if we have a valid filename
                        if !name.isEmpty && name != "/" && name.count > 1 {
                            let displayName: String
                            if name.hasPrefix("file://") {
                                displayName = URL(string: name)?.lastPathComponent ?? name
                            } else {
                                displayName = URL(fileURLWithPath: name).lastPathComponent
                            }
                            if !displayName.isEmpty {
                                item.title = "Drive 0: \(displayName)"
                                logger.debug("Set Drive 0 title to: Drive 0: \(displayName)")
                            } else {
                                item.title = "Drive 0: [Mounted]"
                                logger.debug("Set Drive 0 title to: Drive 0: [Mounted] (empty displayName)")
                            }
                        } else {
                            item.title = "Drive 0: [Mounted]"
                            logger.debug("Set Drive 0 title to: Drive 0: [Mounted] (invalid name: '\(name)')")
                        }
                    } else {
                        item.title = "Drive 0: [Mounted]"
                        logger.debug("Set Drive 0 title to: Drive 0: [Mounted] (nil filename)")
                    }
                } else {
                    logger.debug("Drive 0 not ready")
                    item.title = "Open Drive 0..."
                }
            } else if itemId == "FDD-eject-drive-A" || itemTitle.contains("Eject Drive 0") {
                logger.debug("Updating Drive 0 eject item")
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
                                item.title = "Eject Drive 0 (\(displayName))"
                            } else {
                                item.title = "Eject Drive 0"
                            }
                        } else {
                            item.title = "Eject Drive 0"
                        }
                    } else {
                        item.title = "Eject Drive 0"
                    }
                    item.isEnabled = true
                } else {
                    item.title = "Eject Drive 0"
                    item.isEnabled = false
                }
            }
            // Update FDD Drive 1 menu items
            else if itemId == "FDD-open-drive-B" || itemTitle.contains("Open Drive 1") || itemTitle.contains("Drive 1:") {
                logger.debug("Updating Drive 1 open item")
                if X68000_IsFDDReady(1) != 0 {
                    logger.debug("Drive 1 is ready")
                    if let filename = X68000_GetFDDFilename(1) {
                        let name = String(cString: filename)
                        logger.debug("Raw filename for Drive 1: '\(name)'")
                        
                        // Check if we have a valid filename
                        if !name.isEmpty && name != "/" && name.count > 1 {
                            let displayName: String
                            if name.hasPrefix("file://") {
                                displayName = URL(string: name)?.lastPathComponent ?? name
                            } else {
                                displayName = URL(fileURLWithPath: name).lastPathComponent
                            }
                            if !displayName.isEmpty {
                                item.title = "Drive 1: \(displayName)"
                                logger.debug("Set Drive 1 title to: Drive 1: \(displayName)")
                            } else {
                                item.title = "Drive 1: [Mounted]"
                                logger.debug("Set Drive 1 title to: Drive 1: [Mounted] (empty displayName)")
                            }
                        } else {
                            item.title = "Drive 1: [Mounted]"
                            logger.debug("Set Drive 1 title to: Drive 1: [Mounted] (invalid name: '\(name)')")
                        }
                    } else {
                        item.title = "Drive 1: [Mounted]"
                        logger.debug("Set Drive 1 title to: Drive 1: [Mounted] (nil filename)")
                    }
                } else {
                    logger.debug("Drive 1 not ready")
                    item.title = "Open Drive 1..."
                }
            } else if itemId == "FDD-eject-drive-B" || itemTitle.contains("Eject Drive 1") {
                logger.debug("Updating Drive 1 eject item")
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
                                item.title = "Eject Drive 1 (\(displayName))"
                            } else {
                                item.title = "Eject Drive 1"
                            }
                        } else {
                            item.title = "Eject Drive 1"
                        }
                    } else {
                        item.title = "Eject Drive 1"
                    }
                    item.isEnabled = true
                } else {
                    item.title = "Eject Drive 1"
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
        // debugLog("AppDelegate.applicationWillTerminate - saving SRAM", category: .x68mac)
        gameViewController?.saveSRAM()
        
        // Stop the menu update timer
        menuUpdateTimer?.invalidate()
        menuUpdateTimer = nil
        
        // Clear menu titles on app termination
        clearMenuTitles()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
    
    func applicationWillHide(_ notification: Notification) {
        // debugLog("AppDelegate.applicationWillHide - saving data", category: .x68mac)
        gameViewController?.saveSRAM()
    }
    
    func applicationWillResignActive(_ notification: Notification) {
        // debugLog("AppDelegate.applicationWillResignActive - saving data", category: .x68mac)
        gameViewController?.saveSRAM()
    }
    
    // ファイルメニューからの「開く」アクション
    @IBAction func openDocument(_ sender: Any) {
        // debugLog("AppDelegate.openDocument() called", category: .fileSystem)
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
            // debugLog("File dialog response: \(response == .OK ? "OK" : "Cancel")", category: .fileSystem)
            if response == .OK, let url = openPanel.url {
                // debugLog("Selected file: \(url.lastPathComponent)", category: .fileSystem)
                self.gameViewController?.load(url)
                self.updateMenuOnFileOperation()  // Immediate menu update
                self.autoSaveDiskStateIfNeeded()
            }
        }
    }
    
    // アプリケーションレベルでのファイルオープン処理（ダブルクリックで開いた場合）
    func application(_ sender: NSApplication, openFile filename: String) -> Bool {
        let url = URL(fileURLWithPath: filename)
        gameViewController?.load(url)
        updateMenuOnFileOperation()  // Immediate menu update
        autoSaveDiskStateIfNeeded()
        return true
    }
    
    // より新しいファイルオープン処理
    func application(_ application: NSApplication, open urls: [URL]) {
        for url in urls {
            gameViewController?.load(url)
        }
        if !urls.isEmpty {
            updateMenuOnFileOperation()  // Immediate menu update
            autoSaveDiskStateIfNeeded()
        }
    }
    
    // MARK: - FDD Menu Actions
    @IBAction func openFDDDriveA(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.openFDDDriveA called")
        gameViewController?.openFDDDriveA(sender)
        updateMenuOnFileOperation()  // Immediate menu update
        autoSaveDiskStateIfNeeded()
    }
    
    @IBAction func openFDDDriveB(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.openFDDDriveB called")
        gameViewController?.openFDDDriveB(sender)
        updateMenuOnFileOperation()  // Immediate menu update
        autoSaveDiskStateIfNeeded()
    }
    
    @IBAction func ejectFDDDriveA(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.ejectFDDDriveA called")
        gameViewController?.ejectFDDDriveA(sender)
        updateMenuOnFileOperation()  // Immediate menu update
        autoSaveDiskStateIfNeeded()
    }
    
    @IBAction func ejectFDDDriveB(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.ejectFDDDriveB called")
        gameViewController?.ejectFDDDriveB(sender)
        updateMenuOnFileOperation()  // Immediate menu update
        autoSaveDiskStateIfNeeded()
    }
    
    // MARK: - HDD Menu Actions
    @IBAction func openHDD(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.openHDD called")
        gameViewController?.openHDD(sender)
        updateMenuOnFileOperation()  // Immediate menu update
        autoSaveDiskStateIfNeeded()
    }
    
    @IBAction func ejectHDD(_ sender: Any) {
        // Reduced logging for performance
        // print("🐛 AppDelegate.ejectHDD called")
        gameViewController?.ejectHDD(sender)
        updateMenuOnFileOperation()  // Immediate menu update
        autoSaveDiskStateIfNeeded()
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
    
    // MARK: - Settings Menu Actions
    @IBAction func toggleAutoMount(_ sender: Any) {
        // Legacy method - replaced by AutoMountMode system
        // Keep for backward compatibility if needed
        let userDefaults = UserDefaults.standard
        let currentDisabled = userDefaults.bool(forKey: "DisableAutoMount")
        
        // Toggle the setting
        userDefaults.set(!currentDisabled, forKey: "DisableAutoMount")
        
        // Update menu checkmark
        if let menuItem = sender as? NSMenuItem {
            menuItem.state = currentDisabled ? .on : .off
        }
    }
    
    // MARK: - Auto-Mount Mode Menu Actions
    
    @IBAction func setAutoMountDisabled(_ sender: Any) {
        DiskStateManager.shared.autoMountMode = .disabled
        infoLog("Auto-mount mode set to: Disabled", category: .fileSystem)
    }
    
    @IBAction func setAutoMountLastSession(_ sender: Any) {
        DiskStateManager.shared.autoMountMode = .lastSession
        infoLog("Auto-mount mode set to: Last Session", category: .fileSystem)
    }
    
    @IBAction func setAutoMountSmartLoad(_ sender: Any) {
        DiskStateManager.shared.autoMountMode = .smartLoad
        infoLog("Auto-mount mode set to: Smart Load", category: .fileSystem)
    }
    
    @IBAction func setAutoMountManual(_ sender: Any) {
        DiskStateManager.shared.autoMountMode = .manual
        infoLog("Auto-mount mode set to: Manual Selection", category: .fileSystem)
    }
    
    @IBAction func clearDiskState(_ sender: Any) {
        let alert = NSAlert()
        alert.messageText = "Clear Disk State History"
        alert.informativeText = "This will clear all saved disk mount states. Are you sure?"
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Clear")
        alert.addButton(withTitle: "Cancel")
        
        let response = alert.runModal()
        if response == .alertFirstButtonReturn {
            DiskStateManager.shared.clearAllStates()
            infoLog("All disk states cleared by user", category: .fileSystem)
        }
    }
    
    @IBAction func saveDiskState(_ sender: Any) {
        debugLog("Manual save disk state requested", category: .fileSystem)
        let currentState = DiskStateManager.shared.createCurrentState()
        debugLog("Manual save: FDD states count = \(currentState.fddStates.count)", category: .fileSystem)
        debugLog("Manual save: HDD state = \(currentState.hddState != nil ? "present" : "nil")", category: .fileSystem)
        DiskStateManager.shared.saveState(currentState)
        
        let alert = NSAlert()
        alert.messageText = "Disk State Saved"
        alert.informativeText = "Current disk mount state has been saved successfully."
        alert.alertStyle = .informational
        alert.addButton(withTitle: "OK")
        alert.runModal()
        
        infoLog("Current disk state saved by user", category: .fileSystem)
    }
    
    @IBAction func showDiskStateInfo(_ sender: Any) {
        let stateManager = DiskStateManager.shared
        let alert = NSAlert()
        alert.messageText = "Disk State Information"
        
        var infoText = "Auto-Mount Mode: \(stateManager.autoMountMode.displayName)\n\n"
        
        debugLog("showDiskStateInfo: Loading last state...", category: .fileSystem)
        if let lastState = stateManager.loadLastState() {
            debugLog("showDiskStateInfo: Found saved state with \(lastState.fddStates.count) FDD states", category: .fileSystem)
            infoText += "Last Saved State:\n"
            infoText += "• Timestamp: \(DateFormatter.localizedString(from: lastState.timestamp, dateStyle: .medium, timeStyle: .short))\n"
            infoText += "• Session ID: \(lastState.sessionId.uuidString.prefix(8))...\n\n"
            
            infoText += "Floppy Drives:\n"
            for fddState in lastState.fddStates {
                let driveName = fddState.drive == 0 ? "Drive 0" : "Drive 1"
                infoText += "• \(driveName): \(fddState.fileName) (\(fddState.isReadOnly ? "Read-Only" : "Read-Write"))\n"
            }
            
            if let hddState = lastState.hddState {
                infoText += "\nHard Drive:\n"
                infoText += "• HDD: \(hddState.fileName) (\(hddState.isReadOnly ? "Read-Only" : "Read-Write"))\n"
            } else {
                infoText += "\nHard Drive: Not mounted\n"
            }
        } else {
            infoText += "No saved state available."
        }
        
        alert.informativeText = infoText
        alert.alertStyle = .informational
        alert.addButton(withTitle: "OK")
        alert.runModal()
        
        debugLog("Displayed disk state information to user", category: .fileSystem)
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
    
    // MARK: - Mouse Mode Actions
    @IBAction func toggleMouseMode(_ sender: Any) {
        debugLog("AppDelegate.toggleMouseMode called", category: .input)
        isMouseCaptureEnabled.toggle()
        
        if isMouseCaptureEnabled {
            // Enable X68000 mouse mode
            gameViewController?.enableMouseCapture()
            infoLog("X68000 mouse mode enabled", category: .input)
        } else {
            // Disable X68000 mouse mode
            gameViewController?.disableMouseCapture()
            infoLog("X68000 mouse mode disabled", category: .input)
        }
        
        // Update menu checkmark immediately
        updateMouseMenuCheckmark()
    }
    
    @IBAction func toggleInputMode(_ sender: Any) {
        gameViewController?.gameScene?.toggleInputMode()
    }
    
    // MARK: - Mouse Mode Direct Control (for F12 key)
    func disableMouseCapture() {
        if isMouseCaptureEnabled {
            debugLog("AppDelegate.disableMouseCapture called via F12 key", category: .input)
            isMouseCaptureEnabled = false
            
            // Disable X68000 mouse mode
            gameViewController?.disableMouseCapture()
            infoLog("X68000 mouse mode disabled via F12", category: .input)
            
            // Update menu checkmark immediately
            updateMouseMenuCheckmark()
        }
    }
    
    private func updateMouseMenuCheckmark() {
        guard let mainMenu = NSApplication.shared.mainMenu else { return }
        
        // Find Display menu and update mouse menu item
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu, submenu.title == "Display" {
                for item in submenu.items {
                    if item.identifier?.rawValue == "Display-mouse-mode" || item.title.contains("Use X68000 Mouse") {
                        item.state = isMouseCaptureEnabled ? .on : .off
                        debugLog("Updated mouse menu checkmark: \(isMouseCaptureEnabled ? "ON" : "OFF")", category: .ui)
                        break
                    }
                }
                break
            }
        }
    }
    
    // MARK: - Menu Validation
    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        // Enable all menu items by default
        if menuItem.identifier?.rawValue == "Display-mouse-mode" || menuItem.title.contains("Use X68000 Mouse") {
            // Always enable the mouse toggle menu item
            menuItem.state = isMouseCaptureEnabled ? .on : .off
            debugLog("Validating mouse menu item - enabled: true, state: \(isMouseCaptureEnabled ? "ON" : "OFF")", category: .ui)
            return true
        } else if menuItem.identifier?.rawValue == "Settings-auto-mount" || menuItem.title.contains("Auto-Mount Disk Images") {
            // Legacy auto-mount menu item - keep for compatibility
            let isAutoMountEnabled = !UserDefaults.standard.bool(forKey: "DisableAutoMount")
            menuItem.state = isAutoMountEnabled ? .on : .off
            return true
        } else if let identifier = menuItem.identifier?.rawValue {
            // Handle AutoMountMode menu items
            let currentMode = DiskStateManager.shared.autoMountMode
            switch identifier {
            case "AutoMount-disabled":
                menuItem.state = (currentMode == .disabled) ? .on : .off
            case "AutoMount-lastSession":
                menuItem.state = (currentMode == .lastSession) ? .on : .off
            case "AutoMount-smartLoad":
                menuItem.state = (currentMode == .smartLoad) ? .on : .off
            case "AutoMount-manual":
                menuItem.state = (currentMode == .manual) ? .on : .off
            default:
                break
            }
        }
        
        // Default validation for other menu items
        return true
    }

}

