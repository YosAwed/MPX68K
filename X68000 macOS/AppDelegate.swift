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

    // Caching for FDD/HDD status to prevent infinite loops
    private var cachedFDDReady: [Int: Bool] = [:]
    private var cachedFDDFilename: [Int: String?] = [:]
    private var cachedHDDReady: Bool = false
    private var cachedHDDFilename: String? = nil
    private var lastDriveStatusCheck: TimeInterval = 0
    private let driveStatusCheckInterval: TimeInterval = 1.0

    // Prevent recursive calls to updateMenuTitles
    private var isUpdatingMenuTitles = false
    private var isUpdatingSerialMenuCheckmarks = false

    // Caching for SCC compat mode to prevent infinite loops
    private var cachedSCCCompatMode: Int32 = 0
    private var lastSCCCompatCheck: TimeInterval = 0
    private let sccCompatCheckInterval: TimeInterval = 1.0
    
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
        // Enable verbose logs for troubleshooting
        X68LogConfig.enableInfoLogs = true
        X68LogConfig.enableDebugLogs = true
        infoLog("Verbose logging enabled (info/debug)", category: .ui)

        // Defer menu setup to next runloop to ensure storyboard menu is fully loaded.
        // Build a consistent menu to avoid AppKit validating an incomplete storyboard menu.
        // Note: We also build in applicationWillFinishLaunching for even earlier stabilization.
        rebuildMenuSystem()
        // Perform initial updates after build
        updateMenuTitles()
        updateCRTMenuCheckmarks()
        updateSerialMenuCheckmarks()
        updateJoyportUMenuCheckmarks()
        
        // Listen for disk image loading notifications to update menus
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(diskImageLoadedNotification),
            name: .diskImageLoaded,
            object: nil
        )
    }

    // MARK: - Menu System Rebuild
    private func rebuildMenuSystem() {
        infoLog("Rebuilding corrupted menu system programmatically", category: .ui)

        // Create new main menu
        let mainMenu = NSMenu(title: "Main Menu")

        // App Menu (X68000)
        let appMenuItem = NSMenuItem(title: "X68000", action: nil, keyEquivalent: "")
        let appMenu = NSMenu(title: "X68000")
        appMenuItem.submenu = appMenu

        // About X68000
        let aboutItem = NSMenuItem(title: "About X68000", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(aboutItem)

        appMenu.addItem(NSMenuItem.separator())

        // Services submenu
        let servicesItem = NSMenuItem(title: "Services", action: nil, keyEquivalent: "")
        let servicesMenu = NSMenu(title: "Services")
        servicesItem.submenu = servicesMenu
        NSApp.servicesMenu = servicesMenu
        appMenu.addItem(servicesItem)

        appMenu.addItem(NSMenuItem.separator())

        // Hide/Show items
        let hideItem = NSMenuItem(title: "Hide X68000", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        appMenu.addItem(hideItem)

        let hideOthersItem = NSMenuItem(title: "Hide Others", action: #selector(NSApplication.hideOtherApplications(_:)), keyEquivalent: "h")
        hideOthersItem.keyEquivalentModifierMask = [.command, .option]
        appMenu.addItem(hideOthersItem)

        let showAllItem = NSMenuItem(title: "Show All", action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: "")
        appMenu.addItem(showAllItem)

        appMenu.addItem(NSMenuItem.separator())

        // Quit
        let quitItem = NSMenuItem(title: "Quit X68000", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appMenu.addItem(quitItem)

        mainMenu.addItem(appMenuItem)

        // FDD Menu
        let fddMenuItem = NSMenuItem(title: "FDD", action: nil, keyEquivalent: "")
        let fddMenu = NSMenu(title: "FDD")
        fddMenuItem.submenu = fddMenu

        // Drive 0
        let openDrive0Item = NSMenuItem(title: "Open Drive 0...", action: #selector(openFDDDriveA(_:)), keyEquivalent: "")
        openDrive0Item.target = self
        openDrive0Item.identifier = NSUserInterfaceItemIdentifier("FDD-open-drive-A")
        fddMenu.addItem(openDrive0Item)

        let ejectDrive0Item = NSMenuItem(title: "Eject Drive 0", action: #selector(ejectFDDDriveA(_:)), keyEquivalent: "")
        ejectDrive0Item.target = self
        ejectDrive0Item.identifier = NSUserInterfaceItemIdentifier("FDD-eject-drive-A")
        fddMenu.addItem(ejectDrive0Item)

        fddMenu.addItem(NSMenuItem.separator())

        // Drive 1
        let openDrive1Item = NSMenuItem(title: "Open Drive 1...", action: #selector(openFDDDriveB(_:)), keyEquivalent: "")
        openDrive1Item.target = self
        openDrive1Item.identifier = NSUserInterfaceItemIdentifier("FDD-open-drive-B")
        fddMenu.addItem(openDrive1Item)

        let ejectDrive1Item = NSMenuItem(title: "Eject Drive 1", action: #selector(ejectFDDDriveB(_:)), keyEquivalent: "")
        ejectDrive1Item.target = self
        ejectDrive1Item.identifier = NSUserInterfaceItemIdentifier("FDD-eject-drive-B")
        fddMenu.addItem(ejectDrive1Item)

        mainMenu.addItem(fddMenuItem)

        // HDD Menu
        let hddMenuItem = NSMenuItem(title: "HDD", action: nil, keyEquivalent: "")
        let hddMenu = NSMenu(title: "HDD")
        hddMenuItem.submenu = hddMenu

        let openHDDItem = NSMenuItem(title: "Open Hard Disk...", action: #selector(openHDD(_:)), keyEquivalent: "")
        openHDDItem.target = self
        openHDDItem.identifier = NSUserInterfaceItemIdentifier("HDD-open")
        hddMenu.addItem(openHDDItem)

        let ejectHDDItem = NSMenuItem(title: "Eject Hard Disk", action: #selector(ejectHDD(_:)), keyEquivalent: "")
        ejectHDDItem.target = self
        ejectHDDItem.identifier = NSUserInterfaceItemIdentifier("HDD-eject")
        hddMenu.addItem(ejectHDDItem)

        hddMenu.addItem(NSMenuItem.separator())

        let createHDDItem = NSMenuItem(title: "Create Empty HDD...", action: #selector(createEmptyHDD(_:)), keyEquivalent: "")
        createHDDItem.target = self
        hddMenu.addItem(createHDDItem)

        let saveHDDItem = NSMenuItem(title: "Save HDD", action: #selector(saveHDD(_:)), keyEquivalent: "s")
        saveHDDItem.keyEquivalentModifierMask = [.command, .shift]
        saveHDDItem.target = self
        hddMenu.addItem(saveHDDItem)

        mainMenu.addItem(hddMenuItem)

        // Clock Menu
        let clockMenuItem = NSMenuItem(title: "Clock", action: nil, keyEquivalent: "")
        let clockMenu = NSMenu(title: "Clock")
        clockMenuItem.submenu = clockMenu

        let clk1 = NSMenuItem(title: "1 MHz", action: #selector(GameViewController.setClock1MHz(_:)), keyEquivalent: "1")
        clk1.keyEquivalentModifierMask = [.control]
        clk1.target = nil
        clockMenu.addItem(clk1)

        let clk10 = NSMenuItem(title: "10 MHz", action: #selector(GameViewController.setClock10MHz(_:)), keyEquivalent: "2")
        clk10.keyEquivalentModifierMask = [.control]
        clk10.target = nil
        clockMenu.addItem(clk10)

        let clk16 = NSMenuItem(title: "16 MHz", action: #selector(GameViewController.setClock16MHz(_:)), keyEquivalent: "3")
        clk16.keyEquivalentModifierMask = [.control]
        clk16.target = nil
        clockMenu.addItem(clk16)

        let clk24 = NSMenuItem(title: "24 MHz (Default)", action: #selector(GameViewController.setClock24MHz(_:)), keyEquivalent: "4")
        clk24.keyEquivalentModifierMask = [.control]
        clk24.target = nil
        clockMenu.addItem(clk24)

        clockMenu.addItem(NSMenuItem.separator())

        let clk40 = NSMenuItem(title: "40 MHz", action: #selector(GameViewController.setClock40MHz(_:)), keyEquivalent: "5")
        clk40.keyEquivalentModifierMask = [.control]
        clk40.target = nil
        clockMenu.addItem(clk40)

        let clk50 = NSMenuItem(title: "50 MHz (Max)", action: #selector(GameViewController.setClock50MHz(_:)), keyEquivalent: "6")
        clk50.keyEquivalentModifierMask = [.control]
        clk50.target = nil
        clockMenu.addItem(clk50)

        mainMenu.addItem(clockMenuItem)

        // Display Menu
        let displayMenuItem = NSMenuItem(title: "Display", action: nil, keyEquivalent: "")
        let displayMenu = NSMenu(title: "Display")
        displayMenuItem.submenu = displayMenu

        let rotateItem = NSMenuItem(title: "Rotate Screen", action: #selector(rotateScreen(_:)), keyEquivalent: "r")
        rotateItem.target = self
        displayMenu.addItem(rotateItem)

        let landscapeItem = NSMenuItem(title: "Landscape Mode", action: #selector(setLandscapeMode(_:)), keyEquivalent: "")
        landscapeItem.target = self
        displayMenu.addItem(landscapeItem)

        let portraitItem = NSMenuItem(title: "Portrait Mode", action: #selector(setPortraitMode(_:)), keyEquivalent: "")
        portraitItem.target = self
        displayMenu.addItem(portraitItem)

        displayMenu.addItem(NSMenuItem.separator())

        let mouseToggleItem = NSMenuItem(title: "Use X68000 Mouse", action: #selector(toggleMouseMode(_:)), keyEquivalent: "m")
        mouseToggleItem.keyEquivalentModifierMask = [.command, .shift]
        mouseToggleItem.target = self
        mouseToggleItem.identifier = NSUserInterfaceItemIdentifier("Display-mouse-mode")
        displayMenu.addItem(mouseToggleItem)

        let inputToggleItem = NSMenuItem(title: "Toggle Input Mode", action: #selector(toggleInputMode(_:)), keyEquivalent: "")
        inputToggleItem.target = self
        displayMenu.addItem(inputToggleItem)

        let crtSettingsItem = NSMenuItem(title: "CRT Settings...", action: #selector(showCRTSettings(_:)), keyEquivalent: ",")
        crtSettingsItem.keyEquivalentModifierMask = [.command, .shift]
        crtSettingsItem.target = self
        displayMenu.addItem(crtSettingsItem)

        // Debug menu items (Invert/Tint) removed

        // CRT Display Mode submenu
        displayMenu.addItem(NSMenuItem.separator())
        let crtMenuItem = NSMenuItem(title: "CRT Display Mode", action: nil, keyEquivalent: "")
        crtMenuItem.identifier = NSUserInterfaceItemIdentifier("Display-CRTMode")
        let crtSubmenu = NSMenu(title: "CRT Display Mode")
        crtMenuItem.submenu = crtSubmenu

        let crtOff = NSMenuItem(title: "Off", action: #selector(setCRTModeOff(_:)), keyEquivalent: "")
        crtOff.target = self
        crtOff.identifier = NSUserInterfaceItemIdentifier("CRTMode-off")
        crtSubmenu.addItem(crtOff)

        let crtSubtle = NSMenuItem(title: "Subtle", action: #selector(setCRTModeSubtle(_:)), keyEquivalent: "")
        crtSubtle.target = self
        crtSubtle.identifier = NSUserInterfaceItemIdentifier("CRTMode-subtle")
        crtSubmenu.addItem(crtSubtle)

        let crtStandard = NSMenuItem(title: "Standard", action: #selector(setCRTModeStandard(_:)), keyEquivalent: "")
        crtStandard.target = self
        crtStandard.identifier = NSUserInterfaceItemIdentifier("CRTMode-standard")
        crtSubmenu.addItem(crtStandard)

        let crtEnhanced = NSMenuItem(title: "Enhanced", action: #selector(setCRTModeEnhanced(_:)), keyEquivalent: "")
        crtEnhanced.target = self
        crtEnhanced.identifier = NSUserInterfaceItemIdentifier("CRTMode-enhanced")
        crtSubmenu.addItem(crtEnhanced)

        displayMenu.addItem(crtMenuItem)

        // Background Video submenu
        let bgMenuItem = NSMenuItem(title: "Background Video", action: nil, keyEquivalent: "")
        let bgMenu = NSMenu(title: "Background Video")
        bgMenuItem.submenu = bgMenu

        let setVideo = NSMenuItem(title: "Set Video File…", action: #selector(setBackgroundVideo(_:)), keyEquivalent: "")
        setVideo.target = self
        bgMenu.addItem(setVideo)

        let removeVideo = NSMenuItem(title: "Remove Video", action: #selector(removeBackgroundVideo(_:)), keyEquivalent: "")
        removeVideo.target = self
        bgMenu.addItem(removeVideo)

        bgMenu.addItem(NSMenuItem.separator())

        let enableVideo = NSMenuItem(title: "Enable", action: #selector(toggleBackgroundVideo(_:)), keyEquivalent: "")
        enableVideo.target = self
        enableVideo.identifier = NSUserInterfaceItemIdentifier("BGVideo-Enable")
        bgMenu.addItem(enableVideo)

        let threshItem = NSMenuItem(title: "Threshold", action: #selector(adjustBGVideoThreshold(_:)), keyEquivalent: "")
        threshItem.target = self
        threshItem.identifier = NSUserInterfaceItemIdentifier("BGVideo-Threshold")
        bgMenu.addItem(threshItem)

        let softItem = NSMenuItem(title: "Softness", action: #selector(adjustBGVideoSoftness(_:)), keyEquivalent: "")
        softItem.target = self
        softItem.identifier = NSUserInterfaceItemIdentifier("BGVideo-Softness")
        bgMenu.addItem(softItem)

        let alphaItem = NSMenuItem(title: "Intensity", action: #selector(adjustBGVideoAlpha(_:)), keyEquivalent: "")
        alphaItem.target = self
        alphaItem.identifier = NSUserInterfaceItemIdentifier("BGVideo-Alpha")
        bgMenu.addItem(alphaItem)


        displayMenu.addItem(bgMenuItem)

        mainMenu.addItem(displayMenuItem)

        // System Menu
        let systemMenuItem = NSMenuItem(title: "System", action: nil, keyEquivalent: "")
        let systemMenu = NSMenu(title: "System")
        systemMenuItem.submenu = systemMenu

        let resetItem = NSMenuItem(title: "Reset System", action: #selector(resetSystem(_:)), keyEquivalent: "")
        resetItem.target = self
        systemMenu.addItem(resetItem)

        // Serial Communication submenu
        systemMenu.addItem(NSMenuItem.separator())
        let serialMenuItem = NSMenuItem(title: "Serial Communication", action: nil, keyEquivalent: "")
        let serialMenu = NSMenu(title: "Serial Communication")
        serialMenuItem.submenu = serialMenu

        let mouseOnlyItem = NSMenuItem(title: "Mouse Only (Default)", action: #selector(setSerialMouseOnly(_:)), keyEquivalent: "")
        mouseOnlyItem.target = self
        mouseOnlyItem.identifier = NSUserInterfaceItemIdentifier("Serial-mouseOnly")
        serialMenu.addItem(mouseOnlyItem)

        let ptyItem = NSMenuItem(title: "PTY (Terminal Access)", action: #selector(setSerialPTY(_:)), keyEquivalent: "")
        ptyItem.target = self
        ptyItem.identifier = NSUserInterfaceItemIdentifier("Serial-PTY")
        serialMenu.addItem(ptyItem)

        let tcpItem = NSMenuItem(title: "TCP Connection...", action: #selector(setSerialTCP(_:)), keyEquivalent: "")
        tcpItem.target = self
        tcpItem.identifier = NSUserInterfaceItemIdentifier("Serial-TCP")
        serialMenu.addItem(tcpItem)

        let tcpServerItem = NSMenuItem(title: "TCP Server...", action: #selector(setSerialTCPServer(_:)), keyEquivalent: "")
        tcpServerItem.target = self
        tcpServerItem.identifier = NSUserInterfaceItemIdentifier("Serial-TCPServer")
        serialMenu.addItem(tcpServerItem)

        serialMenu.addItem(NSMenuItem.separator())

        let disconnectItem = NSMenuItem(title: "Disconnect", action: #selector(disconnectSerial(_:)), keyEquivalent: "")
        disconnectItem.target = self
        serialMenu.addItem(disconnectItem)

        serialMenu.addItem(NSMenuItem.separator())

        // CRITICAL: SCC Compatibility Mode toggle - this was causing the infinite loop
        let compatItem = NSMenuItem(title: "Original Mouse SCC (Compat)", action: #selector(toggleSCCCompatMode(_:)), keyEquivalent: "")
        compatItem.target = self
        compatItem.identifier = NSUserInterfaceItemIdentifier("Serial-CompatMouse")
        serialMenu.addItem(compatItem)

        systemMenu.addItem(serialMenuItem)

        // Disk State Management submenu
        systemMenu.addItem(NSMenuItem.separator())
        let diskStateMenuItem = NSMenuItem(title: "Disk State Management", action: nil, keyEquivalent: "")
        let diskStateMenu = NSMenu(title: "Disk State Management")
        diskStateMenuItem.submenu = diskStateMenu

        // Auto-Mount Mode submenu
        let autoMountMenuItem = NSMenuItem(title: "Auto-Mount Mode", action: nil, keyEquivalent: "")
        let autoMountMenu = NSMenu(title: "Auto-Mount Mode")
        autoMountMenuItem.submenu = autoMountMenu

        let disabledItem = NSMenuItem(title: "Disabled", action: #selector(setAutoMountDisabled(_:)), keyEquivalent: "")
        disabledItem.target = self
        disabledItem.identifier = NSUserInterfaceItemIdentifier("AutoMount-disabled")
        autoMountMenu.addItem(disabledItem)

        let lastSessionItem = NSMenuItem(title: "Restore Last Session", action: #selector(setAutoMountLastSession(_:)), keyEquivalent: "")
        lastSessionItem.target = self
        lastSessionItem.identifier = NSUserInterfaceItemIdentifier("AutoMount-lastSession")
        autoMountMenu.addItem(lastSessionItem)

        let smartLoadItem = NSMenuItem(title: "Smart Load", action: #selector(setAutoMountSmartLoad(_:)), keyEquivalent: "")
        smartLoadItem.target = self
        smartLoadItem.identifier = NSUserInterfaceItemIdentifier("AutoMount-smartLoad")
        autoMountMenu.addItem(smartLoadItem)

        let manualItem = NSMenuItem(title: "Manual Selection", action: #selector(setAutoMountManual(_:)), keyEquivalent: "")
        manualItem.target = self
        manualItem.identifier = NSUserInterfaceItemIdentifier("AutoMount-manual")
        autoMountMenu.addItem(manualItem)

        diskStateMenu.addItem(autoMountMenuItem)
        diskStateMenu.addItem(NSMenuItem.separator())

        let saveStateItem = NSMenuItem(title: "Save Current State", action: #selector(saveDiskState(_:)), keyEquivalent: "s")
        saveStateItem.keyEquivalentModifierMask = [.command, .option]
        saveStateItem.target = self
        diskStateMenu.addItem(saveStateItem)

        let clearStateItem = NSMenuItem(title: "Clear Saved State", action: #selector(clearDiskState(_:)), keyEquivalent: "")
        clearStateItem.target = self
        diskStateMenu.addItem(clearStateItem)

        let showStateItem = NSMenuItem(title: "Show State Information", action: #selector(showDiskStateInfo(_:)), keyEquivalent: "")
        showStateItem.target = self
        diskStateMenu.addItem(showStateItem)

        systemMenu.addItem(diskStateMenuItem)

        // JoyportU Settings submenu
        systemMenu.addItem(NSMenuItem.separator())
        let joyportUMenuItem = NSMenuItem(title: "JoyportU Settings", action: nil, keyEquivalent: "")
        let joyportUMenu = NSMenu(title: "JoyportU Settings")
        joyportUMenuItem.submenu = joyportUMenu

        let joyportUDisabledItem = NSMenuItem(title: "Disabled", action: #selector(setJoyportUDisabled(_:)), keyEquivalent: "")
        joyportUDisabledItem.target = self
        joyportUDisabledItem.identifier = NSUserInterfaceItemIdentifier("JoyportU-disabled")
        joyportUMenu.addItem(joyportUDisabledItem)

        let notifyModeItem = NSMenuItem(title: "Notify Mode", action: #selector(setJoyportUNotifyMode(_:)), keyEquivalent: "")
        notifyModeItem.target = self
        notifyModeItem.identifier = NSUserInterfaceItemIdentifier("JoyportU-notifyMode")
        joyportUMenu.addItem(notifyModeItem)

        let commandModeItem = NSMenuItem(title: "Command Mode", action: #selector(setJoyportUCommandMode(_:)), keyEquivalent: "")
        commandModeItem.target = self
        commandModeItem.identifier = NSUserInterfaceItemIdentifier("JoyportU-commandMode")
        joyportUMenu.addItem(commandModeItem)

        systemMenu.addItem(joyportUMenuItem)

        mainMenu.addItem(systemMenuItem)

        // Window Menu (standard)
        let windowMenuItem = NSMenuItem(title: "Window", action: nil, keyEquivalent: "")
        let windowMenu = NSMenu(title: "Window")
        windowMenuItem.submenu = windowMenu

        let minimizeItem = NSMenuItem(title: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        windowMenu.addItem(minimizeItem)

        let zoomItem = NSMenuItem(title: "Zoom", action: #selector(NSWindow.performZoom(_:)), keyEquivalent: "")
        windowMenu.addItem(zoomItem)

        windowMenu.addItem(NSMenuItem.separator())

        let bringAllToFrontItem = NSMenuItem(title: "Bring All to Front", action: #selector(NSApplication.arrangeInFront(_:)), keyEquivalent: "")
        windowMenu.addItem(bringAllToFrontItem)

        mainMenu.addItem(windowMenuItem)

        // Help Menu
        let helpMenuItem = NSMenuItem(title: "Help", action: nil, keyEquivalent: "")
        let helpMenu = NSMenu(title: "Help")
        helpMenuItem.submenu = helpMenu

        let helpItem = NSMenuItem(title: "X68000 Help", action: #selector(NSApplication.showHelp(_:)), keyEquivalent: "?")
        helpMenu.addItem(helpItem)

        mainMenu.addItem(helpMenuItem)

        // Wire system menus for AppKit
        NSApp.servicesMenu = servicesMenu
        NSApp.windowsMenu = windowMenu
        NSApp.helpMenu = helpMenu

        // Set the new main menu
        NSApplication.shared.mainMenu = mainMenu

        infoLog("Successfully rebuilt menu system programmatically", category: .ui)
    }

    // MARK: - CRT Display Mode Actions
    private func persistCRTPreset(_ preset: CRTPreset) {
        UserDefaults.standard.set(preset.rawValue, forKey: "CRTDisplayPreset")
    }

    @objc func setCRTModeOff(_ sender: Any?) {
        if let scene = gameViewController?.gameScene { scene.setCRTDisplayPreset(.off) } else { persistCRTPreset(.off) }
        updateCRTMenuCheckmarks()
    }
    @objc func setCRTModeSubtle(_ sender: Any?) {
        if let scene = gameViewController?.gameScene { scene.setCRTDisplayPreset(.subtle) } else { persistCRTPreset(.subtle) }
        updateCRTMenuCheckmarks()
    }
    @objc func setCRTModeStandard(_ sender: Any?) {
        if let scene = gameViewController?.gameScene { scene.setCRTDisplayPreset(.standard) } else { persistCRTPreset(.standard) }
        updateCRTMenuCheckmarks()
    }
    @objc func setCRTModeEnhanced(_ sender: Any?) {
        if let scene = gameViewController?.gameScene { scene.setCRTDisplayPreset(.enhanced) } else { persistCRTPreset(.enhanced) }
        updateCRTMenuCheckmarks()
    }

    private func currentCRTPreset() -> CRTPreset {
        if let s = UserDefaults.standard.string(forKey: "CRTDisplayPreset"), let p = CRTPreset(rawValue: s) {
            return p
        }
        return .off
    }

    // Helper: find item by identifier in a menu (no native API)
    private func menuItem(in menu: NSMenu, withIdentifier raw: String) -> NSMenuItem? {
        let id = NSUserInterfaceItemIdentifier(raw)
        for item in menu.items {
            if item.identifier == id { return item }
        }
        return nil
    }

    func updateCRTMenuCheckmarks() {
        guard let mainMenu = NSApplication.shared.mainMenu else { return }
        for mi in mainMenu.items where mi.title == "Display" {
            guard let displayMenu = mi.submenu else { continue }
            if let crtMenuItem = menuItem(in: displayMenu, withIdentifier: "Display-CRTMode"),
               let crtSub = crtMenuItem.submenu {
                let preset = currentCRTPreset()
                for item in crtSub.items {
                    item.state = .off
                }
                let idMap: [CRTPreset: String] = [
                    .off: "CRTMode-off",
                    .subtle: "CRTMode-subtle",
                    .standard: "CRTMode-standard",
                    .enhanced: "CRTMode-enhanced"
                ]
                if let id = idMap[preset], let mark = menuItem(in: crtSub, withIdentifier: id) {
                    mark.state = .on
                }
            }
            // Debug checkmarks removed
        }
    }

    // MARK: - Show CRT Settings Panel
    @objc func showCRTSettings(_ sender: Any?) {
        if let scene = gameViewController?.gameScene {
            scene.toggleCRTSettingsPanel()
        }
    }

    // Debug toggle handlers removed

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
                    // Avoid duplicate insertion
                    if submenu.items.contains(where: { $0.title.contains("Create Empty HDD") }) {
                        debugLog("HDD creation items already present", category: .ui)
                        return
                    }
                    
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
                    addSerialMenuItem(to: submenu)
                    addJoyportUMenuItem(to: submenu)
                    return
                }
            }
        }
        
        // If no settings menu found, add to the app menu (first menu)
        if let firstMenuItem = mainMenu.items.first,
           let firstSubmenu = firstMenuItem.submenu {
            // debugLog("Adding auto-mount setting to app menu as fallback", category: .ui)
            addAutoMountMenuItem(to: firstSubmenu)
            addSerialMenuItem(to: firstSubmenu)
            addJoyportUMenuItem(to: firstSubmenu)
        }
    }
    
    private func addAutoMountMenuItem(to menu: NSMenu) {
        // Skip if already added
        if menu.items.contains(where: { $0.submenu?.title == "Disk State Management" }) {
            debugLog("'Disk State Management' menu already present", category: .ui)
            return
        }
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
    
    private func addSerialMenuItem(to menu: NSMenu) {
        // Skip if already added
        if menu.items.contains(where: { $0.submenu?.title == "Serial Communication" }) {
            debugLog("'Serial Communication' menu already present", category: .ui)
            return
        }
        // Create Serial Communication submenu
        let serialSubmenu = NSMenu(title: "Serial Communication")
        let serialMenuItem = NSMenuItem(
            title: "Serial Communication",
            action: nil,
            keyEquivalent: ""
        )
        serialMenuItem.submenu = serialSubmenu
        
        // Serial mode options
        let mouseOnlyItem = NSMenuItem(
            title: "Mouse Only (Default)",
            action: #selector(setSerialMouseOnly(_:)),
            keyEquivalent: ""
        )
        mouseOnlyItem.target = self
        mouseOnlyItem.identifier = NSUserInterfaceItemIdentifier("Serial-mouseOnly")
        
        let ptyItem = NSMenuItem(
            title: "PTY (Terminal Access)",
            action: #selector(setSerialPTY(_:)),
            keyEquivalent: ""
        )
        ptyItem.target = self
        ptyItem.identifier = NSUserInterfaceItemIdentifier("Serial-PTY")
        
        let tcpItem = NSMenuItem(
            title: "TCP Connection...",
            action: #selector(setSerialTCP(_:)),
            keyEquivalent: ""
        )
        tcpItem.target = self
        tcpItem.identifier = NSUserInterfaceItemIdentifier("Serial-TCP")
        
        let tcpServerItem = NSMenuItem(
            title: "TCP Server...",
            action: #selector(setSerialTCPServer(_:)),
            keyEquivalent: ""
        )
        tcpServerItem.target = self
        tcpServerItem.identifier = NSUserInterfaceItemIdentifier("Serial-TCPServer")
        
        let separator = NSMenuItem.separator()

        let compatItem = NSMenuItem(
            title: "Original Mouse SCC (Compat)",
            action: #selector(toggleSCCCompatMode(_:)),
            keyEquivalent: ""
        )
        compatItem.target = self
        compatItem.identifier = NSUserInterfaceItemIdentifier("Serial-CompatMouse")
        
        let disconnectItem = NSMenuItem(
            title: "Disconnect",
            action: #selector(disconnectSerial(_:)),
            keyEquivalent: ""
        )
        disconnectItem.target = self
        
        // Add mode items to submenu
        serialSubmenu.addItem(mouseOnlyItem)
        serialSubmenu.addItem(ptyItem)
        serialSubmenu.addItem(tcpItem)
        serialSubmenu.addItem(tcpServerItem)
        serialSubmenu.addItem(separator)
        serialSubmenu.addItem(disconnectItem)
        serialSubmenu.addItem(NSMenuItem.separator())
        serialSubmenu.addItem(compatItem)
        
        // Find insertion point before Quit menu item
        var insertIndex = menu.items.count
        for (index, item) in menu.items.enumerated() {
            if item.keyEquivalent == "q" && item.action == #selector(NSApplication.terminate(_:)) {
                insertIndex = index
                break
            }
        }
        
        // Add separator before Serial Communication if not already present
        if insertIndex > 0 && !menu.items[insertIndex - 1].isSeparatorItem {
            let separator = NSMenuItem.separator()
            menu.insertItem(separator, at: insertIndex)
            insertIndex += 1
        }
        
        menu.insertItem(serialMenuItem, at: insertIndex)
        
        infoLog("Added 'Serial Communication' menu with mode options", category: .ui)
    }
    
    private func addJoyportUMenuItem(to menu: NSMenu) {
        // Skip if already added
        if menu.items.contains(where: { $0.submenu?.title == "JoyportU Settings" }) {
            debugLog("'JoyportU Settings' menu already present", category: .ui)
            return
        }
        // Create JoyportU submenu
        let joyportUSubmenu = NSMenu(title: "JoyportU Settings")
        let joyportUMenuItem = NSMenuItem(
            title: "JoyportU Settings",
            action: nil,
            keyEquivalent: ""
        )
        joyportUMenuItem.submenu = joyportUSubmenu
        
        // JoyportU mode options
        let disabledItem = NSMenuItem(
            title: "Disabled",
            action: #selector(setJoyportUDisabled(_:)),
            keyEquivalent: ""
        )
        disabledItem.target = self
        disabledItem.identifier = NSUserInterfaceItemIdentifier("JoyportU-disabled")
        
        let notifyModeItem = NSMenuItem(
            title: "Notify Mode",
            action: #selector(setJoyportUNotifyMode(_:)),
            keyEquivalent: ""
        )
        notifyModeItem.target = self
        notifyModeItem.identifier = NSUserInterfaceItemIdentifier("JoyportU-notifyMode")
        
        let commandModeItem = NSMenuItem(
            title: "Command Mode",
            action: #selector(setJoyportUCommandMode(_:)),
            keyEquivalent: ""
        )
        commandModeItem.target = self
        commandModeItem.identifier = NSUserInterfaceItemIdentifier("JoyportU-commandMode")
        
        // Add mode items to submenu
        joyportUSubmenu.addItem(disabledItem)
        joyportUSubmenu.addItem(notifyModeItem)
        joyportUSubmenu.addItem(commandModeItem)
        
        // Find insertion point before Quit menu item
        var insertIndex = menu.items.count
        for (index, item) in menu.items.enumerated() {
            if item.keyEquivalent == "q" && item.action == #selector(NSApplication.terminate(_:)) {
                insertIndex = index
                break
            }
        }
        
        // Add separator before JoyportU Settings if not already present
        if insertIndex > 0 && !menu.items[insertIndex - 1].isSeparatorItem {
            let separator = NSMenuItem.separator()
            menu.insertItem(separator, at: insertIndex)
            insertIndex += 1
        }
        
        menu.insertItem(joyportUMenuItem, at: insertIndex)
        
        infoLog("Added 'JoyportU Settings' menu with mode options", category: .ui)
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
            // (Background Video labels are updated in updateCRTMenuCheckmarks())
        }
    }

    // MARK: - Background Video Actions
    @objc func setBackgroundVideo(_ sender: Any?) {
        print("DEBUG: setBackgroundVideo called")
        guard let gvc = gameViewController else {
            print("DEBUG: No gameViewController")
            return
        }
        print("DEBUG: gameViewController exists, gameScene: \(gvc.gameScene != nil)")

        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        if #available(macOS 12.0, *) {
            var types: [UTType] = []
            if let t = UTType(filenameExtension: "mp4") { types.append(t) }
            if let t = UTType(filenameExtension: "mov") { types.append(t) }
            if let t = UTType(filenameExtension: "m4v") { types.append(t) }
            panel.allowedContentTypes = types.isEmpty ? [.movie] : types
        } else {
            panel.allowedFileTypes = ["mp4", "mov", "m4v"]
        }

        print("DEBUG: About to show file panel")
        if panel.runModal() == .OK, let url = panel.url {
            print("DEBUG: File selected: \(url.lastPathComponent)")

            if let gameScene = gvc.gameScene {
                print("DEBUG: GameScene exists, calling loadBackgroundVideo")
                gameScene.loadBackgroundVideo(url: url)
                gameScene.setSuperimposeEnabled(true)
                print("DEBUG: setSuperimposeEnabled(true) called")
            } else {
                print("DEBUG: ERROR - GameScene is nil!")
            }

            updateCRTMenuCheckmarks()
            print("DEBUG: Video loading and enable completed")
        } else {
            print("DEBUG: File panel cancelled or no URL")
        }
    }
    @objc func removeBackgroundVideo(_ sender: Any?) {
        gameViewController?.gameScene?.removeBackgroundVideo()
        updateCRTMenuCheckmarks()
    }
    @objc func toggleBackgroundVideo(_ sender: Any?) {
        guard let scene = gameViewController?.gameScene else { return }
        scene.setSuperimposeEnabled(!scene.isSuperimposeEnabled())
        scene.osdUpdateSuperimpose()
        updateCRTMenuCheckmarks()
    }
    @objc func adjustBGVideoThreshold(_ sender: Any?) {
        guard let scene = gameViewController?.gameScene else { return }
        let cur = scene.getSuperimposeThreshold()
        // Cycle 0.02 -> 0.05 -> 0.08 -> 0.02
        // Cycle finer: 2%,5%,8%,12%,16%,20%
        let next: Float = (cur < 0.03) ? 0.05 : (cur < 0.07) ? 0.08 : (cur < 0.11) ? 0.12 : (cur < 0.15) ? 0.16 : (cur < 0.19) ? 0.20 : 0.02
        scene.setSuperimposeThreshold(next)
        scene.osdUpdateSuperimpose()
        updateCRTMenuCheckmarks()
    }
    @objc func adjustBGVideoSoftness(_ sender: Any?) {
        guard let scene = gameViewController?.gameScene else { return }
        let cur = scene.getSuperimposeSoftness()
        // Cycle finer: 2%,6%,10%,14%,18%,20%
        let next: Float = (cur < 0.03) ? 0.06 : (cur < 0.09) ? 0.10 : (cur < 0.13) ? 0.14 : (cur < 0.17) ? 0.18 : 0.02
        scene.setSuperimposeSoftness(next)
        scene.osdUpdateSuperimpose()
        updateCRTMenuCheckmarks()
    }
    @objc func adjustBGVideoAlpha(_ sender: Any?) {
        guard let scene = gameViewController?.gameScene else { return }
        let cur = scene.getSuperimposeAlpha()
        let steps: [Float] = [0.25, 0.5, 0.75, 1.0]
        let next = steps.first(where: { $0 > cur + 0.01 }) ?? steps[0]
        scene.setSuperimposeAlpha(next)
        scene.osdUpdateSuperimpose()
        updateCRTMenuCheckmarks()
    }

    
    private func updateMenuTitles() {
        // Prevent recursive calls
        guard !isUpdatingMenuTitles else {
            logger.debug("Preventing recursive updateMenuTitles call")
            return
        }

        guard let mainMenu = NSApplication.shared.mainMenu else {
            logger.debug("Could not get main menu")
            return
        }

        isUpdatingMenuTitles = true
        defer { isUpdatingMenuTitles = false }
        
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
                    logger.debug("Updating Display menu - skipping mouse checkmark update to prevent infinite loop")
                    // Commented out to prevent infinite loop:
                    // self.updateMouseMenuCheckmark()
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
                if getCachedFDDReady(0) {
                    logger.debug("Drive 0 is ready")
                    if let filename = getCachedFDDFilename(0) {
                        let name = filename
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
                if getCachedFDDReady(0) {
                    if let filename = getCachedFDDFilename(0) {
                        let name = filename
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
                if getCachedFDDReady(1) {
                    logger.debug("Drive 1 is ready")
                    if let filename = getCachedFDDFilename(1) {
                        let name = filename
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
                if getCachedFDDReady(1) {
                    if let filename = getCachedFDDFilename(1) {
                        let name = filename
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
                if getCachedHDDReady() {
                    logger.debug("HDD is ready")
                    if let filename = getCachedHDDFilename() {
                        let name = filename
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
                if getCachedHDDReady() {
                    if let filename = getCachedHDDFilename() {
                        let name = filename
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

    // Opt-in to secure state restoration coding on supported macOS versions
    @available(macOS 12.0, *)
    func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
        return true
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
            if let gvc = gameViewController, let scene = gvc.gameScene {
                scene.releaseAllMouseButtons()
                gvc.disableMouseCapture()
            } else {
                gameViewController?.disableMouseCapture()
            }
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

    // Helper functions for cached drive status to prevent infinite loops
    private func getCachedFDDReady(_ drive: Int) -> Bool {
        let now = Date().timeIntervalSince1970
        if now - lastDriveStatusCheck > driveStatusCheckInterval {
            cachedFDDReady[0] = X68000_IsFDDReady(0) != 0
            cachedFDDReady[1] = X68000_IsFDDReady(1) != 0
            cachedHDDReady = X68000_IsHDDReady() != 0
            lastDriveStatusCheck = now
        }
        return cachedFDDReady[drive] ?? false
    }

    private func getCachedFDDFilename(_ drive: Int) -> String? {
        let now = Date().timeIntervalSince1970
        if now - lastDriveStatusCheck > driveStatusCheckInterval {
            cachedFDDFilename[0] = X68000_GetFDDFilename(0) != nil ? String(cString: X68000_GetFDDFilename(0)!) : nil
            cachedFDDFilename[1] = X68000_GetFDDFilename(1) != nil ? String(cString: X68000_GetFDDFilename(1)!) : nil
            cachedHDDFilename = X68000_GetHDDFilename() != nil ? String(cString: X68000_GetHDDFilename()!) : nil
            lastDriveStatusCheck = now
        }
        return cachedFDDFilename[drive] ?? nil
    }

    private func getCachedHDDReady() -> Bool {
        let now = Date().timeIntervalSince1970
        if now - lastDriveStatusCheck > driveStatusCheckInterval {
            cachedFDDReady[0] = X68000_IsFDDReady(0) != 0
            cachedFDDReady[1] = X68000_IsFDDReady(1) != 0
            cachedHDDReady = X68000_IsHDDReady() != 0
            lastDriveStatusCheck = now
        }
        return cachedHDDReady
    }

    private func getCachedHDDFilename() -> String? {
        let now = Date().timeIntervalSince1970
        if now - lastDriveStatusCheck > driveStatusCheckInterval {
            cachedFDDFilename[0] = X68000_GetFDDFilename(0) != nil ? String(cString: X68000_GetFDDFilename(0)!) : nil
            cachedFDDFilename[1] = X68000_GetFDDFilename(1) != nil ? String(cString: X68000_GetFDDFilename(1)!) : nil
            cachedHDDFilename = X68000_GetHDDFilename() != nil ? String(cString: X68000_GetHDDFilename()!) : nil
            lastDriveStatusCheck = now
        }
        return cachedHDDFilename
    }

    private func getCachedSCCCompatMode() -> Int32 {
        let now = Date().timeIntervalSince1970
        if now - lastSCCCompatCheck > sccCompatCheckInterval {
            cachedSCCCompatMode = SCC_GetCompatMode()
            lastSCCCompatCheck = now
        }
        return cachedSCCCompatMode
    }

    // MARK: - Serial Communication Settings Actions
    @objc private func setSerialMouseOnly(_ sender: Any) {
        infoLog("Setting serial mode to Mouse Only", category: .ui)
        _ = gameViewController?.sccManager.setMouseOnlyMode()
        updateSerialMenuCheckmarks()
    }
    
    @objc private func setSerialPTY(_ sender: Any) {
        infoLog("Setting serial mode to PTY", category: .ui)
        if gameViewController?.sccManager.createPTY() == true {
            // Show PTY path information
            if let slavePath = gameViewController?.sccManager.getPTYSlavePath(),
               let screenCommand = gameViewController?.sccManager.getScreenCommand() {
                
                let alert = NSAlert()
                alert.messageText = "PTY Created Successfully"
                alert.informativeText = "PTY slave path: \(slavePath)\n\nTo connect from terminal, use:\n\(screenCommand)"
                alert.alertStyle = .informational
                alert.addButton(withTitle: "OK")
                alert.runModal()
            }
        } else {
            let alert = NSAlert()
            alert.messageText = "PTY Creation Failed"
            alert.informativeText = "Could not create PTY for serial communication."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
        }
        updateSerialMenuCheckmarks()
    }
    
    @objc private func setSerialTCP(_ sender: Any) {
        // Show TCP configuration dialog
        let alert = NSAlert()
        alert.messageText = "TCP Serial Connection"
        alert.informativeText = "Enter host and port for TCP serial connection:"
        alert.alertStyle = .informational
        alert.addButton(withTitle: "Connect")
        alert.addButton(withTitle: "Cancel")
        
        let hostField = NSTextField(frame: NSRect(x: 0, y: 28, width: 200, height: 24))
        hostField.stringValue = "localhost"
        hostField.placeholderString = "Host (e.g., localhost)"
        
        let portField = NSTextField(frame: NSRect(x: 0, y: 0, width: 200, height: 24))
        portField.stringValue = "23"
        portField.placeholderString = "Port (e.g., 23)"
        
        let accessoryView = NSView(frame: NSRect(x: 0, y: 0, width: 200, height: 52))
        accessoryView.addSubview(hostField)
        accessoryView.addSubview(portField)
        alert.accessoryView = accessoryView
        
        let response = alert.runModal()
        if response == .alertFirstButtonReturn {
            let host = hostField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
            if let port = Int(portField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)),
               !host.isEmpty, port > 0, port <= 65535 {
                
                infoLog("Setting serial mode to TCP: \(host):\(port)", category: .ui)
                if gameViewController?.sccManager.connectTCP(host: host, port: port) == true {
                    let successAlert = NSAlert()
                    successAlert.messageText = "TCP Connection Established"
                    successAlert.informativeText = "Successfully connected to \(host):\(port)\nSerial communication is now active."
                    successAlert.alertStyle = .informational
                    successAlert.addButton(withTitle: "OK")
                    successAlert.runModal()
                } else {
                    let errorAlert = NSAlert()
                    errorAlert.messageText = "TCP Connection Failed"
                    errorAlert.informativeText = "Could not connect to \(host):\(port). Please check the host and port settings."
                    errorAlert.alertStyle = .warning
                    errorAlert.addButton(withTitle: "OK")
                    errorAlert.runModal()
                }
            } else {
                let errorAlert = NSAlert()
                errorAlert.messageText = "Invalid Input"
                errorAlert.informativeText = "Please enter a valid host name and port number (1-65535)."
                errorAlert.alertStyle = .warning
                errorAlert.addButton(withTitle: "OK")
                errorAlert.runModal()
            }
        }
        updateSerialMenuCheckmarks()
    }

    @objc private func toggleSCCCompatMode(_ sender: Any) {
        // Toggle original px68k SCC mouse behavior
        let enabled = getCachedSCCCompatMode() != 0
        SCC_SetCompatMode(enabled ? 0 : 1)

        // Force cache update immediately after setting
        cachedSCCCompatMode = SCC_GetCompatMode()
        lastSCCCompatCheck = Date().timeIntervalSince1970

        // Update mouse controller cache to avoid infinite loops
        gameViewController?.gameScene?.mouseController?.updateSCCCompatModeCache()

        infoLog("SCC Mouse Compat Mode: \(enabled ? "OFF" : "ON")", category: .ui)
        updateSerialMenuCheckmarks()
    }
    
    @objc private func setSerialTCPServer(_ sender: Any) {
        // Show TCP Server configuration dialog
        let alert = NSAlert()
        alert.messageText = "TCP Serial Server"
        alert.informativeText = "Enter port for TCP server (emulator will listen for connections):"
        alert.alertStyle = .informational
        alert.addButton(withTitle: "Start Server")
        alert.addButton(withTitle: "Cancel")
        
        let portField = NSTextField(frame: NSRect(x: 0, y: 0, width: 200, height: 24))
        portField.stringValue = "54321"
        portField.placeholderString = "Port (e.g., 54321)"
        
        let accessoryView = NSView(frame: NSRect(x: 0, y: 0, width: 200, height: 24))
        accessoryView.addSubview(portField)
        alert.accessoryView = accessoryView
        
        let response = alert.runModal()
        if response == .alertFirstButtonReturn {
            if let port = Int(portField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)),
               port > 0, port <= 65535 {
                
                infoLog("Starting TCP server on port: \(port)", category: .ui)
                if gameViewController?.sccManager.startTCPServer(port: port) == true {
                    let successAlert = NSAlert()
                    successAlert.messageText = "TCP Server Started"
                    successAlert.informativeText = "Server is listening on port \(port)\n\nTo connect from terminal:\ntelnet localhost \(port)\n\nOr from another host:\ntelnet <emulator-ip> \(port)"
                    successAlert.alertStyle = .informational
                    successAlert.addButton(withTitle: "OK")
                    successAlert.runModal()
                } else {
                    let errorAlert = NSAlert()
                    errorAlert.messageText = "TCP Server Failed"
                    errorAlert.informativeText = "Could not start TCP server on port \(port). Port may be in use."
                    errorAlert.alertStyle = .warning
                    errorAlert.addButton(withTitle: "OK")
                    errorAlert.runModal()
                }
            } else {
                let errorAlert = NSAlert()
                errorAlert.messageText = "Invalid Port"
                errorAlert.informativeText = "Please enter a valid port number (1-65535)."
                errorAlert.alertStyle = .warning
                errorAlert.addButton(withTitle: "OK")
                errorAlert.runModal()
            }
        }
        updateSerialMenuCheckmarks()
    }
    
    @objc private func disconnectSerial(_ sender: Any) {
        infoLog("Disconnecting serial communication", category: .ui)
        gameViewController?.sccManager.disconnect()
        updateSerialMenuCheckmarks()
    }
    
    private func updateSerialMenuCheckmarks() {
        // Prevent recursive calls
        guard !isUpdatingSerialMenuCheckmarks else {
            logger.debug("Preventing recursive updateSerialMenuCheckmarks call")
            return
        }

        guard let mainMenu = NSApplication.shared.mainMenu else { return }

        isUpdatingSerialMenuCheckmarks = true
        defer { isUpdatingSerialMenuCheckmarks = false }
        
        let currentMode = gameViewController?.sccManager.currentMode ?? .mouseOnly
        
        // Find and update Serial menu items
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu {
                for subMenuItem in submenu.items {
                    if let subSubmenu = subMenuItem.submenu,
                       subMenuItem.title == "Serial Communication" {
                        for serialItem in subSubmenu.items {
                            switch serialItem.identifier?.rawValue {
                            case "Serial-mouseOnly":
                                serialItem.state = (currentMode == .mouseOnly) ? .on : .off
                            case "Serial-PTY":
                                serialItem.state = (currentMode == .serialPTY) ? .on : .off
                            case "Serial-TCP":
                                serialItem.state = (currentMode == .serialTCP) ? .on : .off
                            case "Serial-TCPServer":
                                serialItem.state = (currentMode == .serialTCPServer) ? .on : .off
                            case "Serial-CompatMouse":
                                serialItem.state = (getCachedSCCCompatMode() != 0) ? .on : .off
                            default:
                                break
                            }
                        }
                    }
                }
            }
        }
    }
    
    // MARK: - JoyportU Settings Actions
    @objc private func setJoyportUDisabled(_ sender: Any) {
        infoLog("Setting JoyportU mode to Disabled", category: .ui)
        gameViewController?.setJoyportUMode(0)
        updateJoyportUMenuCheckmarks()
    }

    func applicationWillFinishLaunching(_ notification: Notification) {
        // Build menu as early as possible to avoid transient storyboard inconsistencies.
        rebuildMenuSystem()
    }
    
    @objc private func setJoyportUNotifyMode(_ sender: Any) {
        infoLog("Setting JoyportU mode to Notify Mode", category: .ui)
        gameViewController?.setJoyportUMode(1)
        updateJoyportUMenuCheckmarks()
    }
    
    @objc private func setJoyportUCommandMode(_ sender: Any) {
        infoLog("Setting JoyportU mode to Command Mode", category: .ui)
        gameViewController?.setJoyportUMode(2)
        updateJoyportUMenuCheckmarks()
    }
    
    private func updateJoyportUMenuCheckmarks() {
        guard let mainMenu = NSApplication.shared.mainMenu else { return }
        
        let currentMode = gameViewController?.getJoyportUMode() ?? 0
        
        // Find and update JoyportU menu items
        for menuItem in mainMenu.items {
            if let submenu = menuItem.submenu {
                for subMenuItem in submenu.items {
                    if let subSubmenu = subMenuItem.submenu,
                       subMenuItem.title == "JoyportU Settings" {
                        for joyportUItem in subSubmenu.items {
                            switch joyportUItem.identifier?.rawValue {
                            case "JoyportU-disabled":
                                joyportUItem.state = (currentMode == 0) ? .on : .off
                            case "JoyportU-notifyMode":
                                joyportUItem.state = (currentMode == 1) ? .on : .off
                            case "JoyportU-commandMode":
                                joyportUItem.state = (currentMode == 2) ? .on : .off
                            default:
                                break
                            }
                        }
                    }
                }
            }
        }
    }
    
    // MARK: - Menu Validation
    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        // Avoid touching menu state until the main menu is fully attached.
        if NSApplication.shared.mainMenu == nil || (NSApplication.shared.mainMenu?.items.isEmpty ?? true) {
            return true
        }
        // Enable all menu items by default
        if menuItem.identifier?.rawValue == "Display-mouse-mode" || menuItem.title.contains("Use X68000 Mouse") {
            // Always enable the mouse toggle menu item
            menuItem.state = isMouseCaptureEnabled ? .on : .off
            debugLog("Validating mouse menu item - enabled: true, state: \(isMouseCaptureEnabled ? "ON" : "OFF")", category: .ui)
            return true
        } else if menuItem.menu?.title == "Clock" {
            // Reflect current clock selection via checkmark
            let currentMHz: Int = {
                if let s = UserDefaults.standard.string(forKey: "clock"), let v = Int(s) { return v }
                return 24
            }()
            let title = menuItem.title
            let targetMHz: Int? = (
                title.contains("1 MHz") ? 1 :
                title.contains("10 MHz") ? 10 :
                title.contains("16 MHz") ? 16 :
                title.contains("24 MHz") ? 24 :
                title.contains("40 MHz") ? 40 :
                title.contains("50 MHz") ? 50 :
                nil
            )
            if let mhz = targetMHz {
                menuItem.state = (mhz == currentMHz) ? .on : .off
            }
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
