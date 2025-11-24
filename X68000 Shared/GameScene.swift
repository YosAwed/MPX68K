//
//  GameScene.swift
//  X68000 Shared
//
//  Created by GOROman on 2020/03/28.
//  Copyright 2020 GOROman. All rights reserved.
//

import SpriteKit
import GameController
import Metal
import simd
import CoreImage
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

// Notification for screen rotation changes
extension Notification.Name {
    static let screenRotationChanged = Notification.Name("screenRotationChanged")
}

class GameScene: SKScene {
    
    private var clockMHz: Int = 24
    private var samplingRate: Int = 22050
    private var vsync: Bool = true
    
    // Input mode management
    enum InputMode {
        case keyboard
        case joycard
    }
    private var currentInputMode: InputMode = .keyboard
    private var inputModeButton: SKLabelNode?
    
    // Screen rotation management
    enum ScreenRotation: CaseIterable {
        case landscape  // 横画面（通常）
        case portrait   // 縦画面（90度回転）
        
        var angle: CGFloat {
            switch self {
            case .landscape: return 0
            case .portrait: return .pi / 2  // 90度回転
            }
        }
        
        var displayName: String {
            switch self {
            case .landscape: return "Landscape"
            case .portrait: return "Portrait (90°)"
            }
        }
    }
    private var currentRotation: ScreenRotation = .landscape
    
    // シフトキーの状態を追跡
    private var isShiftKeyPressed = false
    
    fileprivate var label: SKLabelNode?
    fileprivate var labelStatus: SKLabelNode?
    fileprivate var spinnyNode: SKShapeNode?
    var titleSprite: SKSpriteNode?
    var mouseSprite: SKSpriteNode?
    var spr: SKSpriteNode = SKSpriteNode()
    private var crtBloomNode: SKEffectNode?
    private var crtVignetteNode: SKEffectNode?
    private var crtCurvatureNode: SKEffectNode?
    private var crtEffectRoot: SKNode?
    private var scanlineOverlay: SKSpriteNode?
    private var noiseOverlay: SKSpriteNode?
    private var lastOverlaySize: CGSize = .zero
    private var chromaRedNode: SKEffectNode?
    private var chromaRedSprite: SKSpriteNode?
    private var chromaBlueNode: SKEffectNode?
    private var chromaBlueSprite: SKSpriteNode?
    private var chromaBaseNode: SKEffectNode?
    private var cornerVignetteOverlay: SKSpriteNode?
    private var cornerVignetteCrop: SKCropNode?
    private var cornerVignetteFill: SKSpriteNode?
    private var lastCornerVignetteSize: CGSize = .zero
    private var lastCornerVignetteIntensity: Float = -1.0
    private var persistenceNode: SKSpriteNode?
    private var lastFrameTexture: SKTexture?
    // Superimpose crop/mask (robust overlay path)
    private var superCrop: SKCropNode?
    private var superMask: SKSpriteNode?
    private var superMaskShader: SKShader?
    private var superimposeDetached: Bool = false
    private var lastLoggedSuperState: (Bool, Float, Float, Float) = (false, 0.0, 0.0, 0.0)
    var labelMIDI: SKLabelNode?
    var joycontroller: JoyController?
    var joycard: X68JoyCard?
    var screen_w: Float = 1336.0
    var screen_h: Float = 1024.0
    private var isEmulatorInitialized: Bool = false
    
    // Performance optimization: Pre-allocated texture for reuse
    private var preAllocatedTexture: SKTexture?
    private var lastScreenWidth: Int = 0
    private var lastScreenHeight: Int = 0
    
    // Fixed-step frame pacing for smooth emulation
    private var fixedStepAccumulator: Double = 0.0
    private var lastUpdateTime: TimeInterval = 0.0
    private let emulatorHz: Double = 55.45
    private let targetFrameTime: Double = 1.0 / 55.45
    
    // Remove manual frame rate control - let SpriteKit handle timing
    
    private var audioStream: AudioStream?
    var mouseController: X68MouseController?
    
    // HDD auto-save removed - direct writes implemented
    private var midiController: MIDIController = MIDIController()
    
    private var devices: [X68Device] = []
    var fileSystem: FileSystem?
    
    // Track currently loading files to prevent duplicate loads
    private static var currentlyLoadingFiles: Set<String> = []
    
    let moveJoystick = TLAnalogJoystick(withDiameter: 200)
    let rotateJoystick = TLAnalogJoystick(withDiameter: 120)
    let rotateJoystick2 = TLAnalogJoystick(withDiameter: 120)

    // CRT display filter manager
    private let crtFilter = CRTFilterManager()
    private var crtPreset: CRTPreset = .off
    var crtOverlay: CRTOverlay?
    // Superimpose (background video)
    private let superManager = SuperimposeManager()
    
    class func newGameScene() -> GameScene {
        
        func buttonHandler() -> GCControllerButtonValueChangedHandler {
            return { (_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
                debugLog("A!", category: .input) 
            }
        }
        
        guard let scene = GameScene(fileNamed: "GameScene") else {
            criticalLog("Failed to load GameScene.sks", category: .ui)
            abort()
        }
        
        scene.scaleMode = .aspectFit
        scene.backgroundColor = .black
        return scene
    }
    
    var count = 0
    
    func controller_event(status: JoyController.Status) {
        debugLog("Controller status: \(status)", category: .input)
        var msg = ""
        if status == .Connected {
            msg = "Controller Connected"
        } else if status == .Disconnected {
            msg = "Controller Disconnected"
        }
        if let t = self.labelStatus {
            t.text = msg
            // Animation disabled - just show text without fade animation
            t.alpha = 1.0
        }
    }

    // (Removed duplicate update override; using main update loop below)
    
    func load(url: URL) {
        // debugLog("GameScene.load() called with: \(url.lastPathComponent)", category: .fileSystem)
        let urlPath = url.path
        
        // Check if we're already loading this exact file
        if GameScene.currentlyLoadingFiles.contains(urlPath) {
            warningLog("Already loading file: \(url.lastPathComponent), skipping", category: .fileSystem)
            return
        }
        
        // Mark this file as loading immediately to prevent duplicates
        GameScene.currentlyLoadingFiles.insert(urlPath)
        
        Benchmark.measure("load", block: {
            // debugLog("Starting Benchmark.measure for file: \(url.lastPathComponent)", category: .emulation)
            let imagefilename = url.deletingPathExtension().lastPathComponent.removingPercentEncoding
            
            let node = SKLabelNode()
            node.fontSize = 64
            node.position.y = -350
            node.text = imagefilename
            node.zPosition = 4.0
            // Animation disabled - skip node creation entirely

            if self.fileSystem == nil {
                // debugLog("Creating new FileSystem instance", category: .fileSystem)
                self.fileSystem = FileSystem()
                self.fileSystem?.gameScene = self  // Set reference for cleanup
            }
            // debugLog("Calling FileSystem.loadDiskImage() with: \(url.lastPathComponent)", category: .fileSystem)
            self.fileSystem?.loadDiskImage(url)
        })
    }
    
    // Method to clear loading file after completion
    func clearLoadingFile(_ url: URL) {
        GameScene.currentlyLoadingFiles.remove(url.path)
    }
    
    // MARK: - FDD Management
    func loadFDDToDrive(url: URL, drive: Int) {
        // debugLog("GameScene.loadFDDToDrive() called with: \(url.lastPathComponent) to drive \(drive)", category: .fileSystem)
        
        if fileSystem == nil {
            // debugLog("Creating new FileSystem instance for FDD", category: .fileSystem)
            fileSystem = FileSystem()
            fileSystem?.gameScene = self
        }
        
        fileSystem?.loadFDDToDrive(url, drive: drive)
        
        // Update menu after FDD load
        #if os(macOS)
        if let appDelegate = NSApp.delegate as? AppDelegate {
            appDelegate.updateMenuOnFileOperation()
        }
        #endif
    }
    
    func ejectFDDFromDrive(_ drive: Int) {
        // debugLog("GameScene.ejectFDDFromDrive() called for drive \(drive)", category: .fileSystem)
        X68000_EjectFDD(drive)
        
        // Save current disk state after eject
        fileSystem?.saveCurrentDiskState()
        
        // Update menu after FDD eject
        #if os(macOS)
        if let appDelegate = NSApp.delegate as? AppDelegate {
            appDelegate.updateMenuOnFileOperation()
        }
        #endif
    }
    
    // MARK: - HDD Management
    func loadHDD(url: URL) {
        // debugLog("GameScene.loadHDD() called with: \(url.lastPathComponent)", category: .fileSystem)
        // debugLog("File extension: \(url.pathExtension)", category: .fileSystem)
        // debugLog("Full path: \(url.path)", category: .fileSystem)
        
        // Direct HDD loading to avoid complex FileSystem routing that may fail in TestFlight
        let extname = url.pathExtension.lowercased()
        
        // Check if this is a valid HDD file
        if extname == "hdf" {
            infoLog("Loading HDD file directly: \(extname.uppercased())", category: .fileSystem)
            
            do {
                let imageData = try Data(contentsOf: url)
                infoLog("Successfully read HDD data: \(imageData.count) bytes", category: .fileSystem)
                
                // Security: Validate HDD file size (reasonable limit for hard disk images)
                let maxSize = 2 * 1024 * 1024 * 1024 // 2GB max
                guard imageData.count <= maxSize else {
                    errorLog("HDD file too large: \(imageData.count) bytes", category: .fileSystem)
                    return
                }
                
                DispatchQueue.main.async {
                    if let p = X68000_GetDiskImageBufferPointer(4, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadHDD(url.path)  // Use url.path instead of url.absoluteString
                        infoLog("HDD loaded successfully: \(url.lastPathComponent)", category: .fileSystem)
                        
                        // Save SRAM after HDD loading
                        self.fileSystem?.saveSRAM()
                        
                        // Update menu after HDD load
                        #if os(macOS)
                        if let appDelegate = NSApp.delegate as? AppDelegate {
                            appDelegate.updateMenuOnFileOperation()
                        }
                        #endif
                    } else {
                        errorLog("Failed to get HDD buffer pointer", category: .fileSystem)
                    }
                }
            } catch {
                errorLog("Error reading HDD file", error: error, category: .fileSystem)
            }
        } else {
            errorLog("Invalid HDD file extension: \(extname)", category: .fileSystem)
            errorLog("Expected: hdf", category: .fileSystem)
        }
    }
    
    func ejectHDD() {
        // debugLog("GameScene.ejectHDD() called", category: .fileSystem)
        
        // Save HDD changes before ejecting
        if X68000_IsHDDReady() != 0 {
            infoLog("Saving HDD changes before ejecting...", category: .fileSystem)
            X68000_SaveHDD()
        }
        
        X68000_EjectHDD()
        
        // Save current disk state after eject
        fileSystem?.saveCurrentDiskState()
        
        // Update menu after HDD eject
        #if os(macOS)
        if let appDelegate = NSApp.delegate as? AppDelegate {
            appDelegate.updateMenuOnFileOperation()
        }
        #endif
    }
    
    func saveHDD() {
        infoLog("GameScene.saveHDD() called", category: .fileSystem)
        if X68000_IsHDDReady() != 0 {
            X68000_SaveHDD()
        } else {
            warningLog("No HDD loaded to save", category: .fileSystem)
        }
    }
    
    // Auto-save system removed - SASI writes directly to files
    
    func createEmptyHDD(at url: URL, sizeInBytes: Int) {
        infoLog("GameScene.createEmptyHDD() called", category: .fileSystem)
        infoLog("Target file: \(url.path)", category: .fileSystem)
        infoLog("Size: \(sizeInBytes) bytes (\(sizeInBytes / (1024 * 1024)) MB)", category: .fileSystem)
        
        // Validate size (must be reasonable for X68000 HDD)
        let maxSize = 80 * 1024 * 1024 // 80MB maximum
        let minSize = 1024 * 1024      // 1MB minimum
        
        guard sizeInBytes >= minSize && sizeInBytes <= maxSize else {
            errorLog("Invalid HDD size: \(sizeInBytes) bytes", category: .fileSystem)
            showAlert(title: "Error", message: "Invalid HDD size. Must be between 1MB and 80MB.")
            return
        }
        
        // Ensure size is multiple of 256 bytes (sector size)
        guard sizeInBytes % 256 == 0 else {
            errorLog("HDD size must be multiple of 256 bytes (sector size)", category: .fileSystem)
            showAlert(title: "Error", message: "HDD size must be multiple of 256 bytes.")
            return
        }
        
        do {
            // Create empty data filled with zeros
            let emptyData = Data(repeating: 0, count: sizeInBytes)
            
            // Write to file
            try emptyData.write(to: url)
            
            infoLog("Empty HDD created successfully: \(url.lastPathComponent)", category: .fileSystem)
            infoLog("Size: \(emptyData.count) bytes", category: .fileSystem)
            
            // Automatically load the created HDD
            DispatchQueue.main.async {
                infoLog("Auto-loading created HDD...", category: .fileSystem)
                self.loadHDD(url: url)
                
                // Show success message
                self.showAlert(title: "Success", 
                              message: "Empty HDD '\(url.lastPathComponent)' created and mounted successfully.")
            }
            
        } catch {
            errorLog("Error creating HDD file", error: error, category: .fileSystem)
            showAlert(title: "Error", 
                     message: "Failed to create HDD file: \(error.localizedDescription)")
        }
    }
    
