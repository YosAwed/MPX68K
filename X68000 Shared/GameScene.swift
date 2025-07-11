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

class GameScene: SKScene {
    
    private var clockMHz: Int = 24
    private var samplingRate: Int = 22050
    private var vsync: Bool = true
    
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
    
    private var audioStream: AudioStream?
    private var mouseController: X68MouseController?
    private var midiController: MIDIController = MIDIController()
    
    private var devices: [X68Device] = []
    private var fileSystem: FileSystem?
    
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
    
    let userDefaults = UserDefaults.standard
    
    private func settings() {
        if let clock = userDefaults.object(forKey: "clock") as? String {
            self.clockMHz = Int(clock)!
            print("CPU Clock: \(self.clockMHz) MHz")
        }
        
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
        // audioStream?.pause()
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
        
        moveJoystick.on(.begin) { [weak self] _ in
            // Empty handler for begin event
        }
        
        moveJoystick.on(.move) { [weak self] joystick in
            guard let self = self else { return }
            let VEL: CGFloat = 5.0
            let VELY: CGFloat = 10.0
            if joystick.velocity.x > VEL {
                self.joycard?.joydata |= JOY_RIGHT
            } else {
                self.joycard?.joydata &= ~JOY_RIGHT
            }
            if joystick.velocity.x < -VEL {
                self.joycard?.joydata |= JOY_LEFT
            } else {
                self.joycard?.joydata &= ~JOY_LEFT
            }
            if joystick.velocity.y > VELY {
                self.joycard?.joydata |= JOY_UP
            } else {
                self.joycard?.joydata &= ~JOY_UP
            }
            if joystick.velocity.y < -VELY {
                self.joycard?.joydata |= JOY_DOWN
            } else {
                self.joycard?.joydata &= ~JOY_DOWN
            }
            if let joydata = self.joycard?.joydata {
                X68000_Joystick_Set(UInt8(0), joydata)
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
            guard let self = self else { return }
            self.joycard?.joydata |= JOY_TRG2
            if let joydata = self.joycard?.joydata {
                X68000_Joystick_Set(UInt8(0), joydata)
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
            guard let self = self else { return }
            self.joycard?.joydata |= JOY_TRG1
            if let joydata = self.joycard?.joydata {
                X68000_Joystick_Set(UInt8(0), joydata)
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
        if pos.y < 400.0 {
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
                
                self.addChild(spinny)
            }
        }
    }
    
    var aaa = 0
    var timer: Timer?
    private let timerQueue = DispatchQueue(label: "timer.queue", qos: .userInteractive)
    
    func startUpdateTimer() {
        // Security: Ensure only one timer exists
        stopUpdateTimer()
        
        print("Starting update timer...")
        // Start timer on main queue for UI updates
        DispatchQueue.main.async {
            self.timer = Timer.scheduledTimer(timeInterval: 1.0 / 60.0, target: self, selector: #selector(self.updateGame), userInfo: nil, repeats: true)
            print("Update timer started successfully")
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
        
        // Actual game update logic from the original update method
        for device in devices {
            device.Update(CFAbsoluteTimeGetCurrent())
        }
        
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
        let scale_x: CGFloat = 768.0 / CGFloat(w)
        let scale_y: CGFloat = 512.0 / CGFloat(h)
        self.spr.xScale = CGFloat(screen_w) / CGFloat(w)
        self.spr.yScale = CGFloat(screen_h) / CGFloat(h)
        self.spr.zPosition = -1.0
        self.addChild(spr)
    }
    
    var d = [UInt8](repeating: 0xff, count: 768 * 512 * 4)
    var w: Int = 1
    var h: Int = 1
    
    override func update(_ currentTime: TimeInterval) {
        // Safety: Only update if emulator is properly initialized
        guard isEmulatorInitialized else {
            return
        }
        
        // Original game update logic restored to fix EXC_BAD_ACCESS
        for device in devices {
            device.Update(currentTime)
        }
        
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
        self.spr.xScale = CGFloat(screen_w) / CGFloat(w)
        self.spr.yScale = CGFloat(screen_h) / CGFloat(h)
        self.spr.zPosition = -1.0
        self.addChild(spr)
    }
}

#if os(iOS) || os(tvOS)
extension GameScene {
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for device in devices {
            device.touchesBegan(touches)
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
            self.makeSpinny(at: t.location(in: self), color: SKColor.green)
            break
        }
    }
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for device in devices {
            device.touchesMoved(touches)
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
        for device in devices {
            device.touchesEnded(touches)
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
        // Joycard mouse click handling
        joycard?.handleMouseClick(at: event.location(in: self), pressed: true)
        
        if let label = self.label {
            if let pulseAction = SKAction(named: "Pulse") {
                label.run(pulseAction, withKey: "fadeInOut")
            }
        }
        self.makeSpinny(at: event.location(in: self), color: SKColor.green)
    }
    
    override func mouseDragged(with event: NSEvent) {
        self.makeSpinny(at: event.location(in: self), color: SKColor.blue)
    }
    
    override func mouseUp(with event: NSEvent) {
        // Joycard mouse click handling
        joycard?.handleMouseClick(at: event.location(in: self), pressed: false)
        
        self.makeSpinny(at: event.location(in: self), color: SKColor.red)
    }
    
    
    override func keyDown(with event: NSEvent) {
        print("key press: \(event) keyCode: \(event.keyCode)")
        
        // Joycard input handling for macOS
        joycard?.handleKeyDown(event.keyCode, true)
        if let characters = event.characters, !characters.isEmpty {
            joycard?.handleCharacterInput(String(characters.first!), true)
        }
        
        // シフトキーの状態をチェックして、状態変化があれば送信
        let isShiftPressed = event.modifierFlags.contains(.shift)
        print("🐛 Shift key pressed: \(isShiftPressed)")
        
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
        
        // 最初に特殊キー（文字なし）をチェック
        let x68KeyTableIndex = getMacKeyToX68KeyTableIndex(event.keyCode)
        if x68KeyTableIndex != 0 {
            print("🐛 Using special key KeyTable index: \(x68KeyTableIndex) for keyCode: \(event.keyCode)")
            X68000_Key_Down(x68KeyTableIndex)
            return
        }
        
        // 物理キーベースのマッピング（修飾キーに依存しない基本文字）
        if let baseChar = getBaseCharacterForKeyCode(event.keyCode) {
            let ascii = baseChar.asciiValue!
            print("🐛 Using physical key mapping: '\(baseChar)' ASCII: \(ascii) for keyCode: \(event.keyCode)")
            X68000_Key_Down(UInt32(ascii))
            return
        }
        
        // フォールバック: 文字ベース処理
        if let characters = event.characters, !characters.isEmpty {
            let char = characters.first!
            let asciiValue = char.asciiValue
            
            print("🐛 Fallback character: '\(char)' ASCII: \(asciiValue ?? 0)")
            
            if let ascii = asciiValue {
                print("🐛 Using KeyTable index: \(ascii) for character: '\(char)'")
                X68000_Key_Down(UInt32(ascii))
                return
            }
        }
        
        print("🐛 Unmapped macOS keyCode: \(event.keyCode)")
    }
    
    override func keyUp(with event: NSEvent) {
        // Joycard input handling for macOS
        joycard?.handleKeyDown(event.keyCode, false)
        if let characters = event.characters, !characters.isEmpty {
            joycard?.handleCharacterInput(String(characters.first!), false)
        }
        
        // シフトキーの状態をチェックして、状態変化があれば送信
        let isShiftPressed = event.modifierFlags.contains(.shift)
        
        if !isShiftPressed && isShiftKeyPressed {
            // シフトキーが離された
            print("🐛 Sending SHIFT UP")
            X68000_Key_Up(0x1e1)
            isShiftKeyPressed = false
        }
        
        // キーアップも同じ順序で処理
        let x68KeyTableIndex = getMacKeyToX68KeyTableIndex(event.keyCode)
        if x68KeyTableIndex != 0 {
            X68000_Key_Up(x68KeyTableIndex)
            return
        }
        
        // 物理キーベースのマッピング
        if let baseChar = getBaseCharacterForKeyCode(event.keyCode) {
            let ascii = baseChar.asciiValue!
            X68000_Key_Up(UInt32(ascii))
            return
        }
        
        // フォールバック: 文字ベース処理
        if let characters = event.characters, !characters.isEmpty {
            let char = characters.first!
            if let ascii = char.asciiValue {
                X68000_Key_Up(UInt32(ascii))
                return
            }
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
        
        // 記号（シフトなし状態）
        case 27: return "-"  // -/= キー
        case 24: return "="  // =/+ キー（注意：これは仮、実際は複雑）
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
