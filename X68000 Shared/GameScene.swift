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
    var spr : SKSpriteNode?
    var spr256 : SKSpriteNode?
    var tex : SKTexture?
    var tex256 : SKTexture?
    var joycontroller : JoyController?

    fileprivate var audioStream : AudioStream?
    
//    fileprivate var fileSystem = FileSystem()
    
    
    class func newGameScene() -> GameScene {
        

        X68000_Init();
        
        let fileSystem = FileSystem.init()
        fileSystem.boot()

        
        func buttonHandler() -> GCControllerButtonValueChangedHandler {
            return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
                print("A!")  // ○
            }
        }

        // Load 'GameScene.sks' as an SKScene.
        guard let scene = SKScene(fileNamed: "GameScene") as? GameScene else {
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
            
            let node = SKLabelNode.init()
            node.fontSize = 64
            node.position.y = -350
            node.text = url.deletingPathExtension().lastPathComponent.removingPercentEncoding

            node.zPosition = 4.0
            node.alpha = 0
            node.run(SKAction.sequence([SKAction.fadeIn(withDuration: 0.2),
                                        SKAction.wait(forDuration: 0.5),
                                             SKAction.fadeOut(withDuration: 0.5),
                                             SKAction.removeFromParent()]))
            self.addChild(node)

            let fileSystem = FileSystem.init()
            fileSystem.loadDiskImage(0,url)
        })
    }
    
    func setUpScene() {
        


        self.joycontroller = JoyController.init()
        self.joycontroller?.setup(callback: controller_event(status:) );
        
        self.audioStream = AudioStream.init();
        self.audioStream?.play();
        
        self.labelStatus = self.childNode(withName: "//labelStatus") as? SKLabelNode
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
                SKAction.fadeAlpha(to: 0.2, duration: 1.0)
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
                SKAction.fadeAlpha(to: 0.2, duration: 1.0)
            ]
            ))
        self.addChild(titleSprite!)


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
            
            #if os(watchOS)
                // For watch we just periodically create one of these and let it spin
                // For other platforms we let user touch/mouse events create these
                spinnyNode.position = CGPoint(x: 0.0, y: 0.0)
                spinnyNode.strokeColor = SKColor.red
                self.run(SKAction.repeatForever(SKAction.sequence([SKAction.wait(forDuration: 2.0),
                                                                   SKAction.run({
                                                                       let n = spinnyNode.copy() as! SKShapeNode
                                                                       self.addChild(n)
                                                                   })])))
            #endif
        }
    }
    func applicationWillResignActive() {
        // audioStream?.pause()
    }
    func applicationWillEnterForeground() {
        //        audioStream?.play()
    }
    #if os(watchOS)
    override func sceneDidLoad() {
        self.setUpScene()
    }
    override func didMove(to view: SKView) {
        print("✳️didMove")
    }
    #else
    override func sceneDidLoad() {
        print("✳️sceneDidLoad")

    }
    override func didMove(to view: SKView) {
        print("✳️didMove")
        self.setUpScene()

    }
    #endif
    

    func makeSpinny(at pos: CGPoint, color: SKColor) {
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

    var d = [UInt8](repeating: 0xff, count: 768*512*4 )

    override func update(_ currentTime: TimeInterval) {
//        Benchmark.measure("X68000_Update  ", block: {
            X68000_Update(self.clockMHz)   // MHz
//        })

        // Called before each frame is rendered
        let w = X68000_GetScreenWidth();
        let h = X68000_GetScreenHeight();

//        Benchmark.measure("X68000_GetImage", block: {
            X68000_GetImage( &d )
//        })
        let cgsize = CGSize(width: Int(w), height: Int(h))
        self.tex = SKTexture.init(data: Data(d), size: cgsize, flipped: true )
        
        self.spr?.removeFromParent()

        self.spr = SKSpriteNode.init(texture: self.tex!, size: CGSize(width: Int(w), height: Int(h)));
        self.spr?.texture = self.tex!;
//        self.spr?.alpha = 1.0
        let scale : CGFloat  = 1.7
//        if ( w == 256 ) {
//            let scale_y : CGFloat = 256.0 / CGFloat(h)
//            let aspect : CGFloat = 768.0 / 512.0
//            self.spr?.xScale = scale * 2.0 * aspect
//            self.spr?.yScale = scale * (2.0 * scale_y)+0.3
//        } else {
            let scale_x : CGFloat = 768.0 / CGFloat(w)
            let scale_y : CGFloat = 512.0 / CGFloat(h)
//            let aspect  : CGFloat = 1.0
            self.spr?.xScale = scale * (1.0 * scale_x)
            self.spr?.yScale = scale * (1.0 * scale_y)//+0.3
//        }
        self.spr?.zPosition = 0.1
//        self.spr?.alpha = 1.0
//        self.spr?.blendMode = .add
        self.spr?.zPosition = -1.0
        
//        self.spr?.run(SKAction.sequence([
//            SKAction.wait(forDuration: 0.016),
//            SKAction.removeFromParent()]))
//
        self.addChild(spr!)
/*
        if ( w == 256 ) {
            let image = RBGImage( data:d, size: size, width: Int(w), height:Int(h) )
            self.tex256 = SKTexture.init( cgImage : image! )
            self.spr256?.texture = self.tex256!;// = SKSpriteNode.init(texture: self.tex);
        } else {
            let image = RBGImage( data:d, size: size, width:Int(w), height:Int(h) )
            self.tex = SKTexture.init( cgImage : image! )
        
            self.spr?.texture = self.tex!;// = SKSpriteNode.init(texture: self.tex);
        }
*/
    }
}

#if os(iOS) || os(tvOS)
// Touch-based event handling
extension GameScene {

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
//        if let label = self.label {
//            label.run(SKAction.init(named: "Pulse")!, withKey: "fadeInOut")
//        }
        
        for t in touches {
            self.makeSpinny(at: t.location(in: self), color: SKColor.green)
            
            print( "X:\(t.location(in: self).x) Y:\(t.location(in: self).y)" )
            break
        }
        if ( touches.count == 1 ) {
  //          X68000_Key_Down(0x20);
        }
        if ( touches.count == 3 ) {
//            print("3")
//            X68000_Quit()
//            X68000_Init()
        }
        
    }
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
//        for t in touches {
//           self.makeSpinny(at: t.location(in: self), color: SKColor.blue)
//        }
    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
//        for t in touches {
//            self.makeSpinny(at: t.location(in: self), color: SKColor.red)
//        }
    //    X68000_Key_Up(0x20);
    }
    
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
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