    private func showAlert(title: String, message: String) {
        #if os(macOS)
        DispatchQueue.main.async {
            let alert = NSAlert()
            alert.messageText = title
            alert.informativeText = message
            alert.alertStyle = .informational
            alert.addButton(withTitle: "OK")
            alert.runModal()
        }
        #endif
    }
    
    // MARK: - Clock Management
    func setCPUClock(_ mhz: Int) {
        // debugLog("GameScene.setCPUClock() called with: \(mhz) MHz", category: .emulation)
        
        // Clamp clock speed to safe range to prevent integer overflow in emulator core
        let safeMHz = max(1, min(mhz, 50))  // Limit to 50MHz maximum
        if safeMHz != mhz {
            warningLog("Clock speed clamped from \(mhz) MHz to \(safeMHz) MHz for stability", category: .emulation)
        }
        
        clockMHz = safeMHz
        
        // Save to UserDefaults
        userDefaults.set("\(safeMHz)", forKey: "clock")
        
        // Show visual feedback
        showClockChangeNotification(safeMHz)
        
        infoLog("CPU Clock set to: \(clockMHz) MHz", category: .emulation)
    }
    
    private func showClockChangeNotification(_ mhz: Int) {
        let notification = SKLabelNode(text: "\(mhz) MHz")
        notification.fontName = "Helvetica-Bold"
        notification.fontSize = 48
        notification.fontColor = .yellow
        notification.zPosition = 1000  // High zPosition for overlays
        notification.position = CGPoint(x: 0, y: 0)
        notification.alpha = 0
        
        // Animation disabled - immediately show and hide notification
        notification.alpha = 1.0
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
            notification.removeFromParent()
        }
        addChild(notification)
        
        // Hide title logo when clock is changed
        hideTitleLogo()
    }
    
    // MARK: - System Management
    func resetSystem() {
        infoLog("GameScene.resetSystem() called - performing manual reset", category: .emulation)
        X68000_Reset()
        infoLog("System reset completed", category: .emulation)
    }

    // MARK: - CRT Display Mode
    func setCRTDisplayPreset(_ preset: CRTPreset) {
        infoLog("Setting CRT display preset: \(preset.rawValue)", category: .ui)
        crtPreset = preset
        userDefaults.set(preset.rawValue, forKey: "CRTDisplayPreset")
        if spr.parent != nil {
            crtFilter.apply(preset: preset)
            crtFilter.attach(to: spr)
        }
        applyCIEffectsForCurrentCRT()
        // Ensure overlays/filters reflect preset (including OFF)
        if crtEffectRoot != nil {
            let s = currentCRTSettings()
            scanlineOverlay?.alpha = CGFloat(max(0.0, min(1.0, s.scanlineIntensity * 0.8)))
            noiseOverlay?.alpha = CGFloat(max(0.0, min(1.0, s.noiseIntensity * 0.25)))
            rebuildOverlaysIfNeeded(size: spr.size)
            rebuildCornerVignetteIfNeeded(size: spr.size, intensity: s.vignetteIntensity)
            applyChromaticSettings() // disables chroma base/filter when strength is 0 (OFF)
        }
        // Small on-screen notice
        let note = SKLabelNode(text: "CRT: \(preset.rawValue.capitalized)")
        note.fontName = "Helvetica-Bold"
        note.fontSize = 24
        note.fontColor = .yellow
        note.zPosition = 1000
        note.position = CGPoint(x: 0, y: size.height/2 - 60)
        note.alpha = 0.0
        addChild(note)
        note.run(.sequence([
            .fadeIn(withDuration: 0.12),
            .wait(forDuration: 0.8),
            .fadeOut(withDuration: 0.25),
            .removeFromParent()
        ]))
    }

    func applyCRTSettings(_ settings: CRTSettings) {
        infoLog("Applying custom CRT settings", category: .ui)
        crtPreset = .custom
        userDefaults.set(crtPreset.rawValue, forKey: "CRTDisplayPreset")
        var s = settings
        s.enabled = true // ensure enabled when adjusting via UI
        crtFilter.update(settings: s)
        if spr.parent != nil {
            crtFilter.attach(to: spr)
        }
        applyCIEffectsForCurrentCRT()
        // Overlays: scanlines/noise/corner vignette
        if crtEffectRoot != nil {
            scanlineOverlay?.alpha = CGFloat(max(0.0, min(1.0, settings.scanlineIntensity * 0.8)))
            noiseOverlay?.alpha = CGFloat(max(0.0, min(1.0, settings.noiseIntensity * 0.25)))
            rebuildOverlaysIfNeeded(size: spr.size)
            rebuildCornerVignetteIfNeeded(size: spr.size, intensity: settings.vignetteIntensity)
        }
        // Update chromatic overlays
        applyChromaticSettings()
    }

    func toggleCRTSettingsPanel() {
        if let overlay = crtOverlay {
            overlay.removeFromParent()
            crtOverlay = nil
            return
        }
        let overlay = CRTOverlay()
        overlay.position = CGPoint(x: 0, y: 0)
        overlay.zPosition = 999
        // Collect current settings (from preset applied in filter)
        let current = currentCRTSettings()
        overlay.configure(with: current)
        overlay.onValueChanged = { [weak self] key, value in
            guard let self = self else { return }
            var s = self.currentCRTSettings()
            switch key {
            case "scanlineIntensity": s.scanlineIntensity = value
            case "curvature": s.curvature = value
            case "chroma": s.chromaShiftR = value; s.chromaShiftB = -value
            case "persistence": s.phosphorPersistence = value
            case "vignette": s.vignetteIntensity = value
            case "bloom": s.bloomIntensity = value
            case "noise": s.noiseIntensity = value
            default: break
            }
            self.applyCRTSettings(s)
        }
        overlay.onClose = { [weak self] in
            self?.crtOverlay?.removeFromParent()
            self?.crtOverlay = nil
        }
        addChild(overlay)
        crtOverlay = overlay
    }

    private func currentCRTSettings() -> CRTSettings {
        // Try to return current filter settings if available; otherwise from preset
        if crtPreset == .custom {
            return crtFilter.settings
        }
        return CRTPresets.settings(for: crtPreset)
    }
    
    // MARK: - Screen Rotation Management
    func rotateScreen() {
        debugLog("GameScene.rotateScreen() called", category: .ui)
        // 次の回転状態に切り替え
        let allRotations = ScreenRotation.allCases
        if let currentIndex = allRotations.firstIndex(of: currentRotation) {
            let nextIndex = (currentIndex + 1) % allRotations.count
            currentRotation = allRotations[nextIndex]
        }
        
        applyScreenRotation()
    }
    
    func setScreenRotation(_ rotation: ScreenRotation) {
        debugLog("GameScene.setScreenRotation() called with: \(rotation.displayName)", category: .ui)
        currentRotation = rotation
        applyScreenRotation()
    }
    
    private func applyScreenRotation() {
        debugLog("Applying screen rotation: \(currentRotation.displayName)", category: .ui)
        
        // 回転とスケーリングを適用
        applyRotationToSprite()
        
        // ユーザー設定に保存
        userDefaults.set(currentRotation == .portrait, forKey: "ScreenRotation_Portrait")
        
        // macOSの場合はウィンドウサイズも調整
        #if os(macOS)
        notifyWindowSizeChange()
        #endif
    }
    
    
    private func applyRotationToSprite() {
        // エミュレータの基本画面サイズを取得
        let w = Int(X68000_GetScreenWidth())
        let h = Int(X68000_GetScreenHeight())
        
        // Scene全体のサイズを取得（ウィンドウサイズ）
        let sceneSize = self.size
        
        // 回転状態に応じたスケーリングを計算
        let scaleX: CGFloat
        let scaleY: CGFloat
        
        switch currentRotation {
        case .landscape:
            // 通常の横画面：シーンサイズに合わせてスケーリング
            scaleX = sceneSize.width / CGFloat(w)
            scaleY = sceneSize.height / CGFloat(h)
        case .portrait:
            // 縦画面：回転後のフィット計算
            // 回転後のエミュレータ画面（w×h が h×w になる）をシーンに収める
            let rotatedWidth = CGFloat(h)  // 回転後の幅
            let rotatedHeight = CGFloat(w) // 回転後の高さ
            
            // シーンサイズに収まるようにスケーリング
            let scaleToFitX = sceneSize.width / rotatedWidth
            let scaleToFitY = sceneSize.height / rotatedHeight
            let uniformScale = min(scaleToFitX, scaleToFitY) // アスペクト比を維持
            
            scaleX = uniformScale
            scaleY = uniformScale
        }
        
        // スケーリングと回転を適用
        spr.xScale = scaleX
        spr.yScale = scaleY
        spr.zRotation = currentRotation.angle
        
        // 回転時の位置調整（中央に配置）
        spr.position = CGPoint(x: 0, y: 0)
        
        infoLog("Applied rotation: \(currentRotation.displayName), scale: \(scaleX)x\(scaleY), scene: \(sceneSize)", category: .ui)
    }
    
    // Apply rotation and scaling silently (for update loop)
    private func applySpriteTransformSilently() {
        let w = Int(X68000_GetScreenWidth())
        let h = Int(X68000_GetScreenHeight())
        let sceneSize = self.size
        
        let scaleX: CGFloat
        let scaleY: CGFloat
        
        switch currentRotation {
        case .landscape:
            scaleX = sceneSize.width / CGFloat(w)
            scaleY = sceneSize.height / CGFloat(h)
        case .portrait:
            let rotatedWidth = CGFloat(h)
            let rotatedHeight = CGFloat(w)
            let scaleToFitX = sceneSize.width / rotatedWidth
            let scaleToFitY = sceneSize.height / rotatedHeight
            let uniformScale = min(scaleToFitX, scaleToFitY)
            scaleX = uniformScale
            scaleY = uniformScale
        }
        
        spr.xScale = scaleX
        spr.yScale = scaleY
        spr.zRotation = currentRotation.angle
        spr.position = CGPoint(x: 0, y: 0)
    }
    
    #if os(macOS)
    private func notifyWindowSizeChange() {
        // macOSでウィンドウサイズ変更を通知
        NotificationCenter.default.post(name: .screenRotationChanged, object: currentRotation)
    }
    #endif
    
    let userDefaults = UserDefaults.standard
    
    private func settings() {
        if let clock = userDefaults.object(forKey: "clock") as? String,
           let clockValue = Int(clock) {
            self.clockMHz = clockValue
            infoLog("CPU Clock: \(self.clockMHz) MHz", category: .emulation)
        } else if userDefaults.object(forKey: "clock") != nil {
            warningLog("Invalid clock value in UserDefaults, using default: \(self.clockMHz) MHz", category: .emulation)
        }

        // 画面回転設定の読み込み - 起動時は常に横画面に強制
        currentRotation = .landscape
        infoLog("Screen rotation forced to Landscape on startup", category: .ui)

        if let virtual_mouse = userDefaults.object(forKey: "virtual_mouse") as? Bool {
            infoLog("virtual_mouse: \(virtual_mouse)", category: .input)
        }
        if let virtual_pad = userDefaults.object(forKey: "virtual_pad") as? Bool {
            infoLog("virtual_pad: \(virtual_pad)", category: .input)
            virtualPad.isHidden = !virtual_pad
        }
        if let sample = userDefaults.object(forKey: "samplingrate") as? String,
           let sampleValue = Int(sample) {
            self.samplingRate = sampleValue
            infoLog("Sampling Rate: \(self.samplingRate) Hz", category: .audio)
        } else if userDefaults.object(forKey: "samplingrate") != nil {
            warningLog("Invalid sampling rate value in UserDefaults, using default: \(self.samplingRate) Hz", category: .audio)
        }
        if let fps = userDefaults.object(forKey: "fps") as? String,
           let fpsValue = Int(fps) {
            view?.preferredFramesPerSecond = fpsValue
            infoLog("FPS: \(fpsValue) Hz", category: .ui)
        } else if userDefaults.object(forKey: "fps") != nil {
            warningLog("Invalid FPS value in UserDefaults, using default", category: .ui)
        }
        if let vsync = userDefaults.object(forKey: "vsync") as? Bool {
            self.vsync = vsync
            infoLog("V-Sync: \(vsync)", category: .ui)
        }

        // Load CRT preset if saved (default: off)
        if let presetStr = userDefaults.string(forKey: "CRTDisplayPreset"),
           let preset = CRTPreset(rawValue: presetStr) {
            crtPreset = preset
        } else {
            crtPreset = .off
        }
    }
    
