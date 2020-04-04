//
//  MouseController.swift
//  X68000
//
//  Created by GOROman on 2020/04/04.
//  Copyright © 2020 GOROman. All rights reserved.
//

class X68MouseController
{
    var screen_width: Float = 0.0
    var screen_height: Float = 0.0
    
    var button_state: Int = 0x00
    var dx : Float = 0.0
    var dy : Float = 0.0
    var old_x : Float = 0.0
    var old_y : Float = 0.0

    func Update()
    {
        X68000_Mouse_Set( dx*screen_width, screen_height-dy*screen_height, button_state)
        dx = 0.0
        dy = 0.0
    }
    
    func SetPosition(_ x: Float,_ y: Float ) {
        
        dx += x - old_x
        dy += y - old_y
        
        old_x = x
        old_y = y

    }

    func SetScreenSize( width: Float, height: Float ) {
        screen_width  = width
        screen_height = height
    }
    func ResetPosition(_ x: Float,_ y: Float ){
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

}
