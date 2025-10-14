//
//  GameViewController.swift
//  X68000 macOS
//
//  Created by GOROman on 2020/03/28.
//  Copyright © 2020 GOROman/Awed. All rights reserved.
//

import Cocoa
import SpriteKit
import GameplayKit
import UniformTypeIdentifiers

// MARK: - Serial Communication Support

public enum SCCMode: Int32 {
    case mouseOnly = 0
    case serialPTY = 1
    case serialTCP = 2
    case serialFile = 3
    case serialTCPServer = 4
}

// SCC Function declarations
@_silgen_name("SCC_Init") func SCC_Init()
@_silgen_name("SCC_SetMode") func SCC_SetMode(_ mode: Int32, _ config: UnsafePointer<CChar>?) -> Int32
@_silgen_name("SCC_CloseSerial") func SCC_CloseSerial()
@_silgen_name("SCC_GetSlavePath") func SCC_GetSlavePath() -> UnsafePointer<CChar>?

// MARK: - Custom SKView for Mouse Event Forwarding

class MouseCaptureSKView: SKView {
    
    weak var gameViewController: GameViewController?
    
    override var acceptsFirstResponder: Bool {
        return true
    }
    
    override func mouseDown(with event: NSEvent) {
        infoLog("🖱️ MouseCaptureSKView.mouseDown - forwarding to GameViewController", category: .input)
        gameViewController?.mouseDown(with: event)
        // Don't call super to prevent SKView from handling the event
    }
    
    override func mouseUp(with event: NSEvent) {
        infoLog("🖱️ MouseCaptureSKView.mouseUp - forwarding to GameViewController", category: .input)
        gameViewController?.mouseUp(with: event)
        // Don't call super to prevent SKView from handling the event
    }
    
    override func rightMouseDown(with event: NSEvent) {
        infoLog("🖱️ MouseCaptureSKView.rightMouseDown - forwarding to GameViewController", category: .input)
        gameViewController?.rightMouseDown(with: event)
        // Don't call super to prevent SKView from handling the event
    }
    
    override func rightMouseUp(with event: NSEvent) {
        infoLog("🖱️ MouseCaptureSKView.rightMouseUp - forwarding to GameViewController", category: .input)
        gameViewController?.rightMouseUp(with: event)
        // Don't call super to prevent SKView from handling the event
    }
    
    override func mouseMoved(with event: NSEvent) {
        gameViewController?.mouseMoved(with: event)
        // Don't call super to prevent SKView from handling the event
    }

    override func mouseDragged(with event: NSEvent) {
        gameViewController?.mouseDragged(with: event)
        // Don't call super to prevent SKView from handling the event
    }
    
    override func rightMouseDragged(with event: NSEvent) {
        gameViewController?.rightMouseDragged(with: event)
        // Don't call super to prevent SKView from handling the event
    }

    // Disable default contextual menu only while capture is active
    override func menu(for event: NSEvent) -> NSMenu? {
        if let gc = gameViewController,
           let mc = gc.gameScene?.mouseController,
           mc.isCaptureMode {
            return nil
        }
        return super.menu(for: event)
    }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
}

class GameViewController: NSViewController {
    
    static weak var shared: GameViewController?
    var gameScene: GameScene?
    
    // Serial Communication Manager wrapper
    internal var currentSerialMode: SCCMode = .mouseOnly
    internal var isSerialConnected: Bool = false
    
    // Mouse tracking for mouse capture mode
    private var mouseTrackingArea: NSTrackingArea?
    private var suppressNextMouseEvent: Bool = false
    private var discardNextDelta: Bool = false
    private var discardDeltaCount: Int = 0
    // Swallow initial noisy deltas after enabling capture
    private var captureSettleUntil: TimeInterval? = nil
    
    
    func load(_ url: URL) {
        // Reduced logging for performance
        // print("🐛 GameViewController.load() called with: \(url.lastPathComponent)")
        // print("🐛 Full path: \(url.path)")
        gameScene?.load(url: url)
    }
    
    // MARK: - FDD Management
    @IBAction func openFDDDriveA(_ sender: Any) {
        openFDDForDrive(0)
    }
    
    @IBAction func openFDDDriveB(_ sender: Any) {
        openFDDForDrive(1)
    }
    