    func setUpScene() {
        settings()
        
        self.fileSystem = FileSystem()
        self.fileSystem?.gameScene = self  // Set reference for timer management
        // Load ROM files FIRST, before any emulator initialization
        guard let fileSystem = self.fileSystem else {
            fatalError("FileSystem not available")
        }
        
        guard fileSystem.loadIPLROM() && fileSystem.loadCGROM() else {
            errorLog("CRITICAL: Required ROM files not found - stopping emulator initialization", category: .emulation)
            return
        }
        
        // Initialize emulator AFTER ROM files are loaded
        X68000_Init(samplingRate)
        
        self.fileSystem?.loadSRAM()
        
        // Use new state restore system for auto-mounting
        debugLog("GameScene: Calling bootWithStateRestore()", category: .fileSystem)
        self.fileSystem?.bootWithStateRestore()

        guard let joyCardSprite = self.childNode(withName: "//JoyCard") as? SKSpriteNode else {
            fatalError("JoyCard sprite node not found in scene")
        }
        joycard = X68JoyCard(id: 0, scene: self, sprite: joyCardSprite)
        devices.append(joycard!)
        
        for device in devices {
            device.Reset()
        }
        
        // Mark emulator as initialized with a small delay to ensure stability
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            self.isEmulatorInitialized = true
            infoLog("Emulator initialization complete - using SpriteKit update", category: .emulation)
            
            // Apply saved screen rotation after emulator is fully initialized
            self.applyScreenRotation()
        }
        
        mouseController = X68MouseController()
        self.joycontroller = JoyController()
        self.joycontroller?.setup(callback: controller_event(status:))
        
        let sample = self.samplingRate
        if view?.preferredFramesPerSecond == 120 && self.vsync == false {
            // sample *= 2
        }
        self.audioStream = AudioStream(samplingrate: sample)
        self.audioStream?.play()
        
        self.mouseSprite = self.childNode(withName: "//Mouse") as? SKSpriteNode
        
        self.labelStatus = self.childNode(withName: "//labelStatus") as? SKLabelNode
        self.labelMIDI = self.childNode(withName: "//labelMIDI") as? SKLabelNode
        
        // Adjust MIDI label position to avoid overlap with input mode button
        adjustMIDILabelPosition()
        
        // Setup input mode toggle button
        setupInputModeButton()
        
        // Set unique zPosition values for all UI layers
        setupLayerZPositions()
        
        self.label = self.childNode(withName: "//helloLabel") as? SKLabelNode
        if let label = self.label {
            label.alpha = 0.0
            label.zPosition = 3.0
            label.blendMode = .add
            
            // Fade in the label
            let fadeInAction = SKAction.fadeIn(withDuration: 2.0)
            let waitAction = SKAction.wait(forDuration: 4.0)
            let fadeOutAction = SKAction.fadeOut(withDuration: 2.0)
            let sequence = SKAction.sequence([fadeInAction, waitAction, fadeOutAction])
            label.run(sequence)
        }
        
        self.titleSprite = SKSpriteNode(imageNamed: "X68000LogoW.png")
        self.titleSprite?.zPosition = 3.0
        self.titleSprite?.alpha = 0.0
        self.titleSprite?.blendMode = .add
        self.titleSprite?.setScale(1.0)
        self.addChild(titleSprite!)
        
        // Title sprite animation
        if let titleSprite = self.titleSprite {
            let fadeInAction = SKAction.fadeIn(withDuration: 2.0)
            let waitAction = SKAction.wait(forDuration: 4.0)
            let fadeOutAction = SKAction.fadeOut(withDuration: 2.0)
            let removeAction = SKAction.run {
                self.hideIOSLegacyUI()
            }
            let sequence = SKAction.sequence([fadeInAction, waitAction, fadeOutAction, removeAction])
            titleSprite.run(sequence)
        }
        
        // Spinny node creation removed to eliminate green square at startup
        
        #if os(iOS)
        let tapGes = UITapGestureRecognizer(target: self, action: #selector(self.tapped(_:)))
        tapGes.numberOfTapsRequired = 1
        tapGes.numberOfTouchesRequired = 1
        self.view?.addGestureRecognizer(tapGes)
        
        let hover = UIHoverGestureRecognizer(target: self, action: #selector(hovering(_:)))
        self.view?.addGestureRecognizer(hover)
        #endif
        self.addChild(spr)

        // Superimpose: attach background video manager and apply uniforms
        superManager.attach(to: self)
        applySuperimposeUniforms()
    }
    
    #if os(iOS)
    @objc func hovering(_ sender: UIHoverGestureRecognizer) {
        debugLog("hovering: \(sender.state.rawValue)", category: .input)
        if #available(iOS 13.4, *) {
            debugLog("Button mask: \(sender.buttonMask.rawValue)", category: .input)
        } else {
            // Fallback on earlier versions
        }
        switch sender.state {
        case .began:
            debugLog("Hover", category: .input)
        case .changed:
            debugLog("Location: \(sender.location(in: self.view))", category: .input)
            let pos = self.view?.convert(sender.location(in: self.view), to: self)
            mouseController?.SetPosition(pos!, scene!.size)
            break
        case .ended:
            break
        default:
            break
        }
    }
    
    @objc func tapped(_ sender: UITapGestureRecognizer) {
        debugLog("Tap state: \(sender.state)", category: .input)
        if sender.state == .began {
            debugLog("began", category: .input)
        }
        if sender.state == .recognized {
            debugLog("recognized", category: .input)
            mouseController?.ClickOnce()
        }
        if sender.state == .ended {
            debugLog("ended", category: .input)
        }
    }
    #endif
    
    func applicationWillEnterForeground() {
        settings()
    }
    
    func applicationWillResignActive() {
        // Pause audio to prevent underruns when app is inactive
        audioStream?.pause()
    }
    
    func applicationDidBecomeActive() {
        // Resume audio when app becomes active
        audioStream?.play()
    }
    
    override func sceneDidLoad() {
        debugLog("sceneDidLoad", category: .ui)
    }
    
    deinit {
        // Save HDD before cleanup
        if X68000_IsHDDReady() != 0 {
            infoLog("Final HDD save in deinit...", category: .fileSystem)
            X68000_SaveHDD()
        }
        
        // Direct file writes - no timer needed
        
        // Cleanup
        isEmulatorInitialized = false
    }
    
    override func didChangeSize(_ oldSize: CGSize) {
        debugLog("didChangeSize \(oldSize)", category: .ui)
        screen_w = Float(oldSize.width)
        screen_h = Float(oldSize.height)
    }
    
    var virtualPad: SKNode = SKNode()
    
    override func didMove(to view: SKView) {
        debugLog("didMove", category: .ui)
        
        // Fix layer ordering: Enable zPosition-based rendering for consistent drawing order
        view.ignoresSiblingOrder = true
        view.preferredFramesPerSecond = 60  // Fixed frame rate for emulator pacing
        
        self.setUpScene()
        
        // Screen rotation will be applied after emulator initialization in setUpScene()
        
        // Timer will be started after emulator initialization in setUpScene()
        
        // Direct file writes enabled - no timer needed
        
        let moveJoystickHiddenArea = TLAnalogJoystickHiddenArea(rect: CGRect(x: -scene!.size.width / 2, y: -scene!.size.height * 0.5, width: scene!.size.width / 2, height: scene!.size.height * 0.9))
        moveJoystickHiddenArea.joystick = moveJoystick
        moveJoystick.isMoveable = true
        moveJoystickHiddenArea.zPosition = 10.0
        moveJoystickHiddenArea.strokeColor = .clear
        virtualPad.addChild(moveJoystickHiddenArea)
        
        let rotateJoystickHiddenArea = TLAnalogJoystickHiddenArea(rect: CGRect(x: (scene!.size.width / 8) * 2, y: -scene!.size.height * 0.5, width: scene!.size.width / 8, height: scene!.size.height * 0.9))
        rotateJoystickHiddenArea.joystick = rotateJoystick
        rotateJoystickHiddenArea.zPosition = 10.0
        rotateJoystickHiddenArea.strokeColor = .clear
        virtualPad.addChild(rotateJoystickHiddenArea)
        
        let rotateJoystickHiddenArea2 = TLAnalogJoystickHiddenArea(rect: CGRect(x: (scene!.size.width / 8) * 3, y: -scene!.size.height * 0.5, width: scene!.size.width / 8, height: scene!.size.height * 0.9))
        rotateJoystickHiddenArea2.joystick = rotateJoystick2
        rotateJoystickHiddenArea2.zPosition = 10.0
        rotateJoystickHiddenArea2.strokeColor = .clear
        virtualPad.addChild(rotateJoystickHiddenArea2)
        
        addChild(virtualPad)
        
        // Update virtual pad visibility based on input mode
        updateVirtualPadVisibility()
        
        moveJoystick.on(.begin) { _ in
            // Empty handler for begin event
        }
        
        moveJoystick.on(.move) { [weak self] joystick in
            guard let self = self, let joycard = self.joycard else { return }
            
            let VEL: CGFloat = 5.0
            let VELY: CGFloat = 10.0
            let oldJoydata = joycard.joydata
            
            // Update direction flags based on velocity
            var newJoydata = oldJoydata
            
            // Horizontal movement
            if joystick.velocity.x > VEL {
                newJoydata |= JOY_RIGHT
                newJoydata &= ~JOY_LEFT
            } else if joystick.velocity.x < -VEL {
                newJoydata |= JOY_LEFT
                newJoydata &= ~JOY_RIGHT
            } else {
                newJoydata &= ~(JOY_RIGHT | JOY_LEFT)
            }
            
            // Vertical movement
            if joystick.velocity.y > VELY {
                newJoydata |= JOY_UP
                newJoydata &= ~JOY_DOWN
            } else if joystick.velocity.y < -VELY {
                newJoydata |= JOY_DOWN
                newJoydata &= ~JOY_UP
            } else {
                newJoydata &= ~(JOY_UP | JOY_DOWN)
            }
            
            // Only update if state actually changed
            if newJoydata != oldJoydata {
                joycard.joydata = newJoydata
                X68000_Joystick_Set(UInt8(0), newJoydata)
            }
        }
        
        moveJoystick.on(.end) { [weak self] _ in
            guard let self = self else { return }
            self.joycard?.joydata &= ~(JOY_RIGHT | JOY_LEFT | JOY_DOWN | JOY_UP)
            if let joydata = self.joycard?.joydata {
                X68000_Joystick_Set(UInt8(0), joydata)
            }
        }
        
        rotateJoystick.on(.begin) { [weak self] _ in
            guard let self = self else { return }
            self.joycard?.joydata |= JOY_TRG2
            if let joydata = self.joycard?.joydata {
                X68000_Joystick_Set(UInt8(0), joydata)
            }
        }
        
        rotateJoystick.on(.move) { [weak self] joystick in
            guard let self = self, let joycard = self.joycard else { return }
            
            // Only update if TRG2 is not already set
            let oldJoydata = joycard.joydata
            if (oldJoydata & JOY_TRG2) == 0 {
                joycard.joydata |= JOY_TRG2
                X68000_Joystick_Set(UInt8(0), joycard.joydata)
            }
        }
        
        rotateJoystick.on(.end) { [weak self] _ in
            guard let self = self else { return }
            self.joycard?.joydata &= ~(JOY_TRG2)
            if let joydata = self.joycard?.joydata {
                X68000_Joystick_Set(UInt8(0), joydata)
            }
        }
        
        rotateJoystick2.on(.begin) { [weak self] _ in
            guard let self = self else { return }
            self.joycard?.joydata |= JOY_TRG1
            if let joydata = self.joycard?.joydata {
                X68000_Joystick_Set(UInt8(0), joydata)
            }
        }
        
        rotateJoystick2.on(.move) { [weak self] joystick in
            guard let self = self, let joycard = self.joycard else { return }
            
            // Only update if TRG1 is not already set
            let oldJoydata = joycard.joydata
            if (oldJoydata & JOY_TRG1) == 0 {
                joycard.joydata |= JOY_TRG1
                X68000_Joystick_Set(UInt8(0), joycard.joydata)
            }
        }
        
        rotateJoystick2.on(.end) { [weak self] _ in
            guard let self = self else { return }
            self.joycard?.joydata &= ~(JOY_TRG1)
            if let joydata = self.joycard?.joydata {
                X68000_Joystick_Set(UInt8(0), joydata)
            }
        }
        
        #if os(iOS)
        moveJoystick.handleImage = UIImage(named: "jStick")
        moveJoystick.baseImage = UIImage(named: "jSubstrate")
        
        rotateJoystick.handleImage = UIImage(named: "jStick")
        rotateJoystick2.handleImage = UIImage(named: "jStick")
        #elseif os(macOS)
        moveJoystick.handleImage = NSImage(named: "jStick")
        moveJoystick.baseImage = NSImage(named: "jSubstrate")
        
        rotateJoystick.handleImage = NSImage(named: "jStick")
        rotateJoystick2.handleImage = NSImage(named: "jStick")
        #endif
    }
    
