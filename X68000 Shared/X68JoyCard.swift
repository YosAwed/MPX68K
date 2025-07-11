//
//  JoyCard.swift
//  X68000 iOS
//
//  Created by GOROman on 2020/04/08.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Foundation
import SpriteKit
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

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
    #if os(iOS)
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
                
            }
        }
    }
    #endif
    
    #if os(iOS)
    override func touchesMoved(_ touches: Set<UITouch>) {
        super.touchesMoved(touches)
        
        var flag : UInt8 = 0x00
        let old : UInt8 = joydata
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

            }
        }
        let trg = old ^ flag
        if ( trg > 0) {
            joydata = flag
            X68000_Joystick_Set(device_id, joydata)
        }
    }
    #endif

    #if os(iOS)
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
            }
        }
    }
    #endif
    
    #if os(macOS)
    // MARK: - macOS Keyboard Input
    func handleKeyDown(_ keyCode: UInt16, _ pressed: Bool) {
        switch keyCode {
        case 0x7E: // Up Arrow
            JoySet(device_id, JOY_UP, pressed)
        case 0x7D: // Down Arrow
            JoySet(device_id, JOY_DOWN, pressed)
        case 0x7B: // Left Arrow
            JoySet(device_id, JOY_LEFT, pressed)
        case 0x7C: // Right Arrow
            JoySet(device_id, JOY_RIGHT, pressed)
        case 0x31: // Space (Button A)
            JoySet(device_id, JOY_TRG1, pressed)
        case 0x06: // Z (Button B)
            JoySet(device_id, JOY_TRG2, pressed)
        case 0x00: // A (Button A alternative)
            JoySet(device_id, JOY_TRG1, pressed)
        case 0x0B: // B (Button B alternative)
            JoySet(device_id, JOY_TRG2, pressed)
        default:
            break
        }
    }
    
    // WASD controls for alternative input
    func handleCharacterInput(_ character: String, _ pressed: Bool) {
        switch character.lowercased() {
        case "w":
            JoySet(device_id, JOY_UP, pressed)
        case "s":
            JoySet(device_id, JOY_DOWN, pressed)
        case "a":
            JoySet(device_id, JOY_LEFT, pressed)
        case "d":
            JoySet(device_id, JOY_RIGHT, pressed)
        case "j":
            JoySet(device_id, JOY_TRG1, pressed)
        case "k":
            JoySet(device_id, JOY_TRG2, pressed)
        default:
            break
        }
    }
    
    // Mouse click support for joycard buttons
    func handleMouseClick(at location: CGPoint, pressed: Bool) {
        let t = scene.atPoint(location)
        if let name = t.name {
            switch name {
            case "A":
                JoySet(device_id, JOY_TRG1, pressed)
            case "B":
                JoySet(device_id, JOY_TRG2, pressed)
            case "U":
                JoySet(device_id, JOY_UP, pressed)
            case "D":
                JoySet(device_id, JOY_DOWN, pressed)
            case "L":
                JoySet(device_id, JOY_LEFT, pressed)
            case "R":
                JoySet(device_id, JOY_RIGHT, pressed)
            default:
                break
            }
        }
    }
    #endif
    
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
