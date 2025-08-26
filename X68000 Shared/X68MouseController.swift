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
    
    // Click state tracking and short hold enforcement
    private var lastClickTime: [Int: TimeInterval] = [:]
    private var lastClickState: [Int: Bool] = [:] // Track last state for each button
    private var pendingRelease: Set<Int> = []
    private var holdUntilFrame: [Int: Int] = [:]
    private let minimumHoldFrames: Int = 3 // ~55Hz => ~54ms (UI)
    private let minimumHoldSeconds: TimeInterval = 0.06 // UI only
    private let minimumHoldFramesCaptureLeft: Int = 1
    private var lastPressTime: [Int: TimeInterval] = [:]
    private let pressDebounceInterval: TimeInterval = 0.12
    private var lastReleaseTime: [Int: TimeInterval] = [:]
    private let retriggerGuardInterval: TimeInterval = 0.15 // ignore press soon after release (non-capture)
    private let pulseLeftClickInCapture: Bool = false
    private let pulseHoldFrames: Int = 3
    
    var x68k_width: Float = 0.0
    var x68k_height: Float = 0.0
    
    var frame = 0
    
    // (Removed hold logic) We send button state every frame in capture mode
    
    // Last-sent snapshot to suppress duplicate packets
    private var lastSentButtonState: Int = -1
    private var lastSentTx: Int = -1
    private var lastSentTy: Int = -1
    
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
        guard isCaptureMode else {
            // In non-capture, nothing to send here; event handlers call sendDirectUpdate()
            button_state = current_button_state
            return
        }
        
        // No hold: current_button_state comes directly from button_state
        
        // Apply deferred releases once minimum hold elapsed
        if !pendingRelease.isEmpty {
            var buttonsToClear: [Int] = []
            for b in pendingRelease {
                if let until = holdUntilFrame[b], frame >= until {
                    buttonsToClear.append(b)
                }
            }
            if !buttonsToClear.isEmpty {
                for b in buttonsToClear {
                    pendingRelease.remove(b)
                    holdUntilFrame[b] = nil
                    // Clear the bit safely
                    current_button_state &= ~(1<<b)
                }
            }
        }

        // Send updates every frame in capture mode to clear core deltas
        // Clamp tiny residuals to zero to avoid inertia, but very small
        let deadEps: Float = 0.0008
        if abs(dx) < deadEps { dx = 0.0 }
        if abs(dy) < deadEps { dy = 0.0 }
        let movement = (dx != 0.0 || dy != 0.0)
        if movement {
            X68000_Mouse_Set(dx * x68k_width, dy * x68k_height, current_button_state)
            dx = 0.0
            dy = 0.0
        } else {
            X68000_Mouse_Set(0, 0, current_button_state)
        }
        // Update last-sent snapshot after sending
        let tx = Int(mx * x68k_width)
        let ty = Int(my * x68k_height)
        lastSentTx = tx
        lastSentTy = ty
        lastSentButtonState = current_button_state
        
        // Sync internal state after sends
        button_state = current_button_state
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
        
        // Accumulate deltas based on absolute position change (no cumulative filtering)
        dx += (x - old_x) * mouseSensitivity
        dy += (y - old_y) * mouseSensitivity * -1.0 // invert Y to match X68 expected direction
        
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
    
    // Send current state immediately to the emulator (for non-capture mode)
    func sendDirectUpdate() {
        // Require valid screen size to avoid zero output
        guard x68k_width > 0 && x68k_height > 0 else { return }
        let tx = Int(mx * x68k_width)
        let ty = Int(my * x68k_height)
        // Suppress duplicates: only send if position (in pixel) or button changed
        if tx == lastSentTx && ty == lastSentTy && button_state == lastSentButtonState {
            return
        }
        if tx == lastSentTx && ty == lastSentTy {
            // Position unchanged: update buttons only (no movement)
            X68000_Mouse_Set(0, 0, button_state)
            lastSentButtonState = button_state
        } else {
            X68000_Mouse_SetDirect(Float(tx), Float(ty), button_state)
            lastSentTx = tx
            lastSentTy = ty
            lastSentButtonState = button_state
        }
    }
    
    // Send only button state (no movement), for capture mode clicks
    func sendButtonOnlyUpdate() {
        // Avoid redundant sends
        if button_state != lastSentButtonState {
            X68000_Mouse_Set(0, 0, button_state)
            lastSentButtonState = button_state
        }
    }
    func Click(_ type: Int,_ pressed:Bool) {
        // Ignore duplicate state
        let lastState = lastClickState[type] ?? false
        if lastState == pressed { return }

        lastClickState[type] = pressed

        let now = Date().timeIntervalSince1970
        infoLog("🖱️ X68MouseController.Click: type=\(type), pressed=\(pressed), button_state before=\(button_state)", category: .input)

        if pressed {
            // If a deferred release is pending for this button, finalize it now to allow double-click
            if pendingRelease.contains(type) {
                pendingRelease.remove(type)
                button_state &= ~(1<<type)
            }
            // Only guard re-press in non-capture UI (to avoid typing repeats)
            if !isCaptureMode, let lr = lastReleaseTime[type], (Date().timeIntervalSince1970 - lr) < retriggerGuardInterval {
                return
            }
            // Short press debounce (none for capture, small for non-capture)
            let debounce: TimeInterval = isCaptureMode ? 0.0 : 0.05
            if let lp = lastPressTime[type] {
                if now - lp < debounce { return }
            }
            lastPressTime[type] = now
            // Press: set bit and arm minimum hold
            pendingRelease.remove(type)
            button_state |= (1<<type)
            let holdFrames = (isCaptureMode && type == 0) ? minimumHoldFramesCaptureLeft : minimumHoldFrames
            holdUntilFrame[type] = frame + holdFrames
            lastClickTime[type] = now
        } else {
            // Release: enforce minimum hold in both capture and non-capture
            let sinceDown = now - (lastClickTime[type] ?? now)
            let needDelay = !isCaptureMode && (sinceDown < minimumHoldSeconds)
            lastReleaseTime[type] = now

            if isCaptureMode {
                // Immediate clear in capture mode
                button_state &= ~(1<<type)
            } else {
                if needDelay {
                    // Defer on main queue to simulate hold time
                    let delay = minimumHoldSeconds - sinceDown
                    pendingRelease.insert(type)
                    DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                        guard let self = self else { return }
                        if self.pendingRelease.contains(type) {
                            self.pendingRelease.remove(type)
                            self.button_state &= ~(1<<type)
                            self.sendDirectUpdate()
                        }
                    }
                } else {
                    button_state &= ~(1<<type)
                }
            }
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