    func makeSpinny(at pos: CGPoint, color: SKColor) {
        // Only allow clock changes in the upper-left clock control area
        // Exclude the input mode button area (x: -size.width/2 + 80, y: size.height/2 - 30)
        let modeButtonX = -size.width/2 + 150
        let modeButtonY = size.height/2 - 30
        
        // Don't allow clock changes if we're too close to the mode button
        let distanceFromModeButton = sqrt(pow(pos.x - modeButtonX, 2) + pow(pos.y - modeButtonY, 2))
        if distanceFromModeButton < 100.0 {
            return
        }
        
        // Don't allow clock changes in lower area or far right
        if pos.y < 400.0 || pos.x > 300.0 {
            return
        }
        
        if let spinny = self.spinnyNode?.copy() as? SKShapeNode {
            spinny.position = pos
            spinny.strokeColor = color
            spinny.lineWidth = 0.1
            spinny.zPosition = 1.0
            
            var clock = 0.0
            if pos.x < -500 {
                clock = 1.0
            } else if pos.x < -400 {
                clock = 10.0
            } else if pos.x < -200 {
                clock = 16.0
            } else if pos.x < 0 {
                clock = 24.0
            } else if pos.x < 300 {
                clock = ((Double(pos.y) + 1000.0) / 2000.0)
                clock *= 200.0
            }
            
            if clock > 0.0 {
                clockMHz = Int(clock)
                let label = SKLabelNode()
                label.text = "\(clockMHz) MHz"
                label.fontName = "Helvetica Neue"
                label.fontSize = 36
                label.horizontalAlignmentMode = .center
                label.verticalAlignmentMode = .center
                label.fontColor = .yellow
                
                spinny.addChild(label)
                
                // Hide title logo when clock is changed
                hideTitleLogo()
                
                self.addChild(spinny)
            }
        }
    }
    
    var aaa = 0
    var timer: Timer?
    var sramSaveTimer: Timer?
    private let timerQueue = DispatchQueue(label: "timer.queue", qos: .userInteractive)
    private var sramSaveCounter = 0
    
    func startUpdateTimer() {
        // Performance optimization: Use SpriteKit's native update() instead of Timer
        // This eliminates duplicate update loops and improves performance
        
        stopUpdateTimer()
        
        // Only start SRAM save timer - main updates handled by SpriteKit update()
        DispatchQueue.main.async {
            infoLog("Using SpriteKit native update - Timer-based updates disabled for performance", category: .emulation)
            
            // Start periodic SRAM save timer (every 30 seconds)
            self.sramSaveTimer = Timer.scheduledTimer(timeInterval: 30.0, target: self, selector: #selector(self.periodicSRAMSave), userInfo: nil, repeats: true)
            infoLog("Periodic SRAM save timer started", category: .fileSystem)
        }
    }
    
    func ensureTimerRunning() {
        // Public method to restart timer if needed (for disk loading)
        if timer?.isValid != true && isEmulatorInitialized {
            warningLog("Timer not running, restarting...", category: .emulation)
            startUpdateTimer()
        }
    }
    
    @objc func diskImageLoaded() {
        infoLog("Disk image loaded notification received - ensuring timer is running", category: .fileSystem)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            self.ensureTimerRunning()
        }
    }
    
    func stopUpdateTimer() {
        timer?.invalidate()
        timer = nil
        sramSaveTimer?.invalidate()
        sramSaveTimer = nil
    }
    
    @objc func updateGame() {
        // Legacy method - replaced by SpriteKit native update() for performance
        // This method is no longer called to eliminate duplicate updates
    }
    
    private func performGameUpdate() {
        // Legacy method - functionality moved to SpriteKit update() method
        // This eliminates duplicate processing and improves performance
    }
    
    @objc func periodicSRAMSave() {
        // Save SRAM periodically to prevent data loss
        sramSaveCounter += 1
        debugLog("Periodic SRAM save #\(sramSaveCounter)", category: .fileSystem)
        fileSystem?.saveSRAM()
    }
    
    var d = [UInt8](repeating: 0xff, count: 768 * 512 * 4)
    var w: Int = 1
    var h: Int = 1
    
    override func update(_ currentTime: TimeInterval) {
        // Safety: Only update if emulator is properly initialized
        guard isEmulatorInitialized else {
            return
        }
        
        // Fixed-step frame pacing for 55.45Hz emulator on 60Hz display
        if lastUpdateTime == 0.0 {
            lastUpdateTime = currentTime
        }
        
        let deltaTime = min(0.05, currentTime - lastUpdateTime)  // Cap to prevent large jumps
        lastUpdateTime = currentTime
        fixedStepAccumulator += deltaTime
        
        var newFrameReady = false
        
        // Update emulator in fixed steps
        while fixedStepAccumulator >= targetFrameTime {
            // Optimized device updates based on input mode
            if currentInputMode == .joycard {
                joycard?.Update(currentTime)
            }
            
            // Update screen dimensions first, then configure mouse controller
            w = Int(X68000_GetScreenWidth())
            h = Int(X68000_GetScreenHeight())
            
            // Only update mouse controller when in capture mode
            if let mouseController = mouseController, mouseController.isCaptureMode {
                mouseController.SetScreenSize(width: Float(w), height: Float(h))
                mouseController.Update()
            }

            // Tick FDD reinsert protection to counter program-driven eject at boot
            DiskStateManager.shared.tickFDDReinsertProtection()
            
            // Step emulator forward one frame
            X68000_Update(self.clockMHz, self.vsync ? 1 : 0)
            
            fixedStepAccumulator -= targetFrameTime
            newFrameReady = true
        }
        
        // Only update display when new emulator frame is ready
        if newFrameReady {
            // Security: Validate screen dimensions
            guard w > 0 && h > 0 && w <= 1024 && h <= 1024 else {
                return
            }
            
            X68000_GetImage(&d)
            updateScreenTexture()
        }
    }
    
    private func updateScreenTexture() {
        let cgsize = CGSize(width: w, height: h)
        _ = (w != lastScreenWidth || h != lastScreenHeight)
        
        // Fall back to standard texture creation (SpriteKit doesn't expose Metal device directly)
        fallbackTextureUpdate(cgsize)
    }
    
    private func fallbackTextureUpdate(_ cgsize: CGSize) {
        // Fallback to old method if Metal fails
        let tex = SKTexture(data: Data(d), size: cgsize, flipped: true)
        tex.filteringMode = .nearest  // Pixel art filtering
        updateSpriteWithTexture(tex, size: cgsize)
    }
    
    private func updateSpriteWithTexture(_ texture: SKTexture, size: CGSize) {
        let screenSizeChanged = (w != lastScreenWidth || h != lastScreenHeight)
        
        if screenSizeChanged || spr.parent == nil {
            // Recreate sprite only when necessary
            if spr.parent != nil { spr.removeFromParent() }
            if let n = crtEffectRoot { n.removeFromParent(); crtEffectRoot = nil }
            
            spr = SKSpriteNode(texture: texture, size: size)
            spr.zPosition = 0  // Use consistent zPosition from setupLayerZPositions
            spr.blendMode = .alpha
            applySpriteTransformSilently()
            syncOverlaysTransformToSprite()

            // Build effect chain: Curvature -> Vignette -> Bloom -> ChromaBase(G-only) -> Sprite
            let bloomNode = SKEffectNode()
            bloomNode.shouldRasterize = true
            bloomNode.shouldEnableEffects = false
            bloomNode.zPosition = 0
            // Insert chroma base node to optionally strip R/B from base
            let cbase = SKEffectNode()
            cbase.shouldRasterize = true
            cbase.shouldEnableEffects = false
            cbase.zPosition = 0
            cbase.addChild(spr)
            bloomNode.addChild(cbase)

            let vignetteNode = SKEffectNode()
            vignetteNode.shouldRasterize = true
            vignetteNode.shouldEnableEffects = false
            vignetteNode.zPosition = 0
            vignetteNode.addChild(bloomNode)

            let curvatureNode = SKEffectNode()
            curvatureNode.shouldRasterize = true
            curvatureNode.shouldEnableEffects = false
            curvatureNode.zPosition = 0
            curvatureNode.addChild(vignetteNode)

            self.addChild(curvatureNode)
            crtBloomNode = bloomNode
            crtVignetteNode = vignetteNode
            crtCurvatureNode = curvatureNode
            chromaBaseNode = cbase
            crtEffectRoot = curvatureNode

            // Attach CRT filter when sprite is (re)created
            crtFilter.apply(preset: crtPreset)
            crtFilter.attach(to: spr)

            // Also apply CI-based effects for visible feedback (bloom/vignette)
            applyCIEffectsForCurrentCRT()
            // Build overlays (scanlines/noise) sized to current content
            rebuildOverlaysIfNeeded(size: size)
            rebuildChromaOverlaysIfNeeded(texture: texture, size: size)
            rebuildPersistenceOverlayIfNeeded(size: size)
            rebuildCornerVignetteIfNeeded(size: size, intensity: currentCRTSettings().vignetteIntensity)
            
            lastScreenWidth = w
            lastScreenHeight = h
        } else {
            // Just update texture - keep existing sprite node
            spr.texture = texture
        }

        // Update CRT uniforms each frame
        let res = vector_float2(Float(w), Float(h))
        crtFilter.frameDidUpdate(with: texture, resolution: res, dt: Float(targetFrameTime))
        syncOverlaysTransformToSprite()
        syncChromaticTransforms()
        updatePersistenceOverlay(with: texture)
        updateChromaticOverlayTextures(texture)
        applySuperimposeUniforms()

        // Debug: handled in shader via uniform

        // Ensure no debug tint is applied
        spr.colorBlendFactor = 0.0

        // Update overlays content when enabled
        updateNoiseOverlay()
    }

    private func applyCIEffectsForCurrentCRT() {
        guard let bloomNode = crtBloomNode, let vignetteNode = crtVignetteNode else { return }
        let s = currentCRTSettings()
        if s.bloomIntensity > 0.001, let f = CIFilter(name: "CIBloom") {
            f.setDefaults()
            let b = CGFloat(max(0.0, min(1.0, s.bloomIntensity)))
            // Softer, non-linear response: very gentle near 0.0, grows smoothly
            let shaped = pow(b, 1.6)
            let intensityVal = shaped * 0.5            // previously very strong; halve it
            let radiusVal = CGFloat(12.0) + shaped * 48.0 // narrower default radius for softness
            if intensityVal > 0.02 {
                f.setValue(intensityVal, forKey: kCIInputIntensityKey)
                f.setValue(radiusVal, forKey: kCIInputRadiusKey)
                bloomNode.filter = f
                bloomNode.shouldEnableEffects = true
            } else {
                bloomNode.filter = nil
                bloomNode.shouldEnableEffects = false
            }
        } else {
            bloomNode.filter = nil
            bloomNode.shouldEnableEffects = false
        }
        // Disable CI vignette; use corner-only overlay instead
        vignetteNode.filter = nil
        vignetteNode.shouldEnableEffects = false

        // Curvature via lens/pinch distortion (outermost)
        if let curveNode = crtCurvatureNode {
            if s.curvature > 0.001 {
                if let f = CIFilter(name: "CILensDistortion") {
                    f.setDefaults()
                    let contentSize = spr.size
                    let minSide = max(1.0, min(contentSize.width, contentSize.height))
                    f.setValue(CIVector(x: contentSize.width * 0.5, y: contentSize.height * 0.5), forKey: kCIInputCenterKey)
                    f.setValue(minSide * 0.5, forKey: kCIInputRadiusKey)
                    // negative distortion for barrel (CRT curvature)
                    f.setValue(-CGFloat(s.curvature) * 80.0, forKey: "inputDistortion")
                    curveNode.filter = f
                    curveNode.shouldEnableEffects = true
                } else if let f = CIFilter(name: "CIPinchDistortion") {
                    f.setDefaults()
                    let contentSize = spr.size
                    let minSide = max(1.0, min(contentSize.width, contentSize.height))
                    f.setValue(CIVector(x: contentSize.width * 0.5, y: contentSize.height * 0.5), forKey: kCIInputCenterKey)
                    f.setValue(minSide * 0.5, forKey: kCIInputRadiusKey)
                    f.setValue(-CGFloat(s.curvature) * 0.6, forKey: kCIInputScaleKey)
                    curveNode.filter = f
                    curveNode.shouldEnableEffects = true
                }
            } else {
                curveNode.filter = nil
                curveNode.shouldEnableEffects = false
            }
        }
    }