    @IBAction func ejectFDDDriveA(_ sender: Any) {
        ejectFDDFromDrive(0)
    }
    
    @IBAction func ejectFDDDriveB(_ sender: Any) {
        ejectFDDFromDrive(1)
    }
    
    private func openFDDForDrive(_ drive: Int) {
        // Reduced logging for performance
        // print("🔧 Opening FDD dialog for Drive \(drive == 0 ? "A" : "B")")
        
        let openPanel = NSOpenPanel()
        openPanel.title = "Open FDD Image for Drive \(drive == 0 ? "0" : "1")"
        if #available(macOS 11.0, *) {
            var types: [UTType] = []
            if let dim = UTType(filenameExtension: "dim") { types.append(dim) }
            if let xdf = UTType(filenameExtension: "xdf") { types.append(xdf) }
            if let d88 = UTType(filenameExtension: "d88") { types.append(d88) }
            if types.isEmpty { types = [.data] }
            openPanel.allowedContentTypes = types
        } else {
            openPanel.allowedFileTypes = ["dim", "xdf", "d88"]
        }
        openPanel.allowsMultipleSelection = false
        openPanel.canChooseFiles = true
        openPanel.canChooseDirectories = false
        openPanel.treatsFilePackagesAsDirectories = false
        
        // Try to set default directory to user's actual Documents folder first
        var defaultDirectory: URL?
        
