//
//  GameViewController.swift
//  X68000 macOS
//
//  Created by GOROman on 2020/03/28.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Cocoa
import SpriteKit
import GameplayKit
import UniformTypeIdentifiers

class GameViewController: NSViewController {
    
    static weak var shared: GameViewController?
    var gameScene: GameScene?
    
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
        openPanel.title = "Open FDD Image for Drive \(drive == 0 ? "A" : "B")"
        openPanel.allowedContentTypes = [
            UTType(filenameExtension: "dim")!,
            UTType(filenameExtension: "xdf")!,
            UTType(filenameExtension: "d88")!
        ]
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
        else if let userDocuments = FileManager.default.urls(for: .userDirectory, in: .localDomainMask).first?.appendingPathComponent("Documents") {
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
                print("✅ NSOpenPanel selected file: \(url.path)")
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
                print("❌ NSOpenPanel cancelled or failed")
            }
        }
    }
    
    private func ejectFDDFromDrive(_ drive: Int) {
        gameScene?.ejectFDDFromDrive(drive)
    }
    
    // MARK: - HDD Management
    @IBAction func openHDD(_ sender: Any) {
        print("🔧 Opening HDD dialog")
        
        let openPanel = NSOpenPanel()
        openPanel.title = "Open Hard Disk Image"
        
        // Create UTTypes for HDF file extension with fallback
        var allowedTypes: [UTType] = []
        
        if let hdfType = UTType(filenameExtension: "hdf") {
            allowedTypes.append(hdfType)
            print("🔧 Added HDF UTType successfully")
        } else {
            print("⚠️  Failed to create HDF UTType, using fallback")
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
            print("🔧 Using legacy allowedFileTypes for older macOS")
        }
        
        // Allow all files as emergency fallback (user can still filter manually)
        openPanel.allowsOtherFileTypes = true
        
        print("🔧 HDD NSOpenPanel configured with \(allowedTypes.count) content types, allowsOtherFileTypes: true")
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
        else if let userDocuments = FileManager.default.urls(for: .userDirectory, in: .localDomainMask).first?.appendingPathComponent("Documents") {
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
        
        print("🔧 NSOpenPanel configured for HDD, showing dialog...")
        
        openPanel.begin { [weak self] response in
            print("🔧 HDD NSOpenPanel response: \(response == .OK ? "OK" : "Cancel/Error")")
            if response == .OK, let url = openPanel.url {
                print("✅ NSOpenPanel selected HDD file: \(url.path)")
                let accessible = url.startAccessingSecurityScopedResource()
                print("🔧 HDD Security-scoped resource access: \(accessible)")
                
                DispatchQueue.main.async {
                    self?.gameScene?.loadHDD(url: url)
                    if accessible {
                        url.stopAccessingSecurityScopedResource()
                    }
                }
            } else {
                print("❌ HDD NSOpenPanel cancelled or failed")
            }
        }
    }
    
    @IBAction func ejectHDD(_ sender: Any) {
        gameScene?.ejectHDD()
    }
    
    @IBAction func createEmptyHDD(_ sender: Any) {
        print("🔧 Creating empty HDD dialog")
        
        // Step 1: Show size selection alert
        let alert = NSAlert()
        alert.messageText = "Create Empty Hard Disk"
        alert.informativeText = "Select the size for the new hard disk image:"
        alert.alertStyle = .informational
        
        // Add size options
        alert.addButton(withTitle: "10 MB")
        alert.addButton(withTitle: "20 MB") 
        alert.addButton(withTitle: "40 MB")
        alert.addButton(withTitle: "80 MB")
        alert.addButton(withTitle: "Cancel")
        
        let response = alert.runModal()
        
        // Check for Cancel button (fifth button)
        if response.rawValue == NSApplication.ModalResponse.alertFirstButtonReturn.rawValue + 4 {
            print("🔧 HDD creation cancelled")
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
        case NSApplication.ModalResponse.alertFirstButtonReturn.rawValue + 3:  // 80 MB (fourth button)
            sizeInMB = 80
            sizeInBytes = 80 * 1024 * 1024
        default:
            print("❌ Unexpected button response")
            return
        }
        
        print("🔧 Selected HDD size: \(sizeInMB) MB (\(sizeInBytes) bytes)")
        
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
            print("🔧 Set HDD creation default directory to: \(defaultDir.path)")
        }
        
        savePanel.begin { response in
            guard response == .OK, let url = savePanel.url else {
                print("❌ HDD creation save dialog cancelled")
                return
            }
            
            print("🔧 Creating HDD at: \(url.path)")
            
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
        print("🐛 GameViewController.shared set in viewDidLoad")
        
        gameScene = GameScene.newGameScene()
        
        // Present the scene
        let skView = self.view as! SKView
        skView.presentScene(gameScene)
        
        skView.ignoresSiblingOrder = true
        
        skView.showsFPS = true
        skView.showsNodeCount = true
        skView.showsDrawCount = true
        
        // Enable keyboard input for the SKView
        view.window?.makeFirstResponder(self.view)
        
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
                print("🐛 Window delegate set for close event handling")
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
        // Ensure the view becomes first responder to receive keyboard events
        view.window?.makeFirstResponder(self)
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
        
        print("🐛 Screen rotation changed to: \(rotation.displayName)")
        
        // ウィンドウサイズを回転に応じて調整
        DispatchQueue.main.async {
            self.adjustWindowSizeForRotation(rotation)
        }
    }
    
    private func adjustWindowSizeForSavedRotation() {
        let userDefaults = UserDefaults.standard
        let isPortrait = userDefaults.bool(forKey: "ScreenRotation_Portrait")
        let savedRotation: GameScene.ScreenRotation = isPortrait ? .portrait : .landscape
        
        print("🐛 Adjusting window size for saved rotation: \(savedRotation.displayName)")
        adjustWindowSizeForRotation(savedRotation)
    }
    
    private func forceInitialLandscapeWindow() {
        print("🐛 Forcing initial landscape window size")
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
        
        window.setFrame(newFrame, display: true, animate: true)
        
        print("🐛 Window size adjusted for \(rotation.displayName): \(newContentSize)")
    }
    
    deinit {
        print("🐛 GameViewController.deinit - final save")
        // Final save attempt
        gameScene?.saveHDD()
        gameScene?.fileSystem?.saveSRAM()
    }

}

