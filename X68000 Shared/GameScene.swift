//
//  GameScene.swift
//  X68000 Shared
//
//  Created by GOROman on 2020/03/28.
//  Copyright © 2020 GOROman. All rights reserved.
//

import SpriteKit
import GameController


class GameScene: SKScene {
    
    var clockMHz:Int = 10
    
    fileprivate var label : SKLabelNode?
    fileprivate var labelStatus : SKLabelNode?
    fileprivate var spinnyNode : SKShapeNode?
    var titleSprite : SKSpriteNode?
    var mouseSprite : SKSpriteNode?
    var spr : SKSpriteNode?
    var spr256 : SKSpriteNode?
    var tex : SKTexture?
    var tex256 : SKTexture?
    var joycontroller : JoyController?
    let screen_w : Float = 1336.0
    let screen_h : Float = 1024.0
    
    fileprivate var audioStream : AudioStream?
    fileprivate var mouseController : X68MouseController?
    fileprivate var midiController : MIDIController = MIDIController()
    
    private var devices : [X68Device] = []
    
    
    let moveJoystick = 🕹(withDiameter: 100)
    let rotateJoystick = TLAnalogJoystick(withDiameter: 150)
    let rotateJoystick2 = TLAnalogJoystick(withDiameter: 150)
    
    
    //    fileprivate var fileSystem = FileSystem()
    
    var joycard : X68JoyCard?
    
    class func newGameScene() -> GameScene {
        
        
        X68000_Init();
        
        let fileSystem = FileSystem.init()
        fileSystem.loadSRAM()
        fileSystem.boot()
        
        func buttonHandler() -> GCControllerButtonValueChangedHandler {
            return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
                print("A!")  // ○
            }
        }
        
        // Load 'GameScene.sks' as an SKScene.
        guard let scene = GameScene(fileNamed: "GameScene") as? GameScene else {
            print("Failed to load GameScene.sks")
            abort()
        }
        
