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
    
    // Mouse capture mode state
    var isCaptureMode = false
    
    // Mouse sensitivity adjustment (lower = less sensitive, higher = more sensitive)
    var mouseSensitivity: Float = 0.8
    
    var mx : Float = 0.0
    var my : Float = 0.0
    var dx : Float = 0.0
    var dy : Float = 0.0
    var old_x : Float = 0.0
    var old_y : Float = 0.0
    
    var button_state: Int = 0x00
    
    var click_flag : Int = 0
    
    // Click debouncing to prevent multiple clicks
    private var lastClickTime: [Int: TimeInterval] = [:]
    private let clickDebounceInterval: TimeInterval = 0.3 // 300ms debounce
    private var lastClickState: [Int: Bool] = [:] // Track last state for each button
    
    var x68k_width: Float = 0.0
    var x68k_height: Float = 0.0
    
    var frame = 0
    
    func Update()
    {
        frame += 1
        
        // Handle ClickOnce temporary click flag (for legacy compatibility)
        var current_button_state = button_state
        if click_flag > 0 {
            current_button_state |= 1  // Add left button for ClickOnce
            click_flag -= 1
            // debugLog("Mouse ClickOnce frame: \(frame) flag: \(click_flag)", category: .input)
        }
        
        // Only process mouse updates when in capture mode
        guard isCaptureMode else { return }
        
        // Use delta movement (dx, dy) instead of absolute position to prevent drift
        if dx != 0.0 || dy != 0.0 {
            X68000_Mouse_Set( dx*x68k_width, dy*x68k_height, current_button_state)
            dx = 0.0
            dy = 0.0
        } else {
            // No movement, just update button state - treat X and Y coordinates the same way
            X68000_Mouse_SetDirect( mx*x68k_width, my*x68k_height, current_button_state)
        }
    }
    
    fileprivate func Normalize(_ a :CGFloat, _ b :CGFloat ) -> Float
    {
        // Fixed: Remove +0.5 offset that was causing coordinate issues
        return Float(a) / Float(b)
    }
    
    func SetPosition(_ location :CGPoint, _ size: CGSize ){
        let x = Normalize( location.x, size.width  )
        let y = Normalize( location.y, size.height )
        
        self.SetPosition(x,y)
    }
    func SetPosition(_ x: Float,_ y: Float  ) {
        
        mx = x
        my = y
        
        // Simple direct sensitivity adjustment without accumulation
        dx += (x - old_x) * mouseSensitivity
        dy += (y - old_y) * mouseSensitivity
        
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
        // Check if this is actually a state change
        let lastState = lastClickState[type] ?? false
        if lastState == pressed {
            // No state change, ignore duplicate call
            return
        }
        
        let currentTime = CFAbsoluteTimeGetCurrent()
        
        // Additional time-based debouncing
        if pressed {
            if let lastTime = lastClickTime[type] {
                let timeSinceLastClick = currentTime - lastTime
                if timeSinceLastClick < clickDebounceInterval {
                    // Too soon since last click, ignore this one
                    return
                }
            }
            lastClickTime[type] = currentTime
        }
        
        // Update state tracking
        lastClickState[type] = pressed
        
        infoLog("🖱️ X68MouseController.Click: type=\(type), pressed=\(pressed), button_state before=\(button_state)", category: .input)
        if pressed {
            button_state |= (1<<type)
        } else {
            button_state &= ~(1<<type)
        }
        infoLog("🖱️ X68MouseController.Click: button_state after=\(button_state)", category: .input)
    }
    func ClickOnce()
    {
        debugLog("\(frame): ClickOnce", category: .input)
        click_flag = 2
    }
    
    // MARK: - Mouse Capture Mode Management
    
    func enableCaptureMode() {
        isCaptureMode = true
        
        // Initialize mouse position to prevent jumping
        dx = 0.0
        dy = 0.0
        
        // Reset to center position to ensure consistent starting point
        ResetPosition(0.5, 0.5)
        
        // debugLog("Mouse controller capture mode enabled", category: .input)
    }
    
    func disableCaptureMode() {
        isCaptureMode = false
        
        // Clear all movement data to prevent drift after disabling
        dx = 0.0
        dy = 0.0
        
        // Reset internal coordinates to prevent any residual movement
        mx = 0.5
        my = 0.5
        old_x = 0.5 
        old_y = 0.5
        
        // Clear click state tracking
        lastClickTime.removeAll()
        lastClickState.removeAll()
        
        // Send multiple stop commands to ensure X68000 mouse system stops completely
        if x68k_width > 0 && x68k_height > 0 {
            // First try to counteract any ongoing movement with large opposite deltas
            X68000_Mouse_Set(-1000, -1000, 0) // Large negative deltas to counteract right-down drift
            X68000_Mouse_Set(-500, -500, 0)   // Medium negative deltas
            X68000_Mouse_Set(-100, -100, 0)   // Small negative deltas
            
            // Send zero deltas multiple times to ensure complete stop
            for _ in 0..<10 {
                X68000_Mouse_Set(0, 0, 0)
            }
            
            // Set stable center position
            X68000_Mouse_SetDirect(x68k_width/2, x68k_height/2, 0)
            
            // Send more zeros to be absolutely sure
            for _ in 0..<5 {
                X68000_Mouse_Set(0, 0, 0)
            }
        }
        
        // debugLog("Mouse controller capture mode disabled", category: .input)
    }
    
}