// MARK: - Window Delegate Support
extension GameViewController: NSWindowDelegate {
    
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        print("🐛 Window is about to close - saving all data")
        
        // Save HDD and SRAM before window closes
        gameScene?.saveHDD()
        gameScene?.fileSystem?.saveSRAM()
        
        return true
    }
    
    func windowWillClose(_ notification: Notification) {
        print("🐛 Window will close - final save attempt")
        
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
        print("🐛 Drag and drop operation started")
        guard let urls = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self]) as? [URL] else {
            print("🐛 No URLs found in drag operation")
            return false
        }
        
        var validUrls: [URL] = []
        for url in urls {
            print("🐛 Dropped file: \(url.lastPathComponent)")
            let ext = url.pathExtension.lowercased()
            if ["dim", "xdf", "d88", "hdm", "hdf"].contains(ext) {
                validUrls.append(url)
            }
        }
        
        if validUrls.isEmpty {
            print("🐛 No valid disk image files found in drop")
            return false
        }
        
        // If multiple floppy disk files, try to load them to separate drives
        let floppyUrls = validUrls.filter { ["dim", "xdf", "d88"].contains($0.pathExtension.lowercased()) }
        let hddUrls = validUrls.filter { ["hdm", "hdf"].contains($0.pathExtension.lowercased()) }
        
        if floppyUrls.count >= 2 {
            // Load first two floppy disks to drives A and B
            gameScene?.loadFDDToDrive(url: floppyUrls[0], drive: 0)
            gameScene?.loadFDDToDrive(url: floppyUrls[1], drive: 1)
            print("🐛 Loaded \(floppyUrls[0].lastPathComponent) to Drive A and \(floppyUrls[1].lastPathComponent) to Drive B")
        } else if floppyUrls.count == 1 {
            // Load single floppy to drive A
            gameScene?.loadFDDToDrive(url: floppyUrls[0], drive: 0)
            print("🐛 Loaded \(floppyUrls[0].lastPathComponent) to Drive A")
        }
        
        // Load HDD images using existing method
        for hddUrl in hddUrls {
            load(hddUrl)
        }
        
        return true
    }
    
    func saveSRAM() {
        print("🐛 GameViewController.saveSRAM() called")
        gameScene?.fileSystem?.saveSRAM()
        
        // Also save HDD changes
        gameScene?.saveHDD()
    }
    
    override func viewWillDisappear() {
        super.viewWillDisappear()
        print("🐛 GameViewController.viewWillDisappear - saving data before disappearing")
        
        // Save HDD changes when view is about to disappear
        gameScene?.saveHDD()
        gameScene?.fileSystem?.saveSRAM()
    }
}