        // Priority 1: User's actual Documents/X68000 folder
        let userDocumentsX68000 = URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Documents/X68000")
        if FileManager.default.fileExists(atPath: userDocumentsX68000.path) {
            defaultDirectory = userDocumentsX68000
            // Reduced logging for performance
            // print("🔧 Using user Documents/X68000 as default: \(userDocumentsX68000.path)")
        }
        // Priority 2: User's Documents folder
        else if let userDocuments = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
            if FileManager.default.fileExists(atPath: userDocuments.path) {
                defaultDirectory = userDocuments
                // Reduced logging for performance
                // print("🔧 Using user Documents as default: \(userDocuments.path)")
            }
        }
        // Priority 3: Sandboxed Documents folder
        else if let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
            defaultDirectory = documentsURL
            // Reduced logging for performance
            // print("🔧 Using sandboxed Documents as default: \(documentsURL.path)")
        }
        
        if let defaultDir = defaultDirectory {
            openPanel.directoryURL = defaultDir
        }
        
        // Reduced logging for performance
        // print("🔧 NSOpenPanel configured, showing dialog...")
        
        openPanel.begin { [weak self] response in
            // Reduced logging for performance
            // print("🔧 NSOpenPanel response: \(response == .OK ? "OK" : "Cancel/Error")")
            if response == .OK, let url = openPanel.url {
                infoLog("NSOpenPanel selected file: \(url.path)", category: .fileSystem)
                let accessible = url.startAccessingSecurityScopedResource()
                // Reduced logging for performance
                // print("🔧 Security-scoped resource access: \(accessible)")
                
                DispatchQueue.main.async {
                    self?.gameScene?.loadFDDToDrive(url: url, drive: drive)
                    if accessible {
                        url.stopAccessingSecurityScopedResource()
                    }
                }
            } else {
                warningLog("NSOpenPanel cancelled or failed", category: .ui)
            }
        }
    }
    
    private func ejectFDDFromDrive(_ drive: Int) {
        gameScene?.ejectFDDFromDrive(drive)
    }
    
    // MARK: - HDD Management
    @IBAction func openHDD(_ sender: Any) {
        // debugLog("Opening HDD dialog", category: .ui)
        
        let openPanel = NSOpenPanel()
        openPanel.title = "Open Hard Disk Image"
        
        // Create UTTypes for HDF file extension with fallback
        var allowedTypes: [UTType] = []
        
        if let hdfType = UTType(filenameExtension: "hdf") {
            allowedTypes.append(hdfType)
            // debugLog("Added HDF UTType successfully", category: .fileSystem)
        } else {
            warningLog("Failed to create HDF UTType, using fallback", category: .fileSystem)
            // Fallback for unknown extensions - use exported type or data type
            let exportedType = UTType(exportedAs: "NANKIN.X68000.1.HDD")
            allowedTypes.append(exportedType)
        }
        
        
        // Add generic data type as additional fallback
        allowedTypes.append(.data)
        
        // For older macOS versions, also set allowedFileTypes as fallback
        if #available(macOS 11.0, *) {
            openPanel.allowedContentTypes = allowedTypes
        } else {
            openPanel.allowedFileTypes = ["hdf"]
            // debugLog("Using legacy allowedFileTypes for older macOS", category: .ui)
        }
        
        // Allow all files as emergency fallback (user can still filter manually)
        openPanel.allowsOtherFileTypes = true
        
        // debugLog("HDD NSOpenPanel configured with \(allowedTypes.count) content types, allowsOtherFileTypes: true", category: .ui)
        openPanel.allowsMultipleSelection = false
        openPanel.canChooseFiles = true
        openPanel.canChooseDirectories = false
        openPanel.treatsFilePackagesAsDirectories = false
        
        // Set default directory using same logic as FDD
        var defaultDirectory: URL?
        
        // Priority 1: User's actual Documents/X68000 folder
        let userDocumentsX68000 = URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Documents/X68000")
        if FileManager.default.fileExists(atPath: userDocumentsX68000.path) {
            defaultDirectory = userDocumentsX68000
            // Reduced logging for performance
            // print("🔧 Using user Documents/X68000 as default: \(userDocumentsX68000.path)")
        }
        // Priority 2: User's Documents folder
        else if let userDocuments = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
            if FileManager.default.fileExists(atPath: userDocuments.path) {
                defaultDirectory = userDocuments
                // Reduced logging for performance
                // print("🔧 Using user Documents as default: \(userDocuments.path)")
            }
        }
        // Priority 3: Sandboxed Documents folder
        else if let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
            defaultDirectory = documentsURL
            // Reduced logging for performance
            // print("🔧 Using sandboxed Documents as default: \(documentsURL.path)")
        }
        
        if let defaultDir = defaultDirectory {
            openPanel.directoryURL = defaultDir
        }
        
        // debugLog("NSOpenPanel configured for HDD, showing dialog...", category: .ui)
        
        openPanel.begin { [weak self] response in
            // debugLog("HDD NSOpenPanel response: \(response == .OK ? "OK" : "Cancel/Error")", category: .ui)
            if response == .OK, let url = openPanel.url {
                infoLog("NSOpenPanel selected HDD file: \(url.path)", category: .fileSystem)
                let accessible = url.startAccessingSecurityScopedResource()
                // debugLog("HDD Security-scoped resource access: \(accessible)", category: .fileSystem)
                
                DispatchQueue.main.async {
                    self?.gameScene?.loadHDD(url: url)
                    if accessible {
                        url.stopAccessingSecurityScopedResource()
                    }
                }
            } else {
                warningLog("HDD NSOpenPanel cancelled or failed", category: .ui)
            }
        }
    }
    
    @IBAction func ejectHDD(_ sender: Any) {
        gameScene?.ejectHDD()
    }
    
    @IBAction func createEmptyHDD(_ sender: Any) {
        // debugLog("Creating empty HDD dialog", category: .ui)
        
        // Step 1: Show size selection alert
        let alert = NSAlert()
        alert.messageText = "Create Empty Hard Disk"
        alert.informativeText = "Select the size for the new hard disk image:"
        alert.alertStyle = .informational
        
        // Add size options (FORMAT.X supports up to 40MB)
        alert.addButton(withTitle: "10 MB")
        alert.addButton(withTitle: "20 MB")
        alert.addButton(withTitle: "40 MB")
        alert.addButton(withTitle: "Cancel")
        
        let response = alert.runModal()
        
        // Check for Cancel button (fourth button)
        if response.rawValue == NSApplication.ModalResponse.alertFirstButtonReturn.rawValue + 3 {
            // debugLog("HDD creation cancelled", category: .ui)
            return
        }
        
        // Determine size based on button selection
        let sizeInMB: Int
        let sizeInBytes: Int
        
        switch response.rawValue {
        case NSApplication.ModalResponse.alertFirstButtonReturn.rawValue:   // 10 MB
            sizeInMB = 10
            sizeInBytes = 10 * 1024 * 1024
        case NSApplication.ModalResponse.alertSecondButtonReturn.rawValue:  // 20 MB
            sizeInMB = 20
            sizeInBytes = 20 * 1024 * 1024
        case NSApplication.ModalResponse.alertThirdButtonReturn.rawValue:   // 40 MB
            sizeInMB = 40
            sizeInBytes = 40 * 1024 * 1024
        default:
            errorLog("Unexpected button response", category: .ui)
            return
        }
        
        infoLog("Selected HDD size: \(sizeInMB) MB (\(sizeInBytes) bytes)", category: .fileSystem)
        
        // Step 2: Show save dialog
        let savePanel = NSSavePanel()
        savePanel.title = "Create Hard Disk Image"
        savePanel.allowedContentTypes = [UTType(filenameExtension: "hdf") ?? .data]
        savePanel.nameFieldStringValue = "NewHDD_\(sizeInMB)MB.hdf"
        
        // Set default directory - same logic as openHDD
        var defaultDirectory: URL?
        
        let userDocumentsX68000 = URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Documents/X68000")
        if FileManager.default.fileExists(atPath: userDocumentsX68000.path) {
            defaultDirectory = userDocumentsX68000
        } else {
            let userDocuments = URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Documents")
            if FileManager.default.fileExists(atPath: userDocuments.path) {
                defaultDirectory = userDocuments
            }
        }
        
        if let defaultDir = defaultDirectory {
            savePanel.directoryURL = defaultDir
            // debugLog("Set HDD creation default directory to: \(defaultDir.path)", category: .fileSystem)
        }
        
        savePanel.begin { response in
            guard response == .OK, let url = savePanel.url else {
                warningLog("HDD creation save dialog cancelled", category: .ui)
                return
            }
            
            infoLog("Creating HDD at: \(url.path)", category: .fileSystem)
            
            // Step 3: Create the empty HDD file
            self.gameScene?.createEmptyHDD(at: url, sizeInBytes: sizeInBytes)
        }
    }
    
    // MARK: - Screen Rotation Management
    @IBAction func rotateScreen(_ sender: Any) {
        gameScene?.rotateScreen()
    }
    
    @IBAction func setLandscapeMode(_ sender: Any) {
        gameScene?.setScreenRotation(.landscape)
    }
    
    @IBAction func setPortraitMode(_ sender: Any) {
        gameScene?.setScreenRotation(.portrait)
    }
    
    // MARK: - Clock Management
    @IBAction func setClock1MHz(_ sender: Any) {
        gameScene?.setCPUClock(1)
    }
    
    @IBAction func setClock10MHz(_ sender: Any) {
        gameScene?.setCPUClock(10)
    }
    
    @IBAction func setClock16MHz(_ sender: Any) {
        gameScene?.setCPUClock(16)
    }
    
    @IBAction func setClock24MHz(_ sender: Any) {
        gameScene?.setCPUClock(24)
    }
    
    @IBAction func setClock40MHz(_ sender: Any) {
        gameScene?.setCPUClock(40)
    }
    
    @IBAction func setClock50MHz(_ sender: Any) {
        gameScene?.setCPUClock(50)
    }
    
    // MARK: - System Management
    @IBAction func resetSystem(_ sender: Any) {
        gameScene?.resetSystem()
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        
        // 静的参照を設定
        GameViewController.shared = self
        // debugLog("GameViewController.shared set in viewDidLoad", category: .ui)
        
        // Initialize SCC for serial communication
        SCC_Init()
        
        gameScene = GameScene.newGameScene()
        
        // Configure SKView settings BEFORE presenting scene
        let skView = self.view as! MouseCaptureSKView
        skView.gameViewController = self  // Set reference for event forwarding
        
        // Critical: Set frame synchronization settings first
        skView.preferredFramesPerSecond = 60
        skView.isAsynchronous = false  // Essential for consistent timing
        skView.ignoresSiblingOrder = true
        
        // Present the scene AFTER configuration
        skView.presentScene(gameScene)
        
        skView.showsFPS = true
        skView.showsNodeCount = true
        skView.showsDrawCount = true
        
        // Enable keyboard and mouse input for the SKView
        view.window?.makeFirstResponder(self.view)
        
        // Ensure the view accepts mouse events
        view.wantsLayer = true
        
        // ドラッグ&ドロップを有効にする
        setupDragAndDrop()
        
        // 画面回転変更の通知を監視
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(screenRotationChanged(_:)),
            name: .screenRotationChanged,
            object: nil
        )
        
        // アプリ起動時は常に横長ウィンドウサイズでスタート
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
            self.forceInitialLandscapeWindow()
        }
        
        // Set up window delegate for close events
        DispatchQueue.main.async {
            if let window = self.view.window {
                window.delegate = self
                // debugLog("Window delegate set for close event handling", category: .ui)
            }
        }
    }
    
    private func setupDragAndDrop() {
        view.registerForDraggedTypes([.fileURL])
    }
    
    // MARK: - First Responder and Keyboard Handling
    override var acceptsFirstResponder: Bool {
        return true
    }
    
    override func viewDidAppear() {
        super.viewDidAppear()
        // Ensure the view becomes first responder to receive keyboard and mouse events
        view.window?.makeFirstResponder(self)
        infoLog("GameViewController became first responder", category: .input)
    }
    
    override func keyDown(with event: NSEvent) {
        // Forward keyboard events to the game scene
        gameScene?.keyDown(with: event)
    }
    
    override func keyUp(with event: NSEvent) {
        // Forward keyboard events to the game scene
        gameScene?.keyUp(with: event)
    }
    
    @objc private func screenRotationChanged(_ notification: Notification) {
        guard let rotation = notification.object as? GameScene.ScreenRotation else { return }
        
        // debugLog("Screen rotation changed to: \(rotation.displayName)", category: .ui)
        
        // ウィンドウサイズを回転に応じて調整
        DispatchQueue.main.async {
            self.adjustWindowSizeForRotation(rotation)
        }
    }
    
    private func adjustWindowSizeForSavedRotation() {
        let userDefaults = UserDefaults.standard
        let isPortrait = userDefaults.bool(forKey: "ScreenRotation_Portrait")
        let savedRotation: GameScene.ScreenRotation = isPortrait ? .portrait : .landscape
        
        // debugLog("Adjusting window size for saved rotation: \(savedRotation.displayName)", category: .ui)
        adjustWindowSizeForRotation(savedRotation)
    }
    
    private func forceInitialLandscapeWindow() {
        // debugLog("Forcing initial landscape window size", category: .ui)
        adjustWindowSizeForRotation(.landscape)
    }
    
    private func adjustWindowSizeForRotation(_ rotation: GameScene.ScreenRotation) {
        guard let window = view.window else { return }
        
        _ = window.frame  // Get current frame for potential future use
        
        // 基本的なX68000解像度（768x512）に基づいて計算
        let baseWidth: CGFloat = 768
        let baseHeight: CGFloat = 512
        
        // 固定的な最適サイズを計算（現在のウィンドウサイズに依存しない）
        let optimalScale: CGFloat = 1.5  // 適度なサイズ倍率
        
        let newContentSize: NSSize
        
        switch rotation {
        case .landscape:
            // 横画面：通常のアスペクト比
            newContentSize = NSSize(
                width: baseWidth * optimalScale,
                height: baseHeight * optimalScale
            )
            
        case .portrait:
            // 縦画面：アスペクト比を反転し、縦長ウィンドウに
            newContentSize = NSSize(
                width: baseHeight * optimalScale,  // 元の高さが新しい幅
                height: baseWidth * optimalScale   // 元の幅が新しい高さ
            )
        }
        
        // ウィンドウの新しい位置を計算（中央に配置）
        let screenFrame = window.screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1920, height: 1080)
        let newX = screenFrame.midX - newContentSize.width / 2
        let newY = screenFrame.midY - newContentSize.height / 2
        
        let newFrame = window.frameRect(forContentRect: NSRect(
            x: newX,
            y: newY,
            width: newContentSize.width,
            height: newContentSize.height
        ))
        
        window.setFrame(newFrame, display: true, animate: false)
        
        debugLog("Window size adjusted for \(rotation.displayName): \(newContentSize)", category: .ui)
    }
    
    deinit {
        infoLog("GameViewController.deinit - final save", category: .ui)
        // Final save attempt
        gameScene?.saveHDD()
        gameScene?.fileSystem?.saveSRAM()
    }

}