    // Debug helpers removed
    
    // MARK: - Overlays (Scanlines / Noise)
    private func rebuildOverlaysIfNeeded(size: CGSize) {
        guard size != .zero else { return }
        if size == lastOverlaySize, scanlineOverlay != nil, noiseOverlay != nil { return }
        lastOverlaySize = size

        // Remove old
        scanlineOverlay?.removeFromParent(); scanlineOverlay = nil
        noiseOverlay?.removeFromParent(); noiseOverlay = nil

        // Build scanline texture matching emulator drawable size
        if let tex = makeScanlineTexture(width: Int(size.width), height: Int(size.height), pitch: max(1, Int(currentCRTSettings().scanlinePitch))) {
            let node = SKSpriteNode(texture: tex, size: size)
            node.zPosition = 1
            // Use alpha blend to avoid total blackout at high intensity
            node.blendMode = .alpha
            // Slightly stronger than before but still safe
            node.alpha = CGFloat(min(1.0, currentCRTSettings().scanlineIntensity * 0.8))
            crtEffectRoot?.addChild(node)
            scanlineOverlay = node
        }
        // Build noise overlay (small texture, tiled by scaling)
        if let tex = makeNoiseTexture(width: 128, height: 128) {
            let node = SKSpriteNode(texture: tex, size: size)
            node.zPosition = 2
            node.blendMode = .alpha
            // Keep noise subtle; map slider to a smaller effective alpha
            node.alpha = CGFloat(currentCRTSettings().noiseIntensity * 0.25)
            crtEffectRoot?.addChild(node)
            noiseOverlay = node
        }
        syncOverlaysTransformToSprite()
    }

