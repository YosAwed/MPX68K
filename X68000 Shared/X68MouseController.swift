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
    
    // Base sensitivity (small moves stay small). Larger moves are accelerated below.
    var mouseSensitivity: Float = 0.85

    // Cache SCC compatibility mode to avoid infinite loops
    private var cachedSCCCompatMode: Int32 = 0
    private var lastSCCCompatCheck: TimeInterval = 0
    private let sccCompatCheckInterval: TimeInterval = 1.0  // Check every 1 second

    // Clamp per-frame movement in capture mode to avoid pathological spikes
    private let movementMaxStepCapture: Float = 9.0

    // Hysteresis for crisp stop/start
    private var movingX: Bool = false
    private var movingY: Bool = false
    private let startThreshold: Float = 0.02
    private let stopThreshold:  Float = 0.04

    // Acceleration: only large deltas are amplified
    // If |delta| <= accelStart, scale ~1. Above it, scale increases up to accelMaxScale.
    private let accelStart: Float = 8.0
    private let accelGain: Float = 0.03
    private let accelMaxScale: Float = 1.8
    
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
    // Target click cadence ~100ms
    private let targetClickInterval: TimeInterval = 0.10
    private let minimumHoldFrames: Int = 6 // ~60Hz => ~100ms
    private let minimumHoldSeconds: TimeInterval = 0.10
    private let minimumHoldFramesCaptureLeft: Int = 6
    private var lastPressTime: [Int: TimeInterval] = [:]
    private let pressDebounceInterval: TimeInterval = 0.10
    private var lastReleaseTime: [Int: TimeInterval] = [:]
    private let retriggerGuardInterval: TimeInterval = 0.10
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
    private let doubleClickSuppressionInterval: TimeInterval = 0.20
    private var doubleClickSuppressionQueueCount = 0
    
    // Edge throttling to normalize cadence (~100ms)
    private var lastEdgeTime: [Int: TimeInterval] = [:]
    
    var x68k_width: Float = 0.0
    var x68k_height: Float = 0.0
    
    var frame = 0

    // Frame-locked button edge scheduler (for stable cadence in capture mode)
    private var edgeQueueCount: [Int: Int] = [:]   // queued toggle count per button
    private var nextAllowedFrame: [Int: Int] = [:] // next frame an edge may be emitted

    // Safe SCC compatibility mode checker to avoid infinite loops
    private func getSCCCompatMode() -> Int32 {
        let now = Date().timeIntervalSince1970
        if now - lastSCCCompatCheck > sccCompatCheckInterval {
            // Only check C function occasionally to avoid loops
            cachedSCCCompatMode = SCC_GetCompatMode()
            lastSCCCompatCheck = now
        }
        return cachedSCCCompatMode
    }

    // Force update SCC compat mode cache (called when mode is changed externally)
    func updateSCCCompatModeCache() {
        cachedSCCCompatMode = SCC_GetCompatMode()
        lastSCCCompatCheck = Date().timeIntervalSince1970
    }

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
        
        // Frame-locked edge scheduler: apply at most one toggle per button per 6 frames (~100ms)
        for b in [0, 1] {
            let queued = edgeQueueCount[b] ?? 0
            if queued > 0 {
                let allow = nextAllowedFrame[b] ?? frame
                if frame >= allow {
                    // Toggle the bit
                    current_button_state ^= (1 << b)
                    edgeQueueCount[b] = queued - 1
                    nextAllowedFrame[b] = frame + minimumHoldFrames
                }
            }
        }

        // ダブルクリック合成イベントは廃止：VS.X側でダブルクリック判定を行う

        // Send updates only on movement or button-change
        // Clamp tiny residuals to zero to avoid inertia
        // Keep tiny sensor noise out; hysteresis handles crisp stop
        let deadEps: Float = 0.04
        if abs(dx) < deadEps { dx = 0.0 }
        if abs(dy) < deadEps { dy = 0.0 }
        var movement = (dx != 0.0 || dy != 0.0)

        // During double-click suppression window, minimal movement filtering
        // In SCC compat mode, allow all movement for VS.X compatibility
        if doubleClickSuppressionActive && getSCCCompatMode() == 0 {
            // Only suppress large movements, allow micro-movements for VS.X
            let suppressThreshold: Float = 0.5
            if abs(dx) > suppressThreshold { dx = 0.0 }
            if abs(dy) > suppressThreshold { dy = 0.0 }
            movement = (dx != 0.0 || dy != 0.0)
        }
        // Prefer movement packets; if no movement, send button-only packet to avoid jitter
        if movement {
            // Keep button state current via Mouse_Event so MouseStat is correct
            let leftDown = (current_button_state & 0x1) != 0
            let rightDown = (current_button_state & 0x2) != 0
            // Send relative movement via SCC mouse queue; core inverts Y internally
            var sx = dx
            var sy = dy
            
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

            // Acceleration: only amplify large instantaneous deltas
            func accel(_ v: Float) -> Float {
                if v == 0 { return 0 }
                let sign: Float = v > 0 ? 1.0 : -1.0
                let mag = abs(v)
                if mag <= accelStart { return v }
                let over = mag - accelStart
                let scale = min(1.0 + over * accelGain, accelMaxScale)
                return sign * mag * scale
            }
            sx = accel(sx)
            sy = accel(sy)

            // Final clamp to avoid pathological spikes after acceleration
            if sx > movementMaxStepCapture { sx = movementMaxStepCapture }
            if sx < -movementMaxStepCapture { sx = -movementMaxStepCapture }
            if sy > movementMaxStepCapture { sy = movementMaxStepCapture }
            if sy < -movementMaxStepCapture { sy = -movementMaxStepCapture }
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
        let maxStep: Float = 0.20 // allow faster movement for better responsiveness
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
                // In capture mode, Update() will deliver state via Mouse_Event
                if !self.isCaptureMode {
                    self.sendDirectUpdate()
                }
            }
            self.scheduledReleases[type] = nil
        }
        
        scheduledReleases[type] = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: workItem)
    }
    
    func Click(_ type: Int,_ pressed:Bool) {
        // In capture mode, queue button edges and emit them on frame boundaries
        if isCaptureMode {
            // Ignore duplicate physical state
            let lastState = lastClickState[type] ?? false
            if lastState == pressed { return }
            lastClickState[type] = pressed

            // Each transition request enqueues one toggle
            edgeQueueCount[type] = (edgeQueueCount[type] ?? 0) + 1
            // Initialize allowance if not set
            if nextAllowedFrame[type] == nil { nextAllowedFrame[type] = frame }

            // Do not mutate button_state here; Update() will emit with stable cadence
            return
        }

        // Non-capture path: keep existing immediate behavior with light debounce
        // 修正: 処理中の場合は無視
        if clickProcessing.contains(type) { return }
        let lastState = lastClickState[type] ?? false
        if lastState == pressed { return }
        clickProcessing.insert(type)
        defer { clickProcessing.remove(type) }
        lastClickState[type] = pressed
        let now = Date().timeIntervalSince1970
        // Short debounce
        let debounce: TimeInterval = 0.02
        if pressed {
            if let lp = lastPressTime[type], now - lp < debounce { return }
            lastPressTime[type] = now
            button_state |= (1<<type)
        } else {
            lastReleaseTime[type] = now
            button_state &= ~(1<<type)
        }
        sendDirectUpdate()
    }
    func ClickOnce()
    {
        debugLog("\(frame): ClickOnce", category: .input)
        click_flag = 2
    }
    
    // MARK: - Mouse Capture Mode Management
    
    func enableCaptureMode() {
        isCaptureMode = true

        // Initialize SCC compat mode cache
        cachedSCCCompatMode = SCC_GetCompatMode()
        lastSCCCompatCheck = Date().timeIntervalSince1970

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