        // Set the scale mode to scale to fit the window
        scene.scaleMode = .aspectFit//.aspectFill
        //        scene.scaleMode = .aspectFill;
        scene.backgroundColor = .black
        
        
        return scene
    }
    
    
    var count = 0
    
    func controller_event( status : JoyController.Status ) {
        print( status )
        var msg = ""
        if ( status == .Conntected  ) {
            msg = "Controller Connected"
        } else if ( status == .Disconnected ) {
            msg = "Controller Disconnected"
            
        }
        if let t = self.labelStatus {
            t.text = msg
            t.run(SKAction.init(named: "Notify")!, withKey: "fadeInOut")
        }
    }
    
    func load( url : URL ){
        Benchmark.measure("load", block: {
            
            let imagefilename = url.deletingPathExtension().lastPathComponent.removingPercentEncoding
            
            let node = SKLabelNode.init()
            node.fontSize = 64
            node.position.y = -350
            // 拡張子を除いたファイル名
            node.text = imagefilename
            
            node.zPosition = 4.0
            node.alpha = 0
            node.run(SKAction.sequence([SKAction.fadeIn(withDuration: 0.2),
                                        SKAction.wait(forDuration: 0.5),
                                        SKAction.fadeOut(withDuration: 0.5),
                                        SKAction.removeFromParent()]))
            self.addChild(node)
            
            let fileSystem = FileSystem.init()
            fileSystem.loadDiskImage(url)
        })
    }
    
    func setUpScene() {
        joycard = X68JoyCard( id:0, scene: self, sprite: (self.childNode(withName: "//JoyCard") as? SKSpriteNode)! )
        devices.append( joycard! )
        
        for device in devices {
            device.Reset()
        }
        
        mouseController = X68MouseController()
        self.joycontroller = JoyController.init()
        self.joycontroller?.setup(callback: controller_event(status:) );
        
        self.audioStream = AudioStream.init();
        self.audioStream?.play();
        
        self.mouseSprite = self.childNode(withName: "//Mouse") as? SKSpriteNode
        
        self.labelStatus = self.childNode(withName: "//labelStatus") as? SKLabelNode
        #if true
        self.label = self.childNode(withName: "//helloLabel") as? SKLabelNode
        if let label = self.label {
            label.alpha = 0.0
            label.zPosition = 3.0
            label.blendMode = .add
            label.run(
                SKAction.sequence([
                    SKAction.wait(forDuration: 1.0),
                    SKAction.fadeIn(withDuration: 2.0),
                    SKAction.wait(forDuration: 0.5),
                    SKAction.fadeAlpha(to: 0.02, duration: 1.0)
                    ]
            ))
            
        }
        
        self.titleSprite = SKSpriteNode.init( imageNamed: "X68000LogoW.png" )
        self.titleSprite?.zPosition = 3.0
        self.titleSprite?.alpha = 0.0
        self.titleSprite?.blendMode = .add
        self.titleSprite?.setScale( 1.0 )
        self.titleSprite?.run(
            
            SKAction.sequence([
                SKAction.fadeIn(withDuration: 2.0),
                SKAction.wait(forDuration: 1.5),
                SKAction.fadeAlpha(to: 0.02, duration: 1.0)
                ]
        ))
        self.addChild(titleSprite!)
        #endif
        
        // Get label node from scene and store it for use later
        
        // Create shape node to use during mouse interaction
        let w = (self.size.width + self.size.height) * 0.05
        self.spinnyNode = SKShapeNode.init(rectOf: CGSize.init(width: w, height: w), cornerRadius: w * 0.3)
        
        if let spinnyNode = self.spinnyNode {
            spinnyNode.lineWidth = 20.0
            //           spinnyNode.run(SKAction.repeatForever(SKAction.rotate(byAngle: CGFloat(Double.pi), duration: 1)))
            spinnyNode.run(SKAction.sequence([SKAction.wait(forDuration: 0.5),
                                              SKAction.fadeOut(withDuration: 0.5),
                                              SKAction.removeFromParent()]))
            
        }
        do {
            let tapGes = UITapGestureRecognizer(target: self, action: #selector(self.tapped(_:)))
            tapGes.numberOfTapsRequired = 1
            tapGes.numberOfTouchesRequired = 1
            //            self.view?.addGestureRecognizer(tapGes)
        }
        
//        updating()    // メモリリーク？
        
        
    }
    
    @objc func tapped(_ sender: UITapGestureRecognizer){
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
    
    func applicationWillResignActive() {
        //        audioStream?.pause()
    }
    func applicationWillEnterForeground() {
        //        audioStream?.play()
    }
    override func sceneDidLoad() {
        print("✳️sceneDidLoad")
        
    }
    override func didMove(to view: SKView) {
        print("✳️didMove")
        self.setUpScene()
        
        let moveJoystickHiddenArea = TLAnalogJoystickHiddenArea(rect:
            CGRect(x: -scene!.size.width/2, y: -scene!.size.height * 0.45, width: scene!.size.width/2, height: scene!.size.height/3))
        moveJoystickHiddenArea.joystick = moveJoystick
        moveJoystick.isMoveable = true
        moveJoystickHiddenArea.zPosition = 10.0
        addChild(moveJoystickHiddenArea)
        
        let rotateJoystickHiddenArea = TLAnalogJoystickHiddenArea(rect:
            CGRect(x: (scene!.size.width/8)*2, y: -scene!.size.height * 0.45, width: scene!.size.width/8, height: scene!.size.height/3))
        rotateJoystickHiddenArea.joystick = rotateJoystick
        rotateJoystickHiddenArea.zPosition = 10.0
        addChild(rotateJoystickHiddenArea)

        let rotateJoystickHiddenArea2 = TLAnalogJoystickHiddenArea(rect:
            CGRect(x: (scene!.size.width/8)*3, y: -scene!.size.height * 0.45, width: scene!.size.width/8, height: scene!.size.height/3))
        rotateJoystickHiddenArea2.joystick = rotateJoystick2
        rotateJoystickHiddenArea2.zPosition = 10.0
        addChild(rotateJoystickHiddenArea2)
        #if true
        //MARK: Handlers begin
        moveJoystick.on(.begin) { [unowned self] _ in
        }
        
        moveJoystick.on(.move) { [unowned self] joystick in
            let VEL :CGFloat = 5.0
            let VELY :CGFloat = 10.0
//            print(joystick.velocity.x)
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
            X68000_Joystick_Set(UInt8(0), self.joycard!.joydata)
            
        }
        
        moveJoystick.on(.end) { [unowned self] _ in
            self.joycard?.joydata &= ~(JOY_RIGHT|JOY_LEFT|JOY_DOWN|JOY_UP)
            X68000_Joystick_Set(UInt8(0), self.joycard!.joydata)
            
        }
                rotateJoystick.on(.begin) { [unowned self] _ in
                    self.joycard?.joydata |= JOY_TRG2
                    X68000_Joystick_Set(UInt8(0), self.joycard!.joydata)
                }
                
                
                rotateJoystick.on(.move) { [unowned self] joystick in
                    self.joycard?.joydata |= JOY_TRG2
                    X68000_Joystick_Set(UInt8(0), self.joycard!.joydata)
                }
                
                rotateJoystick.on(.end) { [unowned self] _ in
                    self.joycard?.joydata &= ~(JOY_TRG2)
                    X68000_Joystick_Set(UInt8(0), self.joycard!.joydata)
                }

        rotateJoystick2.on(.begin) { [unowned self] _ in
                    self.joycard?.joydata |= JOY_TRG1
                    X68000_Joystick_Set(UInt8(0), self.joycard!.joydata)
                }
                
                
                rotateJoystick2.on(.move) { [unowned self] joystick in
                    self.joycard?.joydata |= JOY_TRG1
                    X68000_Joystick_Set(UInt8(0), self.joycard!.joydata)
                }
                
                rotateJoystick2.on(.end) { [unowned self] _ in
                    self.joycard?.joydata &= ~(JOY_TRG1)
                    X68000_Joystick_Set(UInt8(0), self.joycard!.joydata)
                }
        #endif
        //MARK: Handlers end
        
        moveJoystick.handleImage = UIImage(named: "jStick")
        
        moveJoystick.baseImage = UIImage(named: "jSubstrate")
        
            rotateJoystick.handleImage = UIImage(named: "jStick")
            rotateJoystick2.handleImage = UIImage(named: "jStick")
            //        rotateJoystick.baseImage = UIImage(named: "jSubstrate")
        
        
    }
    
    
    func makeSpinny(at pos: CGPoint, color: SKColor) {
        if ( pos.y < 400.0 ) {
            return
        }
        
        if let spinny = self.spinnyNode?.copy() as! SKShapeNode? {
            spinny.position = pos
            spinny.strokeColor = color
            spinny.lineWidth = 0.1
            spinny.zPosition = 1.0
            
            var clock = ((pos.y+1000.0) / 2000.0)
            clock *= 200.0
            if (pos.x < -500 ) {
                clock = 1
            } else if (pos.x < -400 ) {
                clock = 10
            } else if (pos.x < -200 ) {
                clock = 16
            } else if (pos.x < -0 ) {
                clock = 24
            }
            clockMHz = Int(clock)
            let label = SKLabelNode.init()
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
    var aaa  = 0
    var timer : Timer = Timer()
    @objc func updating()  {
        if self.timer.isValid {
            self.timer.invalidate()
        }
        
        
        self.timer = Timer.scheduledTimer(timeInterval: 1.0/120.0, target: self, selector: #selector(updating), userInfo: nil, repeats: true)
        aaa += 1
        if ( aaa % 100 == 0 ) {
            
            print(aaa)
        }
    }
    
    var d = [UInt8](repeating: 0xff, count: 768*512*4 )
    var w:Int32 = 1
    var h:Int32 = 1
    override func update(_ currentTime: TimeInterval) {
        //        Benchmark.measure("X68000_Update  ", block: {
        for device in devices {
            device.Update(currentTime)
        }
        
        
        mouseController?.SetScreenSize( width: Float(w), height: Float(h) )
        mouseController?.Update()
        do { // コレはセットで
            X68000_Update(self.clockMHz)   // MHz
            let midi_count  = X68000_GetMIDIBufferSize()
            let midi_buffer = X68000_GetMIDIBuffer()
            midiController.Send( midi_buffer, midi_count )
        }

        
        //        })
        
        // Called before each frame is rendered
        w = X68000_GetScreenWidth();
        h = X68000_GetScreenHeight();
        
        //        Benchmark.measure("X68000_GetImage", block: {
        X68000_GetImage( &d )
        //        })
        let cgsize = CGSize(width: Int(w), height: Int(h))
        self.tex = SKTexture.init(data: Data(d), size: cgsize, flipped: true )
        
        self.spr?.removeFromParent()
        
        self.spr = SKSpriteNode.init(texture: self.tex!, size: CGSize(width: Int(w), height: Int(h)));
        self.spr?.texture = self.tex!;
        let scale : CGFloat  = 1.0  // 1.7
        
        
        
        let scale_x : CGFloat = 768.0 / CGFloat(w)
        let scale_y : CGFloat = 512.0 / CGFloat(h)
        self.spr?.xScale = CGFloat(screen_w) / CGFloat((w)) //scale * (1.0 * scale_x)
        self.spr?.yScale = CGFloat(screen_h) / CGFloat((h)) //scale * (1.0 * scale_y)//+0.3
        self.spr?.zPosition = 0.1
        self.spr?.zPosition = -1.0
        self.addChild(spr!)
    }
}



#if os(iOS) || os(tvOS)
// Touch-based event handling
extension GameScene {
    
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for device in devices {
            device.touchesBegan( touches )
        }
        
        if ( touches.count == 1 ) {
            if let touch = touches.first as UITouch? {
                let location = touch.location(in: self)
                let t = self.atPoint(location)
                print(t.name)
                if t.name == "LButton" {
                    mouseController?.Click(0, true)
                    var t = t as! SKShapeNode
                    t.fillColor = .yellow
                } else
                    if t.name == "RButton" {
                        mouseController?.Click(1, true)
                        var t = t as! SKShapeNode
                        t.fillColor = .yellow
                    } else
                        if t.name == "MouseBody" {
                            mouseController?.ResetPosition( location, scene!.size )
                        } else {
                            //                            mouseSprite?.position = location
                            mouseController?.ResetPosition( location, scene!.size ) // A
                            
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
            device.touchesMoved( touches )
        }
        
        if ( touches.count == 1 ) {
            if let touch = touches.first as UITouch? {
                let location = touch.location(in: self)
                let t = self.atPoint(location)
                if t.name == "MouseBody" {
                    
                    let t = touches.first!
                    //                    let x2 = Float(t.location(in: self).x)
                    //                    let y2 = Float(t.location(in: self).y)
                    
                    //                    mouseSprite?.position = location
                    //                    mouseController?.SetPosition(location,scene!.size)
                    
                } else {
                    mouseController?.SetPosition(location,scene!.size)  //MODE B
                    
                }
            }
        }
        
        
        
        
        //        for t in touches {
        
        //           self.makeSpinny(at: t.location(in: self), color: SKColor.blue)
        //        }
    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for device in devices {
            device.touchesEnded( touches )
        }
        
        if let touch = touches.first as UITouch? {
            let location = touch.location(in: self)
            let t = self.atPoint(location)
            print(t.name)
            if t.name == "LButton" {
                //                    print("button tapped")
                let t = t as! SKShapeNode
                t.fillColor = .black
                mouseController?.Click(0, false)
            }
            if t.name == "RButton" {
                let t = t as! SKShapeNode
                t.fillColor = .black
                mouseController?.Click(1, false)
                
            }
            if t.name == "MouseBody" {
                let x = Float(location.x)
                let y = Float(location.y)
            }
        }
        
    }
    
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        print("Cancelled")
        //        for t in touches {
        //            self.makeSpinny(at: t.location(in: self), color: SKColor.red)
        //        }
    }
    
    
}
#endif

#if os(OSX)
// Mouse-based event handling
extension GameScene {
    
    override func mouseDown(with event: NSEvent) {
        if let label = self.label {
            label.run(SKAction.init(named: "Pulse")!, withKey: "fadeInOut")
        }
        self.makeSpinny(at: event.location(in: self), color: SKColor.green)
        
        
    }
    
    override func mouseDragged(with event: NSEvent) {
        self.makeSpinny(at: event.location(in: self), color: SKColor.blue)
    }
    
    override func mouseUp(with event: NSEvent) {
        self.makeSpinny(at: event.location(in: self), color: SKColor.red)
    }
    
    func keyConv(_ keyCode:UInt16 ) -> UInt32
    {
        var ret : UInt32 = 0;
        switch(keyCode){
        case 6:            ret = 0x5a;            break;    // Z
        case 7:            ret = 0x58;            break;    // X
        case 18:           ret = 0x31;            break;    // 1
        case 122:          ret = 0x1be;           break;    // F1
        case 123:          ret = 0x114;           break;    // ←
        case 124:          ret = 0x113;           break;    // →
        case 125:          ret = 0x112;           break;    // ↓
        case 126:          ret = 0x111;           break;    // ↑
        case 49:           ret = 0x20;            break;    // Space
        case 36:           ret = 0x0d;            break;    // return
        default:
            break;
        }
        return ret
    }
    override func keyDown(with event: NSEvent) {
        print("key press: \(event)")
        
        X68000_Key_Down(keyConv(event.keyCode));
        /*
         left arrow    123
         right arrow    124
         up arrow    126
         down arrow    125
         */
        
    }
    override func keyUp(with event: NSEvent) {
        X68000_Key_Up(keyConv(event.keyCode));
        
    }
    
    //    void X68000_Key_Down( unsigned int vkcode );
    //    void X68000_Key_Up( unsigned int vkcode );
    
    
}
#endif