    private func makeScanlineTexture(width: Int, height: Int, pitch: Int) -> SKTexture? {
        guard width > 0 && height > 0 else { return nil }
        let bytesPerPixel = 4
        let count = width * height * bytesPerPixel
        var data = [UInt8](repeating: 0, count: count)
        for y in 0..<height {
            let isDark = ((y / max(1, pitch)) % 2) == 1
            let alpha: UInt8 = isDark ? 140 : 0  // a bit stronger, works well with alpha blend
            var idx = y * width * bytesPerPixel
            for _ in 0..<width {
                data[idx+0] = 0   // black
                data[idx+1] = 0
                data[idx+2] = 0
                data[idx+3] = alpha
                idx += 4
            }
        }
        var local = data // create mutable copy for provider lifetime
        let provider = CGDataProvider(data: NSData(bytes: &local, length: count))!
        let cgimg = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * bytesPerPixel,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        )
        return cgimg.map { SKTexture(cgImage: $0) }
    }

    private func makeNoiseTexture(width: Int, height: Int) -> SKTexture? {
        guard width > 0 && height > 0 else { return nil }
        let bytesPerPixel = 4
        let count = width * height * bytesPerPixel
        var data = [UInt8](repeating: 0, count: count)
        for i in 0..<(width*height) {
            let n: UInt8 = UInt8.random(in: 0...255)
            let base = i*4
            data[base+0] = n
            data[base+1] = n
            data[base+2] = n
            data[base+3] = 24  // lower per-pixel alpha; overall intensity comes from node alpha
        }
        var local = data
        let provider = CGDataProvider(data: NSData(bytes: &local, length: count))!
        let cgimg = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * bytesPerPixel,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        )
        return cgimg.map { SKTexture(cgImage: $0) }
    }

    private func updateNoiseOverlay() {
        guard let node = noiseOverlay else { return }
        // Refresh a small noise texture periodically for movement
        if let tex = makeNoiseTexture(width: 128, height: 128) {
            node.texture = tex
        }
    }

    private func syncOverlaysTransformToSprite() {
        guard crtEffectRoot != nil else { return }
        if let sl = scanlineOverlay {
            sl.position = spr.position
            sl.zRotation = spr.zRotation
            sl.xScale = spr.xScale
            sl.yScale = spr.yScale
        }
        if let nz = noiseOverlay {
            nz.position = spr.position
            nz.zRotation = spr.zRotation
            nz.xScale = spr.xScale
            nz.yScale = spr.yScale
        }
        if let cv = cornerVignetteOverlay {
            cv.position = spr.position
            cv.zRotation = spr.zRotation
            cv.xScale = spr.xScale
            cv.yScale = spr.yScale
        }
    }

    // Corner-only vignette overlay (darken only corners, not sides)
    private func rebuildCornerVignetteIfNeeded(size: CGSize, intensity: Float) {
        guard size != .zero else { return }
        // If intensity is effectively zero, remove overlay and exit
        if intensity <= 0.001 {
            cornerVignetteOverlay?.removeFromParent(); cornerVignetteOverlay = nil
            cornerVignetteCrop?.removeFromParent(); cornerVignetteCrop = nil
            cornerVignetteFill = nil
            lastCornerVignetteSize = .zero
            lastCornerVignetteIntensity = -1.0
            return
        }
        // Always rebuild on change for reliability
        lastCornerVignetteSize = size
        lastCornerVignetteIntensity = intensity

        cornerVignetteOverlay?.removeFromParent(); cornerVignetteOverlay = nil
        cornerVignetteCrop?.removeFromParent(); cornerVignetteCrop = nil
        cornerVignetteFill = nil

        // Build mask at moderate resolution and scale to fit
        let w = max(64, Int(size.width.rounded()))
        let h = max(64, Int(size.height.rounded()))
        if let tex = makeRadialVignetteTextureForMultiply(width: w, height: h, intensity: intensity) {
            let node = SKSpriteNode(texture: tex, size: size)
            node.zPosition = 500
            node.blendMode = .multiply
            node.alpha = 1.0
            self.addChild(node)
            cornerVignetteOverlay = node
            syncOverlaysTransformToSprite()
        }
    }

    // Mask for SKCropNode (alpha-only) - not used now
    private func makeCornerVignetteTexture(width: Int, height: Int, intensity: Float) -> SKTexture? {
        guard width > 0 && height > 0 else { return nil }
        let bytesPerPixel = 4
        let count = width * height * bytesPerPixel
        var data = [UInt8](repeating: 0, count: count)

        // Corner-only mask using product of 1D edge ramps (no darkening along edge centers)
        let k = Double(max(0.0, min(1.0, intensity)))
        let start = 0.60 - 0.15 * k   // 0.60..0.45 as intensity grows
        let end = 0.98                 // near absolute edge
        let falloff: Double = 1.6      // ramp curvature
        let maxAlpha = 220.0 * pow(k, 1.0) + 10.0

        func ramp(_ a: Double) -> Double {
            // Map |coord| in [0,1] to [0,1] between start..end with smooth curve
            let t = max(0.0, min(1.0, (a - start) / (end - start)))
            return pow(t, falloff)
        }

        for y in 0..<height {
            let ay = abs((Double(y) / Double(height - 1)) * 2.0 - 1.0)
            let vy = ramp(ay)
            for x in 0..<width {
                let ax = abs((Double(x) / Double(width - 1)) * 2.0 - 1.0)
                let ux = ramp(ax)
                let corner = ux * vy // only strong when both x and y are near edges (corners)
                // Crop mask uses alpha channel only; compute alpha increasing to corners
                let v = pow(corner, falloff)
                let a = UInt8(max(0.0, min(255.0, v * maxAlpha)))
                let idx = (y * width + x) * 4
                data[idx+0] = 255
                data[idx+1] = 255
                data[idx+2] = 255
                data[idx+3] = a
            }
        }
        var local = data
        let provider = CGDataProvider(data: NSData(bytes: &local, length: count))!
        let cgimg = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * bytesPerPixel,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: true,
            intent: .defaultIntent
        )
        return cgimg.map { SKTexture(cgImage: $0) }
    }

    // Brightness texture for multiply overlay: 1.0 center/edges, darker at corners
    private func makeRadialVignetteTextureForMultiply(width: Int, height: Int, intensity: Float) -> SKTexture? {
        guard width > 0 && height > 0 else { return nil }
        let bytesPerPixel = 4
        let count = width * height * bytesPerPixel
        var data = [UInt8](repeating: 0, count: count)

        let k = Double(max(0.0, min(1.0, intensity)))
        // Radial vignette: start radius closer to center as intensity grows a bit
        let start = 0.65 - 0.15 * k // 0.65..0.50
        let end = 0.98
        let falloff: Double = 1.8
        let maxDark = min(0.80, 0.55 * k + 0.10) // up to ~65% darkness

        for y in 0..<height {
            let ny = (Double(y) / Double(height - 1)) * 2.0 - 1.0 // -1..1
            for x in 0..<width {
                let nx = (Double(x) / Double(width - 1)) * 2.0 - 1.0
                // Elliptical radius to account for aspect ratio
                let r = sqrt(nx*nx + ny*ny)
                let t = max(0.0, min(1.0, (r - start) / (end - start)))
                let dark = pow(t, falloff) * maxDark
                let brightness = max(0.0, min(1.0, 1.0 - dark))
                let g = UInt8(brightness * 255.0)
                let idx = (y * width + x) * 4
                data[idx+0] = g
                data[idx+1] = g
                data[idx+2] = g
                data[idx+3] = 255
            }
        }
        var local = data
        let provider = CGDataProvider(data: NSData(bytes: &local, length: count))!
        let cgimg = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * bytesPerPixel,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: true,
            intent: .defaultIntent
        )
        return cgimg.map { SKTexture(cgImage: $0) }
    }

    // MARK: - Chromatic overlays
    private func rebuildChromaOverlaysIfNeeded(texture: SKTexture, size: CGSize) {
        if chromaRedNode != nil && chromaBlueNode != nil { return }
        // Red channel overlay
        let redSprite = SKSpriteNode(texture: texture, size: size)
        redSprite.anchorPoint = CGPoint(x: 0.5, y: 0.5)
        let redFx = SKEffectNode()
        redFx.shouldRasterize = true
        redFx.shouldEnableEffects = true
        if let m = CIFilter(name: "CIColorMatrix") {
            // Keep only red channel
            m.setDefaults()
            m.setValue(CIVector(x: 1, y: 0, z: 0, w: 0), forKey: "inputRVector")
            m.setValue(CIVector(x: 0, y: 0, z: 0, w: 0), forKey: "inputGVector")
            m.setValue(CIVector(x: 0, y: 0, z: 0, w: 0), forKey: "inputBVector")
            m.setValue(CIVector(x: 0, y: 0, z: 0, w: 1), forKey: "inputAVector")
            redFx.filter = m
        }
        redFx.addChild(redSprite)
        redFx.zPosition = 3
        redSprite.blendMode = .add
        crtEffectRoot?.addChild(redFx)
        chromaRedNode = redFx
        chromaRedSprite = redSprite

        // Blue channel overlay
        let blueSprite = SKSpriteNode(texture: texture, size: size)
        blueSprite.anchorPoint = CGPoint(x: 0.5, y: 0.5)
        let blueFx = SKEffectNode()
        blueFx.shouldRasterize = true
        blueFx.shouldEnableEffects = true
        if let m = CIFilter(name: "CIColorMatrix") {
            m.setDefaults()
            m.setValue(CIVector(x: 0, y: 0, z: 0, w: 0), forKey: "inputRVector")
            m.setValue(CIVector(x: 0, y: 0, z: 0, w: 0), forKey: "inputGVector")
            m.setValue(CIVector(x: 0, y: 0, z: 1, w: 0), forKey: "inputBVector")
            m.setValue(CIVector(x: 0, y: 0, z: 0, w: 1), forKey: "inputAVector")
            blueFx.filter = m
        }
        blueFx.addChild(blueSprite)
        blueFx.zPosition = 4
        blueSprite.blendMode = .add
        crtEffectRoot?.addChild(blueFx)
        chromaBlueNode = blueFx
        chromaBlueSprite = blueSprite

        applyChromaticSettings()
        syncChromaticTransforms()
    }

    private func applyChromaticSettings() {
        let s = currentCRTSettings()
        let maxShiftPx: CGFloat = 2.5
        let px = CGFloat(abs(s.chromaShiftR))
        let deadzone: CGFloat = 0.15 // ignore tiny values
        var t = (px - deadzone) / (maxShiftPx - deadzone)
        t = max(0.0, min(1.0, t))
        let shaped = pow(t, 1.3)

        // Use subtle alpha and tiny scale change; do NOT strip base colors to avoid color cast
        let scale = 1.0 + shaped * 0.006 // up to +0.6%
        chromaRedSprite?.xScale = spr.xScale * scale
        chromaRedSprite?.yScale = spr.yScale * scale
        chromaBlueSprite?.xScale = spr.xScale / scale
        chromaBlueSprite?.yScale = spr.yScale / scale

        let overlayAlpha = min(0.22, shaped * 0.25)
        chromaRedSprite?.alpha = overlayAlpha
        chromaBlueSprite?.alpha = overlayAlpha

        // Ensure base is untouched
        if let base = chromaBaseNode {
            base.filter = nil
            base.shouldEnableEffects = false
        }
    }

    private func syncChromaticTransforms() {
        guard let red = chromaRedNode, let blue = chromaBlueNode else { return }
        red.position = spr.position
        red.zRotation = spr.zRotation
        blue.position = spr.position
        blue.zRotation = spr.zRotation
    }

    private func updateChromaticOverlayTextures(_ texture: SKTexture) {
        chromaRedSprite?.texture = texture
        chromaRedSprite?.size = spr.size
        chromaBlueSprite?.texture = texture
        chromaBlueSprite?.size = spr.size
    }

    // MARK: - Superimpose helpers
    func loadBackgroundVideo(url: URL) {
        print("DEBUG: GameScene.loadBackgroundVideo called for: \(url.lastPathComponent)")
        errorLog("GameScene: Loading background video from \(url.lastPathComponent)", category: .ui)
        do {
            try superManager.loadVideo(url: url)
            print("DEBUG: superManager.loadVideo completed successfully")
            errorLog("GameScene: Video loading initiated successfully", category: .ui)
        } catch {
            print("DEBUG: superManager.loadVideo failed: \(error)")
            errorLog("Failed to load background video", error: error, category: .ui)
        }
    }
    func removeBackgroundVideo() { superManager.removeVideo() }
    func setSuperimposeEnabled(_ on: Bool) {
        superManager.setEnabled(on)
        if on {
            if let video = superManager.videoNode {
                if video.parent == nil {
                    addChild(video)
                    video.zPosition = -10
                }
                superManager.adjustVideoScale(matching: spr)
                superManager.play()
                // Don't show simple "ON" message here - osdUpdateSuperimpose will handle unified display
            }
        } else {
            superManager.pause()
            // Don't show simple "OFF" message here - osdUpdateSuperimpose will handle it
        }
        applySuperimposeUniforms()
        // Show unified OSD message for ON/OFF changes
        osdUpdateSuperimpose()
    }
    func setSuperimposeThreshold(_ v: Float) { superManager.setThreshold(v); applySuperimposeUniforms() }
    func setSuperimposeSoftness(_ v: Float) { superManager.setSoftness(v); applySuperimposeUniforms() }
    func setSuperimposeAlpha(_ v: Float) { superManager.setAlpha(v); applySuperimposeUniforms() }
    private func applySuperimposeUniforms() {
        let currentState = (superManager.settings.enabled, superManager.settings.threshold, superManager.settings.softness, superManager.settings.alpha)

        // Only log when values change significantly (simplified check)
        if currentState.0 != lastLoggedSuperState.0 || fabsf(currentState.1 - lastLoggedSuperState.1) > 0.001 || fabsf(currentState.2 - lastLoggedSuperState.2) > 0.001 || fabsf(currentState.3 - lastLoggedSuperState.3) > 0.001 {
            print("DEBUG: applySuperimposeUniforms - enabled: \(superManager.settings.enabled), threshold: \(superManager.settings.threshold), softness: \(superManager.settings.softness), alpha: \(superManager.settings.alpha)")
            lastLoggedSuperState = currentState
        }

        crtFilter.setSuperimpose(enabled: superManager.settings.enabled,
                                 threshold: superManager.settings.threshold,
                                 softness: superManager.settings.softness,
                                 alpha: superManager.settings.alpha)
        // When superimpose is enabled, hide full-screen overlays that would cover holes
        if superManager.settings.enabled {
            scanlineOverlay?.alpha = 0.0
            noiseOverlay?.alpha = 0.0
            cornerVignetteOverlay?.alpha = 0.0
            // Disable SKEffectNode filters so alpha from spr is preserved
            crtBloomNode?.filter = nil
            crtBloomNode?.shouldEnableEffects = false
            crtVignetteNode?.filter = nil
            crtVignetteNode?.shouldEnableEffects = false
            crtCurvatureNode?.filter = nil
            crtCurvatureNode?.shouldEnableEffects = false
            // Detach effect chain and place spr directly under scene to ensure alpha is honored
            if !superimposeDetached { detachCRTEffectChainForSuperimpose() }
        } else {
            // Restore overlay alphas from current CRT settings
            let s = currentCRTSettings()
            scanlineOverlay?.alpha = CGFloat(min(1.0, s.scanlineIntensity * 0.8))
            noiseOverlay?.alpha = CGFloat(min(1.0, s.noiseIntensity * 0.25))
            // corner vignette texture encodes intensity; leave alpha at 1.0 when present
            cornerVignetteOverlay?.alpha = 1.0
            // Re-apply CI filters when superimpose is disabled
            applyCIEffectsForCurrentCRT()
            if superimposeDetached { restoreCRTEffectChainAfterSuperimpose() }
        }
        // Ensure crop masking (video above spr only where dark). If disabled, remove crop.
        // Keep video aligned with sprite transform every frame
        if let video = superManager.videoNode {
            video.zPosition = -10
            superManager.adjustVideoScale(matching: spr)
        }
        // No persistent OSD needed
    }

    // MARK: - OSD for parameter changes
    private func showOSD(_ text: String) {
        let label = SKLabelNode(text: text)
        label.fontName = "Helvetica-Bold"
        label.fontSize = 16
        label.fontColor = .white
        label.zPosition = 990
        label.position = CGPoint(x: size.width/2 - 140, y: size.height/2 - 30)
        label.alpha = 0
        addChild(label)
        label.run(.sequence([.fadeIn(withDuration: 0.12), .wait(forDuration: 2.5), .fadeOut(withDuration: 0.25), .removeFromParent()]))
    }

    func osdUpdateSuperimpose() {
        let t = Int(round(superManager.settings.threshold*100))
        let s = Int(round(superManager.settings.softness*100))
        let a = Int(round(superManager.settings.alpha*100))
        _ = superManager.settings.enabled ? "ON" : "OFF"
        if superManager.settings.enabled {
            // Show temporary ON message with parameters
            showOSD("Superimpose ON  T:\(t)%  S:\(s)%  I:\(a)%")
        } else {
            // Ephemeral notice when turning OFF
            showOSD("Superimpose OFF  T:\(t)%  S:\(s)%  I:\(a)%")
        }
        // No persistent OSD used
    }


    // Crop masking overlay: reveal video only where spr is dark (backup path)
    private func ensureSuperimposeCrop() {
        guard let videoNode = superManager.videoNode else { return }
        // Create crop if needed
        if superCrop == nil {
            let crop = SKCropNode()
            crop.zPosition = spr.zPosition + 0.1
            let mask = SKSpriteNode(texture: spr.texture, size: spr.size)
            mask.anchorPoint = CGPoint(x: 0.5, y: 0.5)
            mask.colorBlendFactor = 0.0
            let src = """
            varying vec2 v_tex_coord;
            uniform sampler2D u_texture;
            uniform float u_th;
            uniform float u_soft;
            void main(){
                vec3 c = texture2D(u_texture, v_tex_coord).rgb;
                float l = dot(c, vec3(0.2126,0.7152,0.0722));
                float k = smoothstep(u_th, u_th+u_soft, l);
                float a = 1.0 - k; // dark -> a=1 (show video), bright -> a=0
                gl_FragColor = vec4(1.0,1.0,1.0, a);
            }
            """
            let shader = SKShader(source: src)
            shader.uniforms = [
                SKUniform(name: "u_th", float: superManager.settings.threshold),
                SKUniform(name: "u_soft", float: superManager.settings.softness)
            ]
            mask.shader = shader
            crop.maskNode = mask

            // Reparent video under crop and add crop above spr
            videoNode.removeFromParent()
            // Ensure video node size/scale aligns to scene/spr before adding
            videoNode.position = .zero
            videoNode.zPosition = 0
            videoNode.size = spr.size
            crop.addChild(videoNode)
            addChild(crop)
            superCrop = crop
            superMask = mask
            superMaskShader = shader
        }
        // Ensure the current video node is a child of crop
        if let crop = superCrop, videoNode.parent !== crop {
            videoNode.removeFromParent()
            videoNode.position = .zero
            videoNode.zPosition = 0
            videoNode.size = spr.size
            crop.addChild(videoNode)
        }
        // Update mask texture / uniforms / transform each frame
        superMask?.texture = spr.texture
        if let shader = superMaskShader {
            if let u = shader.uniformNamed("u_th") { u.floatValue = superManager.settings.threshold }
            if let u = shader.uniformNamed("u_soft") { u.floatValue = superManager.settings.softness }
        }
        superCrop?.position = spr.position
        superCrop?.zRotation = spr.zRotation
        superCrop?.xScale = spr.xScale
        superCrop?.yScale = spr.yScale
        // For crop overlay, keep video size equal to spr region
        videoNode.size = spr.size
    }

    private func detachCRTEffectChainForSuperimpose() {
        // Move spr out of effect chain
        if spr.parent !== self {
            spr.removeFromParent()
            addChild(spr)
            spr.zPosition = 0
            applySpriteTransformSilently()
        }

        // CRITICAL: Ensure CRT shader is fully initialized and applied to spr
        print("DEBUG: Applying CRT shader directly to spr for superimpose")

        // Force shader creation and uniform update before applying
        crtFilter.setSuperimpose(enabled: superManager.settings.enabled,
                                 threshold: superManager.settings.threshold,
                                 softness: superManager.settings.softness,
                                 alpha: superManager.settings.alpha)

        if let shader = crtFilter.getShader() {
            spr.shader = shader
            print("DEBUG: CRT shader applied to spr successfully with uniforms updated")
        } else {
            print("DEBUG: WARNING - No CRT shader available after forced update")
            spr.shader = nil
        }

        // Remove effect chain nodes
        crtBloomNode?.removeFromParent(); crtBloomNode = nil
        crtVignetteNode?.removeFromParent(); crtVignetteNode = nil
        crtCurvatureNode?.removeFromParent(); crtCurvatureNode = nil
        crtEffectRoot = nil
        superimposeDetached = true
    }

    private func restoreCRTEffectChainAfterSuperimpose() {
        // Remove shader from spr before putting it back in effect chain
        print("DEBUG: Restoring CRT effect chain, removing shader from spr")
        spr.shader = nil

        // Rebuild effect chain identical to creation path without changing texture
        let size = spr.size
        // Ensure spr is detached from any parent before reparenting
        if spr.parent != nil { spr.removeFromParent() }
        let bloomNode = SKEffectNode(); bloomNode.shouldRasterize = true; bloomNode.shouldEnableEffects = false; bloomNode.zPosition = 0; bloomNode.addChild(spr)
        let vignetteNode = SKEffectNode(); vignetteNode.shouldRasterize = true; vignetteNode.shouldEnableEffects = false; vignetteNode.zPosition = 0; vignetteNode.addChild(bloomNode)
        let curvatureNode = SKEffectNode(); curvatureNode.shouldRasterize = true; curvatureNode.shouldEnableEffects = false; curvatureNode.zPosition = 0; curvatureNode.addChild(vignetteNode)
        addChild(curvatureNode)
        crtBloomNode = bloomNode
        crtVignetteNode = vignetteNode
        crtCurvatureNode = curvatureNode
        crtEffectRoot = curvatureNode

        // Apply CRT shader to the appropriate node in the effect chain
        print("DEBUG: Applying CRT shader to effect chain")
        crtFilter.attach(to: spr)

        applyCIEffectsForCurrentCRT()
        rebuildOverlaysIfNeeded(size: size)
        superimposeDetached = false
    }

    // Expose read-only settings for menus/UI
    func isSuperimposeEnabled() -> Bool { superManager.settings.enabled }
    func getSuperimposeThreshold() -> Float { superManager.settings.threshold }
    func getSuperimposeSoftness() -> Float { superManager.settings.softness }
    func getSuperimposeAlpha() -> Float { superManager.settings.alpha }

    // MARK: - Persistence overlay
    private func rebuildPersistenceOverlayIfNeeded(size: CGSize) {
        if persistenceNode != nil { return }
        let node = SKSpriteNode(texture: nil, size: size)
        node.zPosition = 5
        node.blendMode = .alpha
        node.alpha = 0.0
        crtEffectRoot?.addChild(node)
        persistenceNode = node
    }

    private func updatePersistenceOverlay(with currentTexture: SKTexture) {
        let s = currentCRTSettings()
        let shaped = pow(CGFloat(max(0.0, min(1.0, s.phosphorPersistence))), 1.1)
        persistenceNode?.alpha = shaped * 0.95
        if let last = lastFrameTexture {
            persistenceNode?.texture = last
            persistenceNode?.size = spr.size
        }
        // Update last frame for next draw
        lastFrameTexture = currentTexture
    }
    
    override func didFinishUpdate() {
        // Ensure all texture updates are completed before next frame
        super.didFinishUpdate()
    }
    
    // MARK: - UI Layout Management
    private func adjustMIDILabelPosition() {
        // Move MIDI label to the right side to avoid overlap with input mode button
        labelMIDI?.position = CGPoint(x: size.width/2 - 150, y: size.height/2 - 50)
        labelMIDI?.horizontalAlignmentMode = .right
        labelMIDI?.fontColor = .cyan
        labelMIDI?.fontSize = 18
    }
    
    private func hideIOSLegacyUI() {
        debugLog("Hiding iOS legacy UI elements after startup...", category: .ui)
        
        // Hide the iOS legacy Settings gear icon
        if let settingsNode = self.childNode(withName: "//Settings") as? SKSpriteNode {
            settingsNode.isHidden = true
            settingsNode.removeFromParent()
            debugLog("Settings gear icon removed", category: .ui)
        }
        
        // Mark title labels as non-interactive to prevent click activation
        // but don't remove them so they can still be controlled by existing fade animations
        if let titleLabel = self.label {
            titleLabel.isUserInteractionEnabled = false
            debugLog("Main title label interaction disabled", category: .ui)
        }
        
        // Disable interaction for all title-related labels
        let titleNodeNames = ["//helloLabel", "//helloLabel2", "//labelTitle", "//labelTitle2"]
        for nodeName in titleNodeNames {
            if let node = self.childNode(withName: nodeName) {
                node.isUserInteractionEnabled = false
                debugLog("Disabled interaction for title node: \(nodeName)", category: .ui)
            }
        }
        
        // Find and disable interaction for any labels containing title text
        self.enumerateChildNodes(withName: "//*") { node, _ in
            if let labelNode = node as? SKLabelNode {
                if let text = labelNode.text {
                    if text.contains("POWER TO MAKE") || text.contains("for macOS") || text.contains("for iOS") {
                        labelNode.isUserInteractionEnabled = false
                        debugLog("Disabled interaction for title text node: \(node.name ?? "unknown")", category: .ui)
                    }
                }
            }
        }
        
        // Reduce visibility of MIDI label for cleaner macOS experience
        labelMIDI?.alpha = 0.3
        labelMIDI?.fontSize = 12
        debugLog("MIDI label visibility reduced", category: .ui)
    }
    
    private func hideTitleLogo() {
        // Immediately hide the title logo when clock is changed
        titleSprite?.removeAllActions()
        titleSprite?.alpha = 0.0
        titleSprite?.isHidden = true
        
        // Also hide any title text labels that might be displayed
        hideTitleLabels()
    }
    
    private func hideTitleLabels() {
        // Hide all potential title-related labels
        if let titleLabel = self.childNode(withName: "//labelTitle") as? SKLabelNode {
            titleLabel.removeAllActions()
            titleLabel.alpha = 0.0
            titleLabel.isHidden = true
        }
        
        // Check for other common title label names
        let titleLabelNames = ["//helloLabel2", "//labelTitle2", "//titleText", "//subtitleText"]
        for labelName in titleLabelNames {
            if let label = self.childNode(withName: labelName) as? SKLabelNode {
                label.removeAllActions()
                label.alpha = 0.0
                label.isHidden = true
            }
        }
        
        // Find any labels containing title text and hide them
        self.enumerateChildNodes(withName: "//*") { node, _ in
            if let labelNode = node as? SKLabelNode {
                if let text = labelNode.text {
                    if text.contains("POWER TO MAKE") || text.contains("for macOS") || text.contains("for iOS") {
                        labelNode.removeAllActions()
                        labelNode.alpha = 0.0
                        labelNode.isHidden = true
                    }
                }
            }
        }
    }
    
    // MARK: - Animation Disabling
    private func disableAllAnimationsCompletely() {
        // Remove all actions from the scene itself
        self.removeAllActions()
        
        // Find and disable any UI elements that might have animations from the SKS file
        let potentialAnimatedNodes = [
            "//helloLabel", "//helloLabel2", "//labelTitle", "//labelTitle2",
            "//titleText", "//subtitleText", "//X68000LogoW", "//logo",
            "//spinner", "//spinny", "//Settings"
        ]
        
        for nodeName in potentialAnimatedNodes {
            if let node = self.childNode(withName: nodeName) {
                // Immediately remove all actions and hide the node
                node.removeAllActions()
                node.alpha = 0.0
                node.isHidden = true
                node.removeFromParent()
                debugLog("Removed animated node: \(nodeName)", category: .ui)
            }
        }
        
        // Enumerate all child nodes and disable any potential animations
        self.enumerateChildNodes(withName: "//*") { node, _ in
            node.removeAllActions()
            
            // Hide any nodes that might contain title text or be animated
            if let labelNode = node as? SKLabelNode {
                if let text = labelNode.text {
                    if text.contains("POWER TO MAKE") || text.contains("for macOS") || 
                       text.contains("for iOS") || text.contains("X68000") ||
                       text.contains("Hello") || text.isEmpty {
                        labelNode.removeAllActions()
                        labelNode.alpha = 0.0
                        labelNode.isHidden = true
                        debugLog("Disabled animated label: '\(text)'", category: .ui)
                    }
                }
            }
            
            // Hide any sprite nodes that might be logos or animated elements
            if let spriteNode = node as? SKSpriteNode {
                if let textureName = spriteNode.texture?.description {
                    if textureName.contains("Logo") || textureName.contains("X68000") {
                        spriteNode.removeAllActions()
                        spriteNode.alpha = 0.0
                        spriteNode.isHidden = true
                        debugLog("Disabled animated sprite: \(textureName)", category: .ui)
                    }
                }
            }
        }
        
        infoLog("All startup animations completely disabled", category: .ui)
    }
    
    // MARK: - Layer Z-Position Management
    private func setupLayerZPositions() {
        // Set unique zPosition values for all layers to ensure consistent draw order
        spr.zPosition = 0                    // Emulator screen (bottom layer)
        virtualPad.zPosition = 100           // Virtual pad controls
        
        // UI elements
        labelStatus?.zPosition = 200
        labelMIDI?.zPosition = 200
        inputModeButton?.zPosition = 200
        
        // Title elements (temporary, fade out)
        titleSprite?.zPosition = 300
        label?.zPosition = 300
        
        // Notifications and temporary overlays
        // (Dynamic elements will use zPosition 1000+)
        
        infoLog("Layer zPositions configured for consistent draw order", category: .ui)
    }
    
    // MARK: - Input Mode Management
    private func setupInputModeButton() {
        inputModeButton = SKLabelNode(text: getInputModeText())
        inputModeButton?.fontName = "Helvetica"
        inputModeButton?.fontSize = 14
        inputModeButton?.fontColor = .lightGray
        inputModeButton?.name = "InputModeButton"
        inputModeButton?.zPosition = 10
        inputModeButton?.position = CGPoint(x: -size.width/2 + 150, y: size.height/2 - 30)
        
        // Add background for better visibility
        let background = SKShapeNode(rect: CGRect(x: -45, y: -8, width: 90, height: 16))
        background.fillColor = .black
        background.strokeColor = .darkGray
        background.alpha = 0.6
        background.zPosition = -1
        inputModeButton?.addChild(background)
        
        addChild(inputModeButton!)
    }
    
    private func getInputModeText() -> String {
        switch currentInputMode {
        case .keyboard:
            return "MODE: KEYBOARD"
        case .joycard:
            return "MODE: JOYCARD"
        }
    }
    
    func toggleInputMode() {
        // Save current clock setting before mode change
        let savedClockMHz = self.clockMHz
        
        currentInputMode = (currentInputMode == .keyboard) ? .joycard : .keyboard
        inputModeButton?.text = getInputModeText()
        updateVirtualPadVisibility()
        
        // Restore clock setting to prevent unwanted changes
        self.clockMHz = savedClockMHz
        
        // Show mode change notification
        showModeChangeNotification()
    }
    
    private func updateVirtualPadVisibility() {
        #if os(iOS)
        let shouldHide = (currentInputMode == .keyboard)
        if virtualPad.isHidden != shouldHide {
            virtualPad.isHidden = shouldHide
            
            // Disable joystick updates when hidden to save CPU
            if shouldHide {
                moveJoystick.isUserInteractionEnabled = false
                rotateJoystick.isUserInteractionEnabled = false
                rotateJoystick2.isUserInteractionEnabled = false
            } else {
                moveJoystick.isUserInteractionEnabled = true
                rotateJoystick.isUserInteractionEnabled = true
                rotateJoystick2.isUserInteractionEnabled = true
            }
        }
        #endif
    }
    
    private func showModeChangeNotification() {
        let notification = SKLabelNode(text: getInputModeText())
        notification.fontName = "Helvetica-Bold"
        notification.fontSize = 36
        notification.fontColor = .yellow
        notification.zPosition = 1000  // High zPosition for overlays
        notification.position = CGPoint(x: 0, y: 0)
        notification.alpha = 0
        
        // Animation disabled - immediately show and hide notification  
        notification.alpha = 1.0
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) {
            notification.removeFromParent()
        }
        addChild(notification)
    }
}

