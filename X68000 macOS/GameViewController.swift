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
        print("🐛 GameViewController.load() called with: \(url.lastPathComponent)")
        print("🐛 Full path: \(url.path)")
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
        let openPanel = NSOpenPanel()
        openPanel.title = "Open FDD Image for Drive \(drive == 0 ? "A" : "B")"
        openPanel.allowedContentTypes = [
            UTType(filenameExtension: "dim")!,
            UTType(filenameExtension: "xdf")!,
            UTType(filenameExtension: "d88")!
        ]
        openPanel.allowsMultipleSelection = false
        
        openPanel.begin { [weak self] response in
            if response == .OK, let url = openPanel.url {
                DispatchQueue.main.async {
                    self?.gameScene?.loadFDDToDrive(url: url, drive: drive)
                }
            }
        }
    }
    
    private func ejectFDDFromDrive(_ drive: Int) {
        gameScene?.ejectFDDFromDrive(drive)
    }
    
    // MARK: - HDD Management
    @IBAction func openHDD(_ sender: Any) {
        let openPanel = NSOpenPanel()
        openPanel.title = "Open Hard Disk Image"
        openPanel.allowedContentTypes = [
            UTType(filenameExtension: "hdf")!,
            UTType(filenameExtension: "hdm")!
        ]
        openPanel.allowsMultipleSelection = false
        
        openPanel.begin { [weak self] response in
            if response == .OK, let url = openPanel.url {
                DispatchQueue.main.async {
                    self?.gameScene?.loadHDD(url: url)
                }
            }
        }
    }
    
    @IBAction func ejectHDD(_ sender: Any) {
        gameScene?.ejectHDD()
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
    
    @IBAction func setClock50MHz(_ sender: Any) {
        gameScene?.setCPUClock(50)
    }
    
    @IBAction func setClock100MHz(_ sender: Any) {
        gameScene?.setCPUClock(100)
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
    }
    
    private func setupDragAndDrop() {
        view.registerForDraggedTypes([.fileURL])
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
        
        let currentFrame = window.frame
        
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
    }
}

