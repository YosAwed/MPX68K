//
//  MouseController.swift
//  X68000
//
//  Created by GOROman on 2020/04/04.
//  Copyright © 2020 GOROman. All rights reserved.
//

import SpriteKit

class X68MouseController
{
    var mode = 0
    
    var mx : Float = 0.0
    var my : Float = 0.0
    var dx : Float = 0.0
    var dy : Float = 0.0
    var old_x : Float = 0.0
    var old_y : Float = 0.0
    
    var button_state: Int = 0x00
    
    var click_flag : Int = 0
    
    var x68k_width: Float = 0.0
    var x68k_height: Float = 0.0
    
    var frame = 0
    
    func Update()
    {
        frame += 1
        if click_flag > 0 {
            button_state = 1
            click_flag -= 1

            if ( click_flag == 0 ) {
                button_state = 0
            }
            debugLog("Mouse click frame: \(frame) flag: \(click_flag)", category: .input)
            
        }
        X68000_Mouse_SetDirect( mx*x68k_width, x68k_height-my*x68k_height, button_state)
        //        X68000_Mouse_Set( dx*x68k_width, x68k_height-dy*x68k_height, button_state)
        dx = 0.0
        dy = 0.0
    }
    
    fileprivate func Normalize(_ a :CGFloat, _ b :CGFloat ) -> Float
    {
        return Float(a) / Float(b) + 0.5
    }
    
    func SetPosition(_ location :CGPoint, _ size: CGSize ){
        let x = Normalize( location.x, size.width  )
        let y = Normalize( location.y, size.height )
        
        self.SetPosition(x,y)
    }
    func SetPosition(_ x: Float,_ y: Float  ) {
        
        mx = x
        my = y
        
        dx += x - old_x
        dy += y - old_y
        
        old_x = x
        old_y = y
        
    }
    
    func SetScreenSize( width: Float, height: Float ) {
        x68k_width  = width
        x68k_height = height
    }
    func ResetPosition(_ location: CGPoint, _ size: CGSize){
        let x = Normalize( location.x, size.width  )
        let y = Normalize( location.y, size.height )
        
        self.ResetPosition( x, y )
    }
    func ResetPosition(_ x: Float,_ y: Float ) {
        mx = x
        my = y
        
        dx = 0
        dy = 0
        old_x = x
        old_y = y
    }
    func Click(_ type: Int,_ pressed:Bool) {
        if pressed {
            button_state |= (1<<type)
        } else {
            button_state &= ~(1<<type)
        }
    }
    func ClickOnce()
    {
        debugLog("\(frame): ClickOnce", category: .input)
        click_flag = 2
    }
    
}