#if os(iOS) || os(tvOS)
extension GameScene {
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        // If CRT settings overlay is open, forward input to overlay
        if let overlay = crtOverlay, let touch = touches.first {
            overlay.receivePointFromScene(touch.location(in: self), in: self)
            return
        }
        // Check for input mode button tap
        if let touch = touches.first {
            let location = touch.location(in: self)
            let touchedNode = atPoint(location)
            if touchedNode.name == "InputModeButton" {
                toggleInputMode()
                return
            }
        }
        
        // Handle joycard input only in joycard mode
        if currentInputMode == .joycard {
            for device in devices {
                device.touchesBegan(touches)
            }
        }
        
        if touches.count == 1 {
            if let touch = touches.first as UITouch? {
                let location = touch.location(in: self)
                let t = self.atPoint(location)
                debugLog("Touch target: \(t.name ?? "No name")", category: .input)
                if t.name == "LButton" {
                    if let shapeNode = t as? SKShapeNode {
                        shapeNode.fillColor = .yellow
                    }
                    mouseController?.Click(0, true)
                } else if t.name == "RButton" {
                    if let shapeNode = t as? SKShapeNode {
                        shapeNode.fillColor = .yellow
                    }
                    mouseController?.Click(1, true)
                } else if t.name == "MouseBody" {
                    mouseController?.ResetPosition(location, scene!.size)
                } else {
                    mouseController?.Click(0, true)
                    mouseController?.ResetPosition(location, scene!.size)
                }
            }
        }
        for t in touches {
            let location = t.location(in: self)
            // Only create spinny for clock control area (upper area, excluding mode button area)
            let modeButtonX = -size.width/2 + 150
            let modeButtonY = size.height/2 - 30
            let distanceFromModeButton = sqrt(pow(location.x - modeButtonX, 2) + pow(location.y - modeButtonY, 2))
            
            if location.y > 400.0 && distanceFromModeButton >= 100.0 {
                // self.makeSpinny(at: location, color: SKColor.green) // Removed to eliminate green square
            }
            break
        }
    }
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        if let overlay = crtOverlay, let touch = touches.first {
            overlay.receivePointFromScene(touch.location(in: self), in: self)
            return
        }
        // Handle joycard input only in joycard mode
        if currentInputMode == .joycard {
            for device in devices {
                device.touchesMoved(touches)
            }
        }
        
        if touches.count == 1 {
            if let touch = touches.first as UITouch? {
                let location = touch.location(in: self)
                let t = self.atPoint(location)
                if t.name == "MouseBody" {
                    mouseController?.SetPosition(location, scene!.size)
                } else {
                    mouseController?.SetPosition(location, scene!.size)
                }
            }
        }
    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        if let overlay = crtOverlay, let touch = touches.first {
            overlay.receivePointFromScene(touch.location(in: self), in: self)
            return
        }
        // Handle joycard input only in joycard mode
        if currentInputMode == .joycard {
            for device in devices {
                device.touchesEnded(touches)
            }
        }
        
        if let touch = touches.first as UITouch? {
            let location = touch.location(in: self)
            let t = self.atPoint(location)
            debugLog("Touch target: \(t.name ?? "No name")", category: .input)
            if t.name == "LButton" {
                if let shapeNode = t as? SKShapeNode {
                    shapeNode.fillColor = .black
                }
                mouseController?.Click(0, false)
            } else if t.name == "RButton" {
                if let shapeNode = t as? SKShapeNode {
                    shapeNode.fillColor = .black
                }
                mouseController?.Click(1, false)
            } else if t.name == "MouseBody" {
                let _ = Float(location.x)
                let _ = Float(location.y)
            } else {
                mouseController?.Click(0, false)
            }
        }
    }
    
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        debugLog("Cancelled", category: .input)
    }
}
#endif