// MARK: - Window Delegate Support
extension GameViewController: NSWindowDelegate {
    
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        infoLog("Window is about to close - saving all data", category: .ui)
        
        // Save HDD and SRAM before window closes
        gameScene?.saveHDD()
        gameScene?.fileSystem?.saveSRAM()
        
        return true
    }
    
    func windowWillClose(_ notification: Notification) {
        infoLog("Window will close - final save attempt", category: .ui)
        
        // Final save attempt
        gameScene?.saveHDD()
        gameScene?.fileSystem?.saveSRAM()
    }
}

// MARK: - Drag and Drop Support
extension GameViewController: NSDraggingDestination {
    
    func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        guard let types = sender.draggingPasteboard.types, types.contains(.fileURL) else {
            return []
        }
        
        // ファイルがサポートされた形式かチェック
        if let urls = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self]) as? [URL] {
            for url in urls {
                let ext = url.pathExtension.lowercased()
                if ["dim", "xdf", "d88", "hdm", "hdf"].contains(ext) {
                    return .copy
                }
            }
        }
        
        return []
    }
    
    func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        return draggingEntered(sender)
    }
    
    func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        debugLog("Drag and drop operation started", category: .ui)
        guard let urls = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self]) as? [URL] else {
            warningLog("No URLs found in drag operation", category: .ui)
            return false
        }
        
        var validUrls: [URL] = []
        for url in urls {
            debugLog("Dropped file: \(url.lastPathComponent)", category: .fileSystem)
            let ext = url.pathExtension.lowercased()
            if ["dim", "xdf", "d88", "hdm", "hdf"].contains(ext) {
                validUrls.append(url)
            }
        }
        
        if validUrls.isEmpty {
            warningLog("No valid disk image files found in drop", category: .fileSystem)
            return false
        }
        
        // If multiple floppy disk files, try to load them to separate drives
        let floppyUrls = validUrls.filter { ["dim", "xdf", "d88"].contains($0.pathExtension.lowercased()) }
        let hddUrls = validUrls.filter { ["hdm", "hdf"].contains($0.pathExtension.lowercased()) }
        
        if floppyUrls.count >= 2 {
            // Load first two floppy disks to drives A and B
            gameScene?.loadFDDToDrive(url: floppyUrls[0], drive: 0)
            gameScene?.loadFDDToDrive(url: floppyUrls[1], drive: 1)
            infoLog("Loaded \(floppyUrls[0].lastPathComponent) to Drive 0 and \(floppyUrls[1].lastPathComponent) to Drive 1", category: .fileSystem)
        } else if floppyUrls.count == 1 {
            // Load single floppy to drive A
            gameScene?.loadFDDToDrive(url: floppyUrls[0], drive: 0)
            infoLog("Loaded \(floppyUrls[0].lastPathComponent) to Drive 0", category: .fileSystem)
        }
        
        // Load HDD images using existing method
        for hddUrl in hddUrls {
            load(hddUrl)
        }
        
        return true
    }
    
    func saveSRAM() {
        debugLog("GameViewController.saveSRAM() called", category: .fileSystem)
        gameScene?.fileSystem?.saveSRAM()
        
        // Also save HDD changes
        gameScene?.saveHDD()
    }
    
    override func viewWillDisappear() {
        super.viewWillDisappear()
        debugLog("GameViewController.viewWillDisappear - saving data before disappearing", category: .ui)
        
        // Save HDD changes when view is about to disappear
        gameScene?.saveHDD()
        gameScene?.fileSystem?.saveSRAM()
    }
    
    // MARK: - Mouse Capture Management
    
    func enableMouseCapture() {
        // debugLog("Enabling X68000 mouse capture", category: .input)
        
        // Route events via SKView for consistent forwarding
        view.window?.makeFirstResponder(self.view)
        infoLog("MouseCaptureSKView made first responder for mouse capture", category: .input)
        
        // Hide the macOS cursor (AppKit only)
        NSCursor.hide()
        // Ensure we receive mouseMoved events even when not key only
        view.window?.acceptsMouseMovedEvents = true
        // Decouple OS cursor for true capture (prevent host edge clamp/click-through)
        CGAssociateMouseAndMouseCursorPosition(Int32(0))
        discardNextDelta = true // Drop the first delta after enabling to avoid spikes
        discardDeltaCount = 0    // No extra discards; rely on single suppress
        captureSettleUntil = Date().timeIntervalSince1970 + 0.15 // swallow ~150ms of synthetic noise
        
        // Enable mouse capture mode in the game scene
        gameScene?.enableMouseCapture()
        
        // Add mouse tracking area to the view
        setupMouseTracking()

        // Do not warp OS cursor here

        // Do not warp on enable to avoid visual jump
        
        // Mouse controller will be initialized automatically in enableCaptureMode
        // No need for manual initialization here
        
        // No warping on enable to avoid macOS cursor reappearing outside window
        
        infoLog("X68000 mouse capture enabled - Mac cursor hidden", category: .input)
    }
    
    func disableMouseCapture() {
        // debugLog("Disabling X68000 mouse capture", category: .input)
        
        // Remove mouse tracking area
        removeMouseTracking()
        
        // Show the macOS cursor (AppKit only)
        NSCursor.unhide()
        view.window?.acceptsMouseMovedEvents = false
        // Re-attach OS cursor to hardware mouse
        CGAssociateMouseAndMouseCursorPosition(Int32(1))
        
        // Disable mouse capture mode in the game scene
        gameScene?.disableMouseCapture()
        captureSettleUntil = nil
        
        infoLog("X68000 mouse capture disabled - Mac cursor visible", category: .input)
    }
    
    private func setupMouseTracking() {
        removeMouseTracking() // Remove existing tracking area if any
        
        let trackingArea = NSTrackingArea(
            rect: view.bounds,
            options: [.activeInKeyWindow, .mouseMoved, .enabledDuringMouseDrag, .mouseEnteredAndExited, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        
        view.addTrackingArea(trackingArea)
        mouseTrackingArea = trackingArea
        // debugLog("Mouse tracking area added: \(view.bounds)", category: .input)
    }
    
    private func removeMouseTracking() {
        if let trackingArea = mouseTrackingArea {
            view.removeTrackingArea(trackingArea)
            mouseTrackingArea = nil
            // debugLog("Mouse tracking area removed", category: .input)
        }
    }
    
    // MARK: - Mouse Event Handling

    private func handleMouseEvent(_ event: NSEvent) {
        guard let gameScene = gameScene,
              let mouseController = gameScene.mouseController else { return }

        if mouseController.isCaptureMode {
            // Capture mode: use raw deltas and re-center each event
            if let until = captureSettleUntil, Date().timeIntervalSince1970 < until {
                // During settle window, just swallow events (no warping needed when decoupled)
                return
            } else if captureSettleUntil != nil {
                // Settle window elapsed
                captureSettleUntil = nil
            }
            // No synthetic events from warping when decoupled; no need to discard here
            // Invert Y here to cancel core inversion; matches prior good behavior
            mouseController.addDeltas(event.deltaX, -event.deltaY)
            // Reduced verbose per-event logging; no cursor warping when decoupled
        } else {
            // Non-capture: ignore macOS mouse movement entirely to avoid visual drift in emulator
            return
        }
    }

    override func mouseMoved(with event: NSEvent) {
        handleMouseEvent(event)
    }

    override func mouseDragged(with event: NSEvent) {
        handleMouseEvent(event)
    }

    override func rightMouseDragged(with event: NSEvent) {
        handleMouseEvent(event)
    }
    
    override func mouseDown(with event: NSEvent) {
        guard let gameScene = gameScene,
              let mouseController = gameScene.mouseController else { return }

        // 非キャプチャ時は先に座標更新（ダブルクリック時の位置ズレ防止）
        // 修正: マウスモードOFFでは座標を更新しない（視覚的なズレ防止）
        if mouseController.isCaptureMode {
            // Immediately reflect press in core for simplicity
            X68000_Mouse_Event(1, 1.0, 0.0)
            mouseController.Click(0, true)
        } else {
            mouseController.Click(0, true)
            mouseController.sendDirectUpdate()
        }
    }

    override func mouseUp(with event: NSEvent) {
        guard let gameScene = gameScene,
              let mouseController = gameScene.mouseController else { return }
        if mouseController.isCaptureMode {
            X68000_Mouse_Event(1, 0.0, 0.0)
            mouseController.Click(0, false)
        } else {
            mouseController.Click(0, false)
            mouseController.sendDirectUpdate()
        }
    }

    override func rightMouseDown(with event: NSEvent) {
        guard let gameScene = gameScene,
              let mouseController = gameScene.mouseController else { return }
        // In capture mode, cursor visibility is managed by enable/disableMouseCapture
        // 修正: マウスモードOFFでは座標を更新しない
        if mouseController.isCaptureMode {
            X68000_Mouse_Event(2, 1.0, 0.0)
            mouseController.Click(1, true)
        } else {
            mouseController.Click(1, true)
        }
    }

    override func rightMouseUp(with event: NSEvent) {
        guard let gameScene = gameScene,
              let mouseController = gameScene.mouseController else { return }
        // Always send right mouseUp for single click processing
        // 修正: 右クリックのmouseUpも常にシングルクリック処理として送信
        if mouseController.isCaptureMode {
            X68000_Mouse_Event(2, 0.0, 0.0)
            mouseController.Click(1, false)
        } else {
            mouseController.Click(1, false)
            mouseController.sendDirectUpdate()
        }
    }
    
    // MARK: - JoyportU Settings
    func setJoyportUMode(_ mode: Int) {
        infoLog("Setting JoyportU mode to \(mode)", category: .input)
        // Call C function to set PPI JoyportU mode
        PPI_SetJoyportUMode(Int32(mode))
    }
    
    func getJoyportUMode() -> Int {
        // Call C function to get PPI JoyportU mode
        return Int(PPI_GetJoyportUMode())
    }
    
    // MARK: - Serial Communication Management
    
    var sccManager: SCCManagerProxy {
        return SCCManagerProxy(gameViewController: self)
    }
    
    func setSerialMouseOnly() -> Bool {
        let result = SCC_SetMode(SCCMode.mouseOnly.rawValue, nil)
        if result == 0 {
            currentSerialMode = .mouseOnly
            isSerialConnected = false
            return true
        }
        return false
    }
    
    func createSerialPTY() -> Bool {
        let result = SCC_SetMode(SCCMode.serialPTY.rawValue, nil)
        if result == 0 {
            currentSerialMode = .serialPTY
            isSerialConnected = true
            return true
        }
        return false
    }
    
    func connectSerialTCP(host: String, port: Int) -> Bool {
        let config = "\(host):\(port)"
        let result = config.withCString { ptr in SCC_SetMode(SCCMode.serialTCP.rawValue, ptr) }
        if result == 0 {
            currentSerialMode = .serialTCP
            isSerialConnected = true
            return true
        }
        return false
    }
    
    func startSerialTCPServer(port: Int) -> Bool {
        let config = "\(port)"
        let result = config.withCString { ptr in SCC_SetMode(SCCMode.serialTCPServer.rawValue, ptr) }
        if result == 0 {
            currentSerialMode = .serialTCPServer
            isSerialConnected = true
            return true
        }
        return false
    }
    
    func disconnectSerial() {
        SCC_CloseSerial()
        currentSerialMode = .mouseOnly
        isSerialConnected = false
    }
    
    func getPTYSlavePath() -> String? {
        if let cPath = SCC_GetSlavePath() {
            return String(cString: cPath)
        }
        return nil
    }
    
    func getScreenCommand() -> String? {
        if let slavePath = getPTYSlavePath() {
            return "screen \(slavePath) 9600"
        }
        return nil
    }
}

// MARK: - SCCManager Proxy for AppDelegate compatibility

struct SCCManagerProxy {
    weak var gameViewController: GameViewController?
    
    var currentMode: SCCMode {
        return gameViewController?.currentSerialMode ?? .mouseOnly
    }
    
    func setMouseOnlyMode() -> Bool {
        return gameViewController?.setSerialMouseOnly() ?? false
    }
    
    func createPTY() -> Bool {
        return gameViewController?.createSerialPTY() ?? false
    }
    
    func connectTCP(host: String, port: Int) -> Bool {
        return gameViewController?.connectSerialTCP(host: host, port: port) ?? false
    }
    
    func startTCPServer(port: Int) -> Bool {
        return gameViewController?.startSerialTCPServer(port: port) ?? false
    }
    
    func disconnect() {
        gameViewController?.disconnectSerial()
    }
    
    func getPTYSlavePath() -> String? {
        return gameViewController?.getPTYSlavePath()
    }
    
    func getScreenCommand() -> String? {
        return gameViewController?.getScreenCommand()
    }
}
