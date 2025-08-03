//
//  GameScene.swift
//  X68000 Shared
//
//  Created by GOROman on 2020/03/28.
//  Copyright 2020 GOROman. All rights reserved.
//

import SpriteKit
import GameController
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
    
    private var audioStream: AudioStream?
    private var mouseController: X68MouseController?
    private var midiController: MIDIController = MIDIController()
    
    private var devices: [X68Device] = []
    var fileSystem: FileSystem?
    
    // Track currently loading files to prevent duplicate loads
    private static var currentlyLoadingFiles: Set<String> = []
    
    let moveJoystick = TLAnalogJoystick(withDiameter: 200)
    let rotateJoystick = TLAnalogJoystick(withDiameter: 120)
    let rotateJoystick2 = TLAnalogJoystick(withDiameter: 120)
    
    class func newGameScene() -> GameScene {
        
        func buttonHandler() -> GCControllerButtonValueChangedHandler {
            return { (_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
                print("A!")  // 
            }
        }
        
        guard let scene = GameScene(fileNamed: "GameScene") else {
            print("Failed to load GameScene.sks")
            abort()
        }
        
        scene.scaleMode = .aspectFit
        scene.backgroundColor = .black
        return scene
    }
    
    var count = 0
    
    func controller_event(status: JoyController.Status) {
        print(status)
        var msg = ""
        if status == .Connected {
            msg = "Controller Connected"
        } else if status == .Disconnected {
            msg = "Controller Disconnected"
        }
        if let t = self.labelStatus {
            t.text = msg
            if let notifyAction = SKAction(named: "Notify") {
                t.run(notifyAction, withKey: "fadeInOut")
            }
        }
    }
    
    func load(url: URL) {
        print("🐛 GameScene.load() called with: \(url.lastPathComponent)")
        let urlPath = url.path
        
        // Check if we're already loading this exact file
        if GameScene.currentlyLoadingFiles.contains(urlPath) {
            print("Already loading file: \(url.lastPathComponent), skipping")
            return
        }
        
        // Mark this file as loading immediately to prevent duplicates
        GameScene.currentlyLoadingFiles.insert(urlPath)
        
        Benchmark.measure("load", block: {
            print("🐛 Starting Benchmark.measure for file: \(url.lastPathComponent)")
            let imagefilename = url.deletingPathExtension().lastPathComponent.removingPercentEncoding
            
            let node = SKLabelNode()
            node.fontSize = 64
            node.position.y = -350
            node.text = imagefilename
            
            node.zPosition = 4.0
            node.alpha = 0
            node.run(SKAction.sequence([SKAction.fadeIn(withDuration: 0.2),
                                        SKAction.wait(forDuration: 0.5),
                                        SKAction.fadeOut(withDuration: 0.5),
                                        SKAction.removeFromParent()]))
            self.addChild(node)
            
            if self.fileSystem == nil {
                print("🐛 Creating new FileSystem instance")
                self.fileSystem = FileSystem()
                self.fileSystem?.gameScene = self  // Set reference for cleanup
            }
            print("🐛 Calling FileSystem.loadDiskImage() with: \(url.lastPathComponent)")
            self.fileSystem?.loadDiskImage(url)
        })
    }
    
    // Method to clear loading file after completion
    func clearLoadingFile(_ url: URL) {
        GameScene.currentlyLoadingFiles.remove(url.path)
    }
    
    // MARK: - FDD Management
    func loadFDDToDrive(url: URL, drive: Int) {
        print("🐛 GameScene.loadFDDToDrive() called with: \(url.lastPathComponent) to drive \(drive)")
        
        if fileSystem == nil {
            print("🐛 Creating new FileSystem instance for FDD")
            fileSystem = FileSystem()
            fileSystem?.gameScene = self
        }
        
        fileSystem?.loadFDDToDrive(url, drive: drive)
    }
    
    func ejectFDDFromDrive(_ drive: Int) {
        print("🐛 GameScene.ejectFDDFromDrive() called for drive \(drive)")
        X68000_EjectFDD(drive)
    }
    
    // MARK: - HDD Management
    func loadHDD(url: URL) {
        print("🐛 GameScene.loadHDD() called with: \(url.lastPathComponent)")
        print("🐛 File extension: \(url.pathExtension)")
        print("🐛 Full path: \(url.path)")
        
        // Direct HDD loading to avoid complex FileSystem routing that may fail in TestFlight
        let extname = url.pathExtension.lowercased()
        
        // Check if this is a valid HDD file
        if extname == "hdf" {
            print("🔧 Loading HDD file directly: \(extname.uppercased())")
            
            do {
                let imageData = try Data(contentsOf: url)
                print("🔧 Successfully read HDD data: \(imageData.count) bytes")
                
                // Security: Validate HDD file size (reasonable limit for hard disk images)
                let maxSize = 2 * 1024 * 1024 * 1024 // 2GB max
                guard imageData.count <= maxSize else {
                    print("❌ HDD file too large: \(imageData.count) bytes")
                    return
                }
                
                DispatchQueue.main.async {
                    if let p = X68000_GetDiskImageBufferPointer(4, imageData.count) {
                        imageData.copyBytes(to: p, count: imageData.count)
                        X68000_LoadHDD(url.absoluteString)
                        print("✅ HDD loaded successfully: \(url.lastPathComponent)")
                        
                        // Save SRAM after HDD loading
                        self.fileSystem?.saveSRAM()
                    } else {
                        print("❌ Failed to get HDD buffer pointer")
                    }
                }
            } catch {
                print("❌ Error reading HDD file: \(error)")
            }
        } else {
            print("❌ Invalid HDD file extension: \(extname)")
            print("❌ Expected: hdf")
        }
    }
    
    func ejectHDD() {
        print("🐛 GameScene.ejectHDD() called")
        X68000_EjectHDD()
    }
    
    // MARK: - Clock Management
    func setCPUClock(_ mhz: Int) {
        print("🐛 GameScene.setCPUClock() called with: \(mhz) MHz")
        
        // Clamp clock speed to safe range to prevent integer overflow in emulator core
        let safeMHz = max(1, min(mhz, 50))  // Limit to 50MHz maximum
        if safeMHz != mhz {
            print("🐛 Clock speed clamped from \(mhz) MHz to \(safeMHz) MHz for stability")
        }
        
        clockMHz = safeMHz
        
        // Save to UserDefaults
        userDefaults.set("\(safeMHz)", forKey: "clock")
        
        // Show visual feedback
        showClockChangeNotification(safeMHz)
        
        print("🐛 CPU Clock set to: \(clockMHz) MHz")
    }
    
    private func showClockChangeNotification(_ mhz: Int) {
        let notification = SKLabelNode(text: "\(mhz) MHz")
        notification.fontName = "Helvetica-Bold"
        notification.fontSize = 48
        notification.fontColor = .yellow
        notification.zPosition = 15
        notification.position = CGPoint(x: 0, y: 0)
        notification.alpha = 0
        
        let fadeSequence = SKAction.sequence([
            SKAction.fadeIn(withDuration: 0.3),
            SKAction.wait(forDuration: 1.2),
            SKAction.fadeOut(withDuration: 0.5),
            SKAction.removeFromParent()
        ])
        
        notification.run(fadeSequence)
        addChild(notification)
        
        // Hide title logo when clock is changed
        hideTitleLogo()
    }
    
    // MARK: - System Management
    func resetSystem() {
        print("🐛 GameScene.resetSystem() called - performing manual reset")
        X68000_Reset()
        print("🐛 System reset completed")
    }
    
    // MARK: - Screen Rotation Management
    func rotateScreen() {
        print("🐛 GameScene.rotateScreen() called")
        // 次の回転状態に切り替え
        let allRotations = ScreenRotation.allCases
        if let currentIndex = allRotations.firstIndex(of: currentRotation) {
            let nextIndex = (currentIndex + 1) % allRotations.count
            currentRotation = allRotations[nextIndex]
        }
        
        applyScreenRotation()
    }
    
    func setScreenRotation(_ rotation: ScreenRotation) {
        print("🐛 GameScene.setScreenRotation() called with: \(rotation.displayName)")
        currentRotation = rotation
        applyScreenRotation()
    }
    
    private func applyScreenRotation() {
        print("🐛 Applying screen rotation: \(currentRotation.displayName)")
        
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
        
        print("🐛 Applied rotation: \(currentRotation.displayName), scale: \(scaleX)x\(scaleY), scene: \(sceneSize)")
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
        if let clock = userDefaults.object(forKey: "clock") as? String {
            self.clockMHz = Int(clock)!
            print("CPU Clock: \(self.clockMHz) MHz")
        }
        
        // 画面回転設定の読み込み - 起動時は常に横画面に強制
        currentRotation = .landscape
        print("Screen rotation forced to Landscape on startup")
        
        if let virtual_mouse = userDefaults.object(forKey: "virtual_mouse") as? Bool {
            print("virtual_mouse: \(virtual_mouse)")
        }
        if let virtual_pad = userDefaults.object(forKey: "virtual_pad") as? Bool {
            print("virtual_pad: \(virtual_pad)")
            virtualPad.isHidden = !virtual_pad
        }
        if let sample = userDefaults.object(forKey: "samplingrate") as? String {
            self.samplingRate = Int(sample)!
            print("Sampling Rate: \(self.samplingRate) Hz")
        }
        if let fps = userDefaults.object(forKey: "fps") as? String {
            let fpsValue = Int(fps)!
            view?.preferredFramesPerSecond = fpsValue
            print("FPS: \(fpsValue) Hz")
        }
        if let vsync = userDefaults.object(forKey: "vsync") as? Bool {
            self.vsync = vsync
            print("V-Sync: \(vsync)")
        }
    }
    
    func setUpScene() {
        settings()
        
        self.fileSystem = FileSystem()
        self.fileSystem?.gameScene = self  // Set reference for timer management
        self.fileSystem?.loadIPLROM()
        self.fileSystem?.loadCGROM()
        
        X68000_Init(samplingRate)
        
        self.fileSystem?.loadSRAM()
        self.fileSystem?.boot()
        joycard = X68JoyCard(id: 0, scene: self, sprite: (self.childNode(withName: "//JoyCard") as? SKSpriteNode)!)
        devices.append(joycard!)
        
        for device in devices {
            device.Reset()
        }
        
        // Mark emulator as initialized with a small delay to ensure stability
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            self.isEmulatorInitialized = true
            print("Emulator initialization complete - using SpriteKit update")
            
            // Apply saved screen rotation after emulator is fully initialized
            self.applyScreenRotation()
        }
        
        mouseController = X68MouseController()
        self.joycontroller = JoyController()
        self.joycontroller?.setup(callback: controller_event(status:))
        
        var sample = self.samplingRate
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
        
        // Hide iOS legacy UI elements after startup sequence completes
        // Title animations take ~4.5 seconds (titleSprite + label), so wait 6 seconds to be safe
        DispatchQueue.main.asyncAfter(deadline: .now() + 6.0) {
            self.hideIOSLegacyUI()
        }
        
        self.label = self.childNode(withName: "//helloLabel") as? SKLabelNode
        if let label = self.label {
            label.alpha = 0.0
            label.zPosition = 3.0
            label.blendMode = .add
            label.run(SKAction.sequence([SKAction.wait(forDuration: 1.0),
                                         SKAction.fadeIn(withDuration: 2.0),
                                         SKAction.wait(forDuration: 0.5),
                                         SKAction.fadeAlpha(to: 0.0, duration: 1.0)]))
        }
        
        self.titleSprite = SKSpriteNode(imageNamed: "X68000LogoW.png")
        self.titleSprite?.zPosition = 3.0
        self.titleSprite?.alpha = 0.0
        self.titleSprite?.blendMode = .add
        self.titleSprite?.setScale(1.0)
        self.titleSprite?.run(SKAction.sequence([SKAction.fadeIn(withDuration: 2.0),
                                                 SKAction.wait(forDuration: 1.5),
                                                 SKAction.fadeAlpha(to: 0.0, duration: 1.0)]))
        self.addChild(titleSprite!)
        
        let w = (self.size.width + self.size.height) * 0.05
        self.spinnyNode = SKShapeNode(rectOf: CGSize(width: w, height: w), cornerRadius: w * 0.3)
        
        if let spinnyNode = self.spinnyNode {
            spinnyNode.lineWidth = 20.0
            spinnyNode.run(SKAction.sequence([SKAction.wait(forDuration: 0.5),
                                              SKAction.fadeOut(withDuration: 0.5),
                                              SKAction.removeFromParent()]))
        }
        
        #if os(iOS)
        let tapGes = UITapGestureRecognizer(target: self, action: #selector(self.tapped(_:)))
        tapGes.numberOfTapsRequired = 1
        tapGes.numberOfTouchesRequired = 1
        self.view?.addGestureRecognizer(tapGes)
        
        let hover = UIHoverGestureRecognizer(target: self, action: #selector(hovering(_:)))
        self.view?.addGestureRecognizer(hover)
        #endif
        self.addChild(spr)
    }
    
    #if os(iOS)
    @objc func hovering(_ sender: UIHoverGestureRecognizer) {
        print("hovering:\(sender.state.rawValue)")
        if #available(iOS 13.4, *) {
            print(sender.buttonMask.rawValue)
        } else {
            // Fallback on earlier versions
        }
        switch sender.state {
        case .began:
            print("Hover")
        case .changed:
            print(sender.location(in: self.view))
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
        print(sender.state)
        if sender.state == .began {
            print("began")
        }
        if sender.state == .recognized {
            print("recognized")
            mouseController?.ClickOnce()
        }
        if sender.state == .ended {
            print("ended")
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
        print("sceneDidLoad")
    }
    
    deinit {
        // Cleanup
        isEmulatorInitialized = false
    }
    
    override func didChangeSize(_ oldSize: CGSize) {
        print("didChangeSize \(oldSize)")
        screen_w = Float(oldSize.width)
        screen_h = Float(oldSize.height)
    }
    
    var virtualPad: SKNode = SKNode()
    
    override func didMove(to view: SKView) {
        print("didMove")
        self.setUpScene()
        
        // Screen rotation will be applied after emulator initialization in setUpScene()
        
        // Timer will be started after emulator initialization in setUpScene()
        
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
        
        moveJoystick.on(.begin) { [weak self] _ in
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
        // Security: Ensure only one timer exists
        stopUpdateTimer()
        
        print("Starting update timer...")
        // Start timer on main queue for UI updates
        DispatchQueue.main.async {
            self.timer = Timer.scheduledTimer(timeInterval: 1.0 / 60.0, target: self, selector: #selector(self.updateGame), userInfo: nil, repeats: true)
            print("Update timer started successfully")
            
            // Start periodic SRAM save timer (every 30 seconds)
            self.sramSaveTimer = Timer.scheduledTimer(timeInterval: 30.0, target: self, selector: #selector(self.periodicSRAMSave), userInfo: nil, repeats: true)
            print("Periodic SRAM save timer started")
        }
    }
    
    func ensureTimerRunning() {
        // Public method to restart timer if needed (for disk loading)
        if timer?.isValid != true && isEmulatorInitialized {
            print("Timer not running, restarting...")
            startUpdateTimer()
        }
    }
    
    @objc func diskImageLoaded() {
        print("Disk image loaded notification received - ensuring timer is running")
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
        // Security: Thread-safe counter increment
        timerQueue.async {
            self.aaa += 1
            if self.aaa % 100 == 0 {
                DispatchQueue.main.async {
                    print(self.aaa)
                }
            }
        }
        
        // Perform game update on main thread
        DispatchQueue.main.async {
            self.performGameUpdate()
        }
    }
    
    private func performGameUpdate() {
        // Safety: Only update if emulator is properly initialized
        guard isEmulatorInitialized else {
            print("Warning: Emulator not yet initialized, skipping update")
            return
        }
        
        // Debug: Log update calls periodically
        if aaa % 300 == 0 {
            print("GameUpdate running - frame \(aaa)")
        }
        
        // Update devices only when necessary for current input mode
        if currentInputMode == .joycard {
            // Only update joycard device in joycard mode
            joycard?.Update(CFAbsoluteTimeGetCurrent())
        }
        // Other devices updated less frequently or as needed
        
        mouseController?.SetScreenSize(width: Float(w), height: Float(h))
        mouseController?.Update()
        
        X68000_Update(self.clockMHz, self.vsync ? 1 : 0)
        let midi_count = X68000_GetMIDIBufferSize()
        
        // Security: Validate MIDI buffer pointer before use
        if let midi_buffer = X68000_GetMIDIBuffer() {
            labelMIDI?.text = "MIDI OUT:\(midi_count)"
            midiController.Send(midi_buffer, midi_count)
        } else {
            print("Error: Failed to get MIDI buffer")
        }
        
        w = Int(X68000_GetScreenWidth())
        h = Int(X68000_GetScreenHeight())
        
        // Security: Validate screen dimensions
        guard w > 0 && h > 0 && w <= 1024 && h <= 1024 else {
            print("Error: Invalid screen dimensions: \(w)x\(h)")
            return
        }
        
        X68000_GetImage(&d)
        
        let cgsize = CGSize(width: w, height: h)
        
        // Performance optimization: Reduce texture recreation overhead
        let screenSizeChanged = (w != lastScreenWidth || h != lastScreenHeight)
        
        // Always update texture data, but optimize sprite management
        let tex = SKTexture(data: Data(d), size: cgsize, flipped: true)
        
        if screenSizeChanged || spr.parent == nil {
            // Screen size changed or sprite not added yet
            lastScreenWidth = w
            lastScreenHeight = h
            
            if spr.parent != nil {
                spr.removeFromParent()
            }
            
            spr = SKSpriteNode(texture: tex, size: cgsize)
            spr.xScale = CGFloat(screen_w) / CGFloat(w)
            spr.yScale = CGFloat(screen_h) / CGFloat(h)
            spr.zPosition = -1.0
            self.addChild(spr)
        } else {
            // Just update texture, keep sprite
            spr.texture = tex
        }
    }
    
    @objc func periodicSRAMSave() {
        // Save SRAM periodically to prevent data loss
        sramSaveCounter += 1
        print("Periodic SRAM save #\(sramSaveCounter)")
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
        
        // Optimized device updates based on input mode
        if currentInputMode == .joycard {
            joycard?.Update(currentTime)
        }
        // Skip unnecessary device updates to improve performance
        
        mouseController?.SetScreenSize(width: Float(w), height: Float(h))
        mouseController?.Update()
        
        X68000_Update(self.clockMHz, self.vsync ? 1 : 0)
        let midi_count = X68000_GetMIDIBufferSize()
        
        // Security: Validate MIDI buffer pointer before use
        if let midi_buffer = X68000_GetMIDIBuffer() {
            labelMIDI?.text = "MIDI OUT:\(midi_count)"
            midiController.Send(midi_buffer, midi_count)
        } else {
            print("Error: Failed to get MIDI buffer")
        }
        
        w = Int(X68000_GetScreenWidth())
        h = Int(X68000_GetScreenHeight())
        
        // Security: Validate screen dimensions
        guard w > 0 && h > 0 && w <= 1024 && h <= 1024 else {
            print("Error: Invalid screen dimensions: \(w)x\(h)")
            return
        }
        
        X68000_GetImage(&d)
        
        let cgsize = CGSize(width: w, height: h)
        let tex = SKTexture(data: Data(d), size: cgsize, flipped: true)
        
        self.spr.removeFromParent()
        self.spr = SKSpriteNode(texture: tex, size: cgsize)
        self.spr.texture = tex
        self.spr.size = CGSize(width: w, height: h)
        
        // Apply current rotation and scaling without logging
        applySpriteTransformSilently()
        
        self.spr.zPosition = -1.0
        self.addChild(spr)
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
        print("Hiding iOS legacy UI elements after startup...")
        
        // Hide the iOS legacy Settings gear icon
        if let settingsNode = self.childNode(withName: "//Settings") as? SKSpriteNode {
            settingsNode.isHidden = true
            settingsNode.removeFromParent()
            print("Settings gear icon removed")
        }
        
        // Mark title labels as non-interactive to prevent click activation
        // but don't remove them so they can still be controlled by existing fade animations
        if let titleLabel = self.label {
            titleLabel.isUserInteractionEnabled = false
            print("Main title label interaction disabled")
        }
        
        // Disable interaction for all title-related labels
        let titleNodeNames = ["//helloLabel", "//helloLabel2", "//labelTitle", "//labelTitle2"]
        for nodeName in titleNodeNames {
            if let node = self.childNode(withName: nodeName) {
                node.isUserInteractionEnabled = false
                print("Disabled interaction for title node: \(nodeName)")
            }
        }
        
        // Find and disable interaction for any labels containing title text
        self.enumerateChildNodes(withName: "//*") { node, _ in
            if let labelNode = node as? SKLabelNode {
                if let text = labelNode.text {
                    if text.contains("POWER TO MAKE") || text.contains("for macOS") || text.contains("for iOS") {
                        labelNode.isUserInteractionEnabled = false
                        print("Disabled interaction for title text node: \(node.name ?? "unknown")")
                    }
                }
            }
        }
        
        // Reduce visibility of MIDI label for cleaner macOS experience
        labelMIDI?.alpha = 0.3
        labelMIDI?.fontSize = 12
        print("MIDI label visibility reduced")
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
    
    private func toggleInputMode() {
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
        notification.zPosition = 15
        notification.position = CGPoint(x: 0, y: 0)
        notification.alpha = 0
        
        let fadeSequence = SKAction.sequence([
            SKAction.fadeIn(withDuration: 0.2),
            SKAction.wait(forDuration: 1.0),
            SKAction.fadeOut(withDuration: 0.5),
            SKAction.removeFromParent()
        ])
        
        notification.run(fadeSequence)
        addChild(notification)
    }
}

#if os(iOS) || os(tvOS)
extension GameScene {
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
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
                print(t.name ?? "No name")
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
                self.makeSpinny(at: location, color: SKColor.green)
            }
            break
        }
    }
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
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
        // Handle joycard input only in joycard mode
        if currentInputMode == .joycard {
            for device in devices {
                device.touchesEnded(touches)
            }
        }
        
        if let touch = touches.first as UITouch? {
            let location = touch.location(in: self)
            let t = self.atPoint(location)
            print(t.name ?? "No name")
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
        print("Cancelled")
    }
}
#endif

#if os(OSX)
extension GameScene {
    override func mouseDown(with event: NSEvent) {
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
        // if location.y > 400.0 && distanceFromModeButton >= 100.0 {
        //     if let label = self.label {
        //         if let pulseAction = SKAction(named: "Pulse") {
        //             label.run(pulseAction, withKey: "fadeInOut")
        //         }
        //     }
        //     self.makeSpinny(at: location, color: SKColor.green)
        // }
    }
    
    override func mouseDragged(with event: NSEvent) {
        // Only create spinny for clock control area (upper area, excluding mode button area)
        let location = event.location(in: self)
        let modeButtonX = -size.width/2 + 150
        let modeButtonY = size.height/2 - 30
        let distanceFromModeButton = sqrt(pow(location.x - modeButtonX, 2) + pow(location.y - modeButtonY, 2))
        
        // Spinny animation disabled for cleaner macOS experience
        // if location.y > 400.0 && distanceFromModeButton >= 100.0 {
        //     self.makeSpinny(at: location, color: SKColor.blue)
        // }
    }
    
    override func mouseUp(with event: NSEvent) {
        let location = event.location(in: self)
        
        // Joycard mouse click handling only in joycard mode
        if currentInputMode == .joycard {
            joycard?.handleMouseClick(at: location, pressed: false)
        }
        
        // Spinny animation disabled for cleaner macOS experience  
        // No upper area interactions to prevent unwanted UI activations
        // let modeButtonX = -size.width/2 + 150
        // let modeButtonY = size.height/2 - 30
        // let distanceFromModeButton = sqrt(pow(location.x - modeButtonX, 2) + pow(location.y - modeButtonY, 2))
        // 
        // if location.y > 400.0 && distanceFromModeButton >= 100.0 {
        //     self.makeSpinny(at: location, color: SKColor.red)
        // }
    }
    
    
    override func keyDown(with event: NSEvent) {
        print("key press: \(event) keyCode: \(event.keyCode)")
        
        // Check for mode toggle key (Tab key)
        if event.keyCode == 48 { // Tab key
            toggleInputMode()
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
        print("🐛 Shift key pressed: \(isShiftPressed)")
        
        if isKeyDown {
            if isShiftPressed && !isShiftKeyPressed {
                // シフトキーが押された
                print("🐛 Sending SHIFT DOWN")
                X68000_Key_Down(0x1e1) // KeyTable拡張領域のシフトキー
                isShiftKeyPressed = true
            } else if !isShiftPressed && isShiftKeyPressed {
                // シフトキーが離された
                print("🐛 Sending SHIFT UP")
                X68000_Key_Up(0x1e1)
                isShiftKeyPressed = false
            }
        } else {
            if !isShiftPressed && isShiftKeyPressed {
                // シフトキーが離された
                print("🐛 Sending SHIFT UP")
                X68000_Key_Up(0x1e1)
                isShiftKeyPressed = false
            }
        }
        
        // 最初に特殊キー（文字なし）をチェック
        let x68KeyTableIndex = getMacKeyToX68KeyTableIndex(event.keyCode)
        if x68KeyTableIndex != 0 {
            print("🐛 Using special key KeyTable index: \(x68KeyTableIndex) for keyCode: \(event.keyCode)")
            if isKeyDown {
                X68000_Key_Down(x68KeyTableIndex)
            } else {
                X68000_Key_Up(x68KeyTableIndex)
            }
            return
        }
        
        // 物理キーベースのマッピング（修飾キーに依存しない基本文字）
        if let baseChar = getBaseCharacterForKeyCode(event.keyCode) {
            let ascii = baseChar.asciiValue!
            print("🐛 Using physical key mapping: '\(baseChar)' ASCII: \(ascii) for keyCode: \(event.keyCode)")
            if isKeyDown {
                X68000_Key_Down(UInt32(ascii))
            } else {
                X68000_Key_Up(UInt32(ascii))
            }
            return
        }
        
        // フォールバック: 文字ベース処理
        if let characters = event.characters, !characters.isEmpty {
            let char = characters.first!
            let asciiValue = char.asciiValue
            
            print("🐛 Fallback character: '\(char)' ASCII: \(asciiValue ?? 0)")
            
            if let ascii = asciiValue {
                print("🐛 Using KeyTable index: \(ascii) for character: '\(char)'")
                if isKeyDown {
                    X68000_Key_Down(UInt32(ascii))
                } else {
                    X68000_Key_Up(UInt32(ascii))
                }
                return
            }
        }
        
        print("🐛 Unmapped macOS keyCode: \(event.keyCode)")
    }
    
    override func keyUp(with event: NSEvent) {
        // Skip Tab key for mode toggle
        if event.keyCode == 48 { // Tab key
            return
        }
        
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
        
        // 記号（シフトなし状態）- Japanese keyboard corrected
        case 27: return "-"  // =/- キー (Japanese keyboard)
        case 24: return "="  // -/_ キー (Japanese keyboard)
        case 33: return "["
        case 30: return "]"
        case 42: return "\\"
        case 39: return ";"
        case 41: return "'"
        case 43: return ","
        case 47: return "."
        case 44: return "/"
        
        default:
            return nil
        }
    }
}
#endif
