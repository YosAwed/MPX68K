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
    // Slight bump for lighter feel
    var mouseSensitivity: Float = 0.52

    // Clamp per-frame movement in capture mode to avoid big jumps
    private let movementMaxStepCapture: Float = 1.75
    
    // Hysteresis + small-move boost to balance light feel and crisp stop
    private var movingX: Bool = false
    private var movingY: Bool = false
    private let startThreshold: Float = 0.04   // begin moving when above this
    private let stopThreshold:  Float = 0.10   // stop quickly when below this
    private let smallBoostThreshold: Float = 0.25 // apply small-boost under this
    private let smallBoostGain: Float = 1.35
    private let smallStepFloor: Float = 0.02
    
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
    private let minimumHoldSeconds: TimeInterval = 0.06
    private let minimumHoldFramesCaptureLeft: Int = 5
    private var lastPressTime: [Int: TimeInterval] = [:]
    private let pressDebounceInterval: TimeInterval = 0.12
    private var lastReleaseTime: [Int: TimeInterval] = [:]
    private let retriggerGuardInterval: TimeInterval = 0.08 // 修正: 0.15 -> 0.08
    private let pulseLeftClickInCapture: Bool = false
    private let pulseHoldFrames: Int = 3

    private var clickProcessing: Set<Int> = []
    private var scheduledReleases: [Int: DispatchWorkItem] = [:]

    // Double-click emulation/timing
    private let doubleClickWindow: TimeInterval = 0.20
    private var doubleClickQueue: [(frame: Int, type: Int, pressed: Bool)] = []
    private var lastSingleClickTime: [Int: TimeInterval] = [:]
    private var doubleClickDetected: [Int: Bool] = [:]

    // Suppress relative movement between the taps that make up a double-click
    private var doubleClickSuppressionActive = false
    private var doubleClickSuppressionWorkItem: DispatchWorkItem?
    private let doubleClickSuppressionInterval: TimeInterval = 0.22
    private var doubleClickSuppressionQueueCount = 0
    
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

        // ダブルクリック合成イベントは廃止：VS.X側でダブルクリック判定を行う

        // Send updates only on movement or button-change
        // Clamp tiny residuals to zero to avoid inertia
        // Keep tiny sensor noise out; hysteresis handles crisp stop
        let deadEps: Float = 0.06
        if abs(dx) < deadEps { dx = 0.0 }
        if abs(dy) < deadEps { dy = 0.0 }
        let movement = (dx != 0.0 || dy != 0.0)
        // Prefer movement packets; if no movement, send button-only packet to avoid jitter
        if movement {
            // Keep button state current via Mouse_Event so MouseStat is correct
            let leftDown = (current_button_state & 0x1) != 0
            let rightDown = (current_button_state & 0x2) != 0
            // Send relative movement via SCC mouse queue; core inverts Y internally
            // Apply clamp to avoid large single-step movement
            var sx = dx
            var sy = dy
            if sx > movementMaxStepCapture { sx = movementMaxStepCapture }
            if sx < -movementMaxStepCapture { sx = -movementMaxStepCapture }
            if sy > movementMaxStepCapture { sy = movementMaxStepCapture }
            if sy < -movementMaxStepCapture { sy = -movementMaxStepCapture }
            
            // Axis-wise hysteresis for crisp stop
            let ax = abs(sx), ay = abs(sy)
            // X axis
            if movingX {
                if ax < stopThreshold { sx = 0; movingX = false }
            } else {
                if ax <= startThreshold { sx = 0 } else { movingX = true }
            }
            // Y axis
            if movingY {
                if ay < stopThreshold { sy = 0; movingY = false }
            } else {
                if ay <= startThreshold { sy = 0 } else { movingY = true }
            }

            // Small-move boost to increase fine movement without sacrificing stop
            if sx != 0 {
                let mag = abs(sx)
                if mag < smallBoostThreshold {
                    let boosted = max(mag * smallBoostGain, smallStepFloor)
                    sx = (sx > 0) ? boosted : -boosted
                }
            }
            if sy != 0 {
                let mag = abs(sy)
                if mag < smallBoostThreshold {
                    let boosted = max(mag * smallBoostGain, smallStepFloor)
                    sy = (sy > 0) ? boosted : -boosted
                }
            }
            // Reduced verbose per-move logging
            X68000_Mouse_Event(0, sx, sy)
            X68000_Mouse_Event(1, leftDown ? 1.0 : 0.0, 0.0)
            X68000_Mouse_Event(2, rightDown ? 1.0 : 0.0, 0.0)
            // Do NOT push absolute/direct updates in capture mode; it causes cursor ghosting
            dx = 0.0
            dy = 0.0
        } else if current_button_state != lastSentButtonState {
            // Button-only change: route via Mouse_Event for SCC path
            let leftDown = (current_button_state & 0x1) != 0
            let rightDown = (current_button_state & 0x2) != 0
            X68000_Mouse_Event(1, leftDown ? 1.0 : 0.0, 0.0)
            X68000_Mouse_Event(2, rightDown ? 1.0 : 0.0, 0.0)
            // Reduced verbose per-button-change logging
            // Avoid absolute/direct updates here to prevent ghosting
        }
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
        dy += (y - old_y) * mouseSensitivity * -1.0
        // Clamp accumulated deltas to SCC 8-bit safe range per frame
        let maxStep: Float = 0.10 // reduce per-frame movement to improve precision
        if dx > maxStep { dx = maxStep }
        if dx < -maxStep { dx = -maxStep }
        if dy > maxStep { dy = maxStep }
        if dy < -maxStep { dy = -maxStep }
        
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
    func sendDirectUpdate(forceAbsolute: Bool = false) {
        // In non-capture mode, do not send position updates on host mouse moves
        // This function is now only used for button updates in non-capture or
        // explicit absolute sends when forceAbsolute is true from UI actions.
        // Require valid screen size to avoid zero output
        guard x68k_width > 0 && x68k_height > 0 else { return }
        let tx = Int(mx * x68k_width)
        let ty = Int(my * x68k_height)
        // Suppress duplicates: only send if position (in pixel) or button changed
        if !forceAbsolute && tx == lastSentTx && ty == lastSentTy && button_state == lastSentButtonState {
            return
        }
        if !forceAbsolute {
            // Do not move pointer in non-capture path; only reflect buttons
            let leftDown = (button_state & 0x1) != 0
            let rightDown = (button_state & 0x2) != 0
            X68000_Mouse_Event(1, leftDown ? 1.0 : 0.0, 0.0)
            X68000_Mouse_Event(2, rightDown ? 1.0 : 0.0, 0.0)
            lastSentButtonState = button_state
            return
        } else {
            X68000_Mouse_SetDirect(Float(tx), Float(ty), button_state)
            lastSentTx = tx
            lastSentTy = ty
            lastSentButtonState = button_state
        }
    }

        // Accumulate raw deltas (capture mode)
    func addDeltas(_ deltaX: CGFloat, _ deltaY: CGFloat) {
        // Not used in associated+absolute mode; keep for fallback
        let sx = Float(deltaX) * mouseSensitivity
        let sy = Float(deltaY) * mouseSensitivity
        dx += sx
        dy += sy
    }

    // Accumulate normalized deltas (relative to scene size)
    func addDeltasNormalized(_ ndx: CGFloat, _ ndy: CGFloat) {
        dx += Float(ndx) * mouseSensitivity
        dy += Float(ndy) * mouseSensitivity * -1.0
    }
    
    // Send only button state (no movement), for capture mode clicks
    func sendButtonOnlyUpdate() {
        // Avoid redundant sends
        if button_state != lastSentButtonState {
            let leftDown = (button_state & 0x1) != 0
            let rightDown = (button_state & 0x2) != 0
            X68000_Mouse_Event(1, leftDown ? 1.0 : 0.0, 0.0)
            X68000_Mouse_Event(2, rightDown ? 1.0 : 0.0, 0.0)
            lastSentButtonState = button_state
        }
    }

    func handleDoubleClick(_ type: Int) -> Bool { return false }

    func handleDoubleClickPress(_ type: Int) { /* no-op: let VS.X detect double-click */ }

    func consumeDoubleClickFlag(_ type: Int) -> Bool { return false }

    private func activateDoubleClickSuppression() {
        if doubleClickSuppressionActive { return }
        doubleClickSuppressionActive = true
        X68000_Mouse_SetDoubleClickInProgress(1)
    }

    private func deactivateDoubleClickSuppression() {
        if !doubleClickSuppressionActive {
            doubleClickSuppressionWorkItem?.cancel()
            doubleClickSuppressionWorkItem = nil
            doubleClickSuppressionQueueCount = 0
            return
        }
        doubleClickSuppressionActive = false
        doubleClickSuppressionWorkItem?.cancel()
        doubleClickSuppressionWorkItem = nil
        doubleClickSuppressionQueueCount = 0
        X68000_Mouse_SetDoubleClickInProgress(0)
    }
    
    // 修正: 改善された遅延リリース処理
    private func scheduleRelease(_ type: Int, delay: TimeInterval) {
        // 既存の遅延リリースをキャンセル
        scheduledReleases[type]?.cancel()
        
        let workItem = DispatchWorkItem { [weak self] in
            guard let self = self else { return }
            if self.pendingRelease.contains(type) {
                self.pendingRelease.remove(type)
                self.button_state &= ~(1<<type)
                self.sendDirectUpdate()
            }
            self.scheduledReleases[type] = nil
        }
        
        scheduledReleases[type] = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: workItem)
    }
    
    func Click(_ type: Int,_ pressed:Bool) {
        // 修正: 処理中の場合は無視
        if clickProcessing.contains(type) { return }
        
        // Ignore duplicate state
        let lastState = lastClickState[type] ?? false
        if lastState == pressed { return }

        clickProcessing.insert(type)
        defer { clickProcessing.remove(type) }
        
        lastClickState[type] = pressed

        let now = Date().timeIntervalSince1970
        // Reduced verbose click logging

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
            // 修正: デバウンス間隔の調整
            let debounce: TimeInterval = isCaptureMode ? 0.01 : 0.02
            if let lp = lastPressTime[type] {
                if now - lp < debounce { return }
            }
            lastPressTime[type] = now
            // Press: set bit; in capture mode do not arm hold to preserve exact cadence
            pendingRelease.remove(type)
            button_state |= (1<<type)
            if !isCaptureMode {
                let holdFrames = minimumHoldFrames
                holdUntilFrame[type] = frame + holdFrames
            } else {
                holdUntilFrame[type] = nil
            }
            lastClickTime[type] = now

            if type == 0 {
                // 最初のタップで抑制を明示的にオフ（直前の状態をクリア）
                if doubleClickSuppressionActive {
                    doubleClickSuppressionWorkItem?.cancel()
                    doubleClickSuppressionWorkItem = nil
                } else {
                    deactivateDoubleClickSuppression()
                }
            }
        } else {
            // Release: enforce minimum hold in both capture and non-capture
            let sinceDown = now - (lastClickTime[type] ?? now)
            let needDelay = !isCaptureMode && (sinceDown < minimumHoldSeconds)
            lastReleaseTime[type] = now

            if isCaptureMode {
                // In capture mode: release immediately
                pendingRelease.remove(type)
                holdUntilFrame[type] = nil
                button_state &= ~(1<<type)
            } else {
                if needDelay {
                    // 修正: 改善された遅延処理を使用
                    let delay = minimumHoldSeconds - sinceDown
                    pendingRelease.insert(type)
                    scheduleRelease(type, delay: delay)
                } else {
                    button_state &= ~(1<<type)
                }
            }

            if type == 0 {
                if doubleClickSuppressionActive {
                    // 2回目の解放で即時に抑制終了
                    deactivateDoubleClickSuppression()
                } else {
                    // 次のタップまでの微小移動を抑制
                    activateDoubleClickSuppression()
                    let workItem = DispatchWorkItem { [weak self] in
                        self?.deactivateDoubleClickSuppression()
                    }
                    doubleClickSuppressionWorkItem?.cancel()
                    doubleClickSuppressionWorkItem = workItem
                    DispatchQueue.main.asyncAfter(deadline: .now() + doubleClickSuppressionInterval, execute: workItem)
                }
            }
        }

        // Reduced verbose click logging
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
        
        // Keep last known normalized position; do not recenter here

        // Clear any accumulated core-side deltas to prevent initial drift
        X68000_Mouse_ResetState()
        // Do not send any absolute/relative packet here; first real movement will initialize

        // Enable core-side capture as well（リセットは済）
        X68000_Mouse_StartCapture(1)
        doubleClickQueue.removeAll()
        lastSingleClickTime.removeAll()
        doubleClickDetected.removeAll()
        doubleClickSuppressionQueueCount = 0
        deactivateDoubleClickSuppression()
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
        
        // 修正: 新しい状態管理のクリア
        clickProcessing.removeAll()
        scheduledReleases.values.forEach { $0.cancel() }
        scheduledReleases.removeAll()
        doubleClickQueue.removeAll()
        lastSingleClickTime.removeAll()
        doubleClickDetected.removeAll()
        doubleClickSuppressionQueueCount = 0
        // Do not inject corrective deltas or center here to avoid side-effects

        deactivateDoubleClickSuppression()

        // Disable core-side capture
        X68000_Mouse_StartCapture(0)
        // debugLog("Mouse controller capture mode disabled", category: .input)
    }
    
}