#if os(OSX)
extension GameScene {
    override func mouseDown(with event: NSEvent) {
        if let overlay = crtOverlay {
            overlay.receivePointFromScene(event.location(in: self), in: self)
            return
        }
        let location = event.location(in: self)
        
        // Check for input mode button click first (with broader area)
        let modeButtonX = -size.width/2 + 150
        let modeButtonY = size.height/2 - 30
        let distanceFromModeButton = sqrt(pow(location.x - modeButtonX, 2) + pow(location.y - modeButtonY, 2))
        
        if distanceFromModeButton < 60.0 {  // Larger click area for mode button
            toggleInputMode()
            return
        }
        
        // Also check by node name as backup
        let clickedNode = atPoint(location)
        if clickedNode.name == "InputModeButton" {
            toggleInputMode()
            return
        }
        
        // Joycard mouse click handling only in joycard mode
        if currentInputMode == .joycard {
            joycard?.handleMouseClick(at: location, pressed: true)
        }
        
        // Clock control area disabled - no title label activation on click
        // Spinny and title pulse animations have been disabled for cleaner macOS experience
    }
    
    override func mouseDragged(with event: NSEvent) {
        if let overlay = crtOverlay {
            overlay.receivePointFromScene(event.location(in: self), in: self)
            return
        }
        // Spinny animation disabled for cleaner macOS experience
    }
    
    override func mouseUp(with event: NSEvent) {
        if let overlay = crtOverlay {
            overlay.receivePointFromScene(event.location(in: self), in: self)
            return
        }
        let location = event.location(in: self)
        
        // Joycard mouse click handling only in joycard mode
        if currentInputMode == .joycard {
            joycard?.handleMouseClick(at: location, pressed: false)
        }
    }
    
    override func mouseMoved(with event: NSEvent) {
        // In capture mode, GameViewController provides raw deltas and recenters.
        // Avoid duplicate/inverted updates from Scene-level handler.
        guard let mouseController = mouseController else { return }
        if mouseController.isCaptureMode { return }
        // Non-capture movement is intentionally ignored to avoid visual drift.
    }
    
    // MARK: - Mouse Capture Management
    
    func enableMouseCapture() {
        // debugLog("GameScene: Enabling mouse capture mode", category: .input)
        mouseController?.enableCaptureMode()
        infoLog("Mouse capture mode enabled in GameScene", category: .input)
    }
    
    func disableMouseCapture() {
        // debugLog("GameScene: Disabling mouse capture mode", category: .input)
        mouseController?.disableCaptureMode()
        infoLog("Mouse capture mode disabled in GameScene", category: .input)
    }

    // Force-release any held mouse buttons (safety when exiting capture)
    func releaseAllMouseButtons() {
        guard let mc = mouseController else { return }
        if mc.button_state != 0 {
            mc.button_state = 0
            #if os(macOS)
            mc.sendButtonOnlyUpdate()
            #endif
        }
    }
    
    
    override func keyDown(with event: NSEvent) {
        // print("🔍 key press: keyCode=\(event.keyCode) characters='\(event.characters ?? "nil")' shift=\(event.modifierFlags.contains(.shift))")
        
        // F1 key mode switching disabled - F1 now available for X68000 use
        
        // macOS-like shortcut: Toggle X68000 Mouse capture with Shift-Command-M
        if event.modifierFlags.contains([.command, .shift]),
           let ch = event.charactersIgnoringModifiers?.lowercased(), ch == "m" {
            if let appDelegate = NSApplication.shared.delegate as? AppDelegate {
                appDelegate.toggleMouseMode(self)
            }
            return
        }

        // Check for mouse capture mode exit (F12 key)
        if event.keyCode == 111 { // F12 key
            // Check if mouse capture is currently enabled
            if let mouseController = mouseController, mouseController.isCaptureMode {
                // debugLog("F12 pressed - disabling mouse capture mode", category: .input)
                // Call AppDelegate to disable mouse capture
                if let appDelegate = NSApplication.shared.delegate as? AppDelegate {
                    appDelegate.disableMouseCapture()
                }
            }
            return
        }
        
        // Mode-specific input handling
        switch currentInputMode {
        case .joycard:
            // Joycard input handling for macOS
            joycard?.handleKeyDown(event.keyCode, true)
            if let characters = event.characters, !characters.isEmpty {
                joycard?.handleCharacterInput(String(characters.first!), true)
            }
        case .keyboard:
            // X68000 keyboard input handling
            handleX68KeyboardInput(event, isKeyDown: true)
        }
    }
    
    private func handleX68KeyboardInput(_ event: NSEvent, isKeyDown: Bool) {
        // シフトキーの状態をチェックして、状態変化があれば送信
        let isShiftPressed = event.modifierFlags.contains(.shift)
        // print("🐛 Shift key pressed: \(isShiftPressed)")
        
        if isKeyDown {
            if isShiftPressed && !isShiftKeyPressed {
                // シフトキーが押された
                // print("🐛 Sending SHIFT DOWN")
                X68000_Key_Down(0x1e1) // KeyTable拡張領域のシフトキー
                isShiftKeyPressed = true
            } else if !isShiftPressed && isShiftKeyPressed {
                // シフトキーが離された
                // print("🐛 Sending SHIFT UP")
                X68000_Key_Up(0x1e1)
                isShiftKeyPressed = false
            }
        } else {
            if !isShiftPressed && isShiftKeyPressed {
                // シフトキーが離された
                // print("🐛 Sending SHIFT UP")
                X68000_Key_Up(0x1e1)
                isShiftKeyPressed = false
            }
        }
        
        // 最初に特殊キー（文字なし）をチェック
        let x68KeyTableIndex = getMacKeyToX68KeyTableIndex(event.keyCode)
        if x68KeyTableIndex != 0 {
            // print("🐛 Using special key KeyTable index: \(x68KeyTableIndex) for keyCode: \(event.keyCode)")
            if isKeyDown {
                X68000_Key_Down(x68KeyTableIndex)
            } else {
                X68000_Key_Up(x68KeyTableIndex)
            }
            return
        }

        // Use character-based processing to handle shift key combinations correctly
        if let characters = event.characters,
           let char = characters.first,
           let ascii = char.asciiValue,
           ascii >= 32 && ascii <= 126 { // printable ASCII range
            // print("🟢 Using character mapping: '\(char)' ASCII: \(ascii) for keyCode: \(event.keyCode)")
            if isKeyDown {
                X68000_Key_Down(UInt32(ascii))
            } else {
                X68000_Key_Up(UInt32(ascii))
            }
            return
        }

        // フォールバック: 物理キーベースのマッピング（修飾キーに依存しない基本文字）
        if let baseChar = getBaseCharacterForKeyCode(event.keyCode),
           let ascii = baseChar.asciiValue {
            // print("🐛 Using physical key mapping: '\(baseChar)' ASCII: \(ascii) for keyCode: \(event.keyCode)")
            if isKeyDown {
                X68000_Key_Down(UInt32(ascii))
            } else {
                X68000_Key_Up(UInt32(ascii))
            }
            return
        }
        
        // print("🐛 Unmapped macOS keyCode: \(event.keyCode)")
    }
    
    override func keyUp(with event: NSEvent) {
        // Skip Tab key for mode toggle - DISABLED
        // if event.keyCode == 48 { // Tab key
        //     return
        // }
        
        // Mode-specific input handling
        switch currentInputMode {
        case .joycard:
            // Joycard input handling for macOS
            joycard?.handleKeyDown(event.keyCode, false)
            if let characters = event.characters, !characters.isEmpty {
                joycard?.handleCharacterInput(String(characters.first!), false)
            }
        case .keyboard:
            // X68000 keyboard input handling
            handleX68KeyboardInput(event, isKeyDown: false)
        }
    }
    
    // 特殊キー（文字以外）用のマッピング
    func getMacKeyToX68KeyTableIndex(_ keyCode: UInt16) -> UInt32 {
        switch keyCode {
        // 特殊キー（ASCII値）
        case 36: return 0x0d  // Return (ASCII CR)
        case 49: return 0x20  // Space (ASCII Space)
        case 51: return 0x08  // Backspace (ASCII BS)
        case 48: return 0x09  // Tab (ASCII Tab)
        case 53: return 0x1b  // Escape (ASCII ESC)
        
        // 矢印キー（KeyTable拡張領域、0x100以降）
        case 126: return 0x111 // Up (keyboard.c:138)
        case 125: return 0x112 // Down (keyboard.c:138)
        case 124: return 0x113 // Right (keyboard.c:138)
        case 123: return 0x114 // Left (keyboard.c:138)
        
        // ファンクションキー（KeyTable拡張領域）
        case 122: return 0x163 // F1
        case 120: return 0x164 // F2
        case 99: return 0x165  // F3
        case 118: return 0x166 // F4
        case 96: return 0x167  // F5
        case 97: return 0x168  // F6
        case 98: return 0x169  // F7
        case 100: return 0x16a // F8
        case 101: return 0x16b // F9
        case 109: return 0x16c // F10
                
        default:
            return 0 // マッピングなし（文字キーは上位で処理）
        }
    }
    
    // macOSキーコードから物理キーの基本文字（シフトなし状態）を取得
    func getBaseCharacterForKeyCode(_ keyCode: UInt16) -> Character? {
        switch keyCode {
        // アルファベット（常に小文字で返す）
        case 0: return "a"
        case 11: return "b"
        case 8: return "c"
        case 2: return "d"
        case 14: return "e"
        case 3: return "f"
        case 5: return "g"
        case 4: return "h"
        case 34: return "i"
        case 38: return "j"
        case 40: return "k"
        case 37: return "l"
        case 46: return "m"
        case 45: return "n"
        case 31: return "o"
        case 35: return "p"
        case 12: return "q"
        case 15: return "r"
        case 1: return "s"
        case 17: return "t"
        case 32: return "u"
        case 9: return "v"
        case 13: return "w"
        case 7: return "x"
        case 16: return "y"
        case 6: return "z"
        
        // 数字（シフトなし状態）- 修正版
        case 18: return "1"  // 1キー
        case 19: return "2"  // 2キー  
        case 20: return "3"  // 3キー
        case 21: return "4"  // 4キー
        case 23: return "5"  // 5キー
        case 22: return "6"  // 6キー
        case 26: return "7"  // 7キー
        case 28: return "8"  // 8キー
        case 25: return "9"  // 9キー
        case 29: return "0"  // 0キー
        
        // 記号（シフトなし状態）- US keyboard layout
        case 27: return "="  // = key (US keyboard)
        case 24: return "-"  // - key (US keyboard)
        case 33: return "["
        case 30: return "]"
        case 42: return "\\"
        case 41: return ";"  // semicolon key (US keyboard)
        case 39: return "'"  // apostrophe key (US keyboard)
        case 43: return ","
        case 47: return "."
        case 44: return "/"
        
        default:
            return nil
        }
    }
}
#endif
