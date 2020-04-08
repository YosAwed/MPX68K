//
//  JoyCard.swift
//  X68000 iOS
//
//  Created by GOROman on 2020/04/08.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Foundation
import SpriteKit
import UIKit

class X68JoyCard : X68Device
{
    let device_id : UInt8
    let scene : SKScene
    let sprite : SKSpriteNode
    
    var buttonA : SKShapeNode?
    var buttonB : SKShapeNode?
    var buttonU : SKShapeNode?
    var buttonD : SKShapeNode?
    var buttonL : SKShapeNode?
    var buttonR : SKShapeNode?
    // MARK: - init
    init( id:Int, scene:SKScene, sprite:SKSpriteNode  ) {

        self.device_id = UInt8(id)
        self.scene = scene
        self.sprite = sprite
        
        buttonA = sprite.childNode(withName: "//A") as? SKShapeNode
        buttonB = sprite.childNode(withName: "//B") as? SKShapeNode
        buttonU = sprite.childNode(withName: "//U") as? SKShapeNode
        buttonD = sprite.childNode(withName: "//D") as? SKShapeNode
        buttonL = sprite.childNode(withName: "//L") as? SKShapeNode
        buttonR = sprite.childNode(withName: "//R") as? SKShapeNode

    }
    
    override func Reset() {
        
    }
    // MARK: - Update
    override func Update(_ currentTime: TimeInterval)
    {
        buttonA?.fillColor = (joydata & JOY_TRG1 != 0) ? .yellow : .black
        buttonB?.fillColor = (joydata & JOY_TRG2 != 0) ? .yellow : .black
        buttonU?.fillColor = (joydata & JOY_UP    != 0)  ? .yellow : .black
        buttonD?.fillColor = (joydata & JOY_DOWN  != 0) ? .yellow : .black
        buttonL?.fillColor = (joydata & JOY_LEFT  != 0) ? .yellow : .black
        buttonR?.fillColor = (joydata & JOY_RIGHT  != 0) ? .yellow : .black

    }
    override func touchesBegan(_ touches: Set<UITouch>) {
        super.touchesBegan(touches)
        for touch in touches {
            let location = touch.location(in: scene)
            let t = scene.atPoint(location)
            if let name = t.name {
                var c = false
                if ( name == "A" ) {
                    self.JoySet(device_id, JOY_TRG1, true ); c = true
                }
                if ( name == "B" ) {
                    self.JoySet(device_id, JOY_TRG2, true ); c = true
                }
#if false
                if ( name == "U" ) {
                    self.JoySet(device_id, JOY_UP, true ); c = true
                }
                if ( name == "D" ) {
                    self.JoySet(device_id, JOY_DOWN, true ); c = true
                }
                if ( name == "L" ) {
                    self.JoySet(device_id, JOY_LEFT, true ); c = true
                }
                if ( name == "R" ) {
                    self.JoySet(device_id, JOY_RIGHT, true ); c = true
                }
#endif
                
            }
        }
    }
    
    override func touchesMoved(_ touches: Set<UITouch>) {
        super.touchesMoved(touches)
        
        var flag : UInt8 = 0x00
        var old : UInt8 = joydata
        for touch in touches {
            let location = touch.location(in: scene)

            let t = scene.atPoint(location)
            if let name = t.name {
                if ( name == "MOVE" ) {
                    self.sprite.position = location
                }
                if ( name == "A" ) {
                    flag |= JOY_TRG1
                }
                if ( name == "B" ) {
                    flag |= JOY_TRG2
                }
#if false
                if ( name == "U" ) {
                    flag |= JOY_UP
                }
                if ( name == "D" ) {
                    flag |= JOY_DOWN
                }
                if ( name == "L" ) {
                    flag |= JOY_LEFT
                }
                if ( name == "R" ) {
                    flag |= JOY_RIGHT
                
                }
                #endif

            }
        }
        let trg = old ^ flag
//        print(trg)
        if ( trg > 0) {
//            joydata = flag
            X68000_Joystick_Set(device_id, joydata)
        }
//        self.JoySet(device_id, JOY_TRG1, false );
    }

    override func touchesEnded(_ touches: Set<UITouch>) {
        super.touchesEnded(touches)
        for touch in touches {
            let location = touch.location(in: scene)
            let t = scene.atPoint(location)
            if let name = t.name {
                if ( name == "A" ) {
                    self.JoySet(device_id, JOY_TRG1, false );
                }
                if ( name == "B" ) {
                    self.JoySet(device_id, JOY_TRG2, false );
                }
#if false
                if ( name == "U" ) {
                    self.JoySet(device_id, JOY_UP, false );
                }
                if ( name == "D" ) {
                    self.JoySet(device_id, JOY_DOWN, false );
                }
                if ( name == "L" ) {
                    self.JoySet(device_id, JOY_LEFT, false );
                }
                if ( name == "R" ) {
                    self.JoySet(device_id, JOY_RIGHT, false );
                }
#endif
            }
        }
    }
    var joydata : UInt8 = 0x00
    // MARK: ---- PRIVATE ----
    private func JoySet(_ port:UInt8,_ type:UInt8,_ pressed: Bool )
    {
        if ( pressed ) {
            joydata |= type
        } else {
            joydata &= ~type
        }
        X68000_Joystick_Set(port, joydata)

    }
}
