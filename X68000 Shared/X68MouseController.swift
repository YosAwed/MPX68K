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
    var mouseSensitivity: Float = 0.3
    
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
    private let minimumHoldSeconds: TimeInterval = 0.03 // 修正: 0.06 -> 0.03
    private let minimumHoldFramesCaptureLeft: Int = 3
    private var lastPressTime: [Int: TimeInterval] = [:]
    private let pressDebounceInterval: TimeInterval = 0.12
    private var lastReleaseTime: [Int: TimeInterval] = [:]
    private let retriggerGuardInterval: TimeInterval = 0.08 // 修正: 0.15 -> 0.08
    private let pulseLeftClickInCapture: Bool = false
    private let pulseHoldFrames: Int = 3
    
    // 新規追加: ダブルクリック処理
    private var doubleClickWindow: TimeInterval = 0.45
    private var doubleClickPulseGap: TimeInterval = 0.12
    private var doubleClickQueue: [(frame: Int, type: Int, pressed: Bool)] = []
    private var lastSingleClickTime: [Int: TimeInterval] = [:]
    private var doubleClickDetected: [Int: Bool] = [:]
    
    // 新規追加: 状態管理の改善
    private var clickProcessing: Set<Int> = []
    private var scheduledReleases: [Int: DispatchWorkItem] = [:]
    
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

        // Apply scheduled double-click pulses (frame-synchronous) – ensure SCC sees a packet
        if !doubleClickQueue.isEmpty {
            var applied = false
            doubleClickQueue.removeAll { item in
                if item.frame <= frame {
                    if item.pressed { button_state |= (1 << item.type) }
                    else { button_state &= ~(1 << item.type) }
                    applied = true
                    return true
                }
                return false
            }
            if applied {
                current_button_state = button_state
        // Force a button-only packet even if no movement this frame (twice for reliability)
                let l = (current_button_state & 0x1) != 0
                let r = (current_button_state & 0x2) != 0
                X68000_Mouse_Event(1, l ? 1.0 : 0.0, 0.0)
                X68000_Mouse_Event(2, r ? 1.0 : 0.0, 0.0)
                X68000_Mouse_Event(1, l ? 1.0 : 0.0, 0.0)
                X68000_Mouse_Event(2, r ? 1.0 : 0.0, 0.0)
                lastSentButtonState = current_button_state
            }
        }

        // Send updates only on movement or button-change
        // Clamp tiny residuals to zero to avoid inertia
        let deadEps: Float = 0.06
        if abs(dx) < deadEps { dx = 0.0 }
        if abs(dy) < deadEps { dy = 0.0 }
        let movement = (dx != 0.0 || dy != 0.0)
        // Prefer movement packets; if no movement, send button-only packet to avoid jitter
        if movement {
            // Keep button state current via Mouse_Event so MouseStat is correct
            let leftDown = (current_button_state & 0x1) != 0
            let rightDown = (current_button_state & 0x2) != 0
            X68000_Mouse_Event(1, leftDown ? 1.0 : 0.0, 0.0)
            X68000_Mouse_Event(2, rightDown ? 1.0 : 0.0, 0.0)
            // Send relative movement directly to SCC range (no screen scaling)
            X68000_Mouse_Set(dx, dy, current_button_state)
            dx = 0.0
            dy = 0.0
        } else if current_button_state != lastSentButtonState {
            // Button-only change: route via Mouse_Event for SCC path
            let leftDown = (current_button_state & 0x1) != 0
            let rightDown = (current_button_state & 0x2) != 0
            X68000_Mouse_Event(1, leftDown ? 1.0 : 0.0, 0.0)
            X68000_Mouse_Event(2, rightDown ? 1.0 : 0.0, 0.0)
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
    
    // 新規追加: ダブルクリック判定処理
    func handleDoubleClick(_ type: Int) -> Bool {
        let now = Date().timeIntervalSince1970
        if let lastClick = lastSingleClickTime[type] {
            if now - lastClick <= doubleClickWindow {
                doubleClickDetected[type] = true
                infoLog("🖱️ Double-click detected for button \(type)", category: .input)
                return true
            }
        }
        lastSingleClickTime[type] = now
        doubleClickDetected[type] = false
        return false
    }
    
    // 新規追加: ダブルクリック専用処理
    func handleDoubleClickPress(_ type: Int) {
        // フレーム同期の二度押し（SCCのサンプリングに合わせる）
        let k = 2 // 約36ms間隔 @55Hz（短めにして反応優先）
        let nowF = self.frame
        doubleClickQueue.removeAll { $0.type == type }
        doubleClickQueue.append((frame: nowF + 0, type: type, pressed: true))
        doubleClickQueue.append((frame: nowF + k, type: type, pressed: false))
        doubleClickQueue.append((frame: nowF + 2*k, type: type, pressed: true))
        doubleClickQueue.append((frame: nowF + 3*k, type: type, pressed: false))
    }

    // 新規追加: ダブルクリック消費フラグ
    // GameViewController 側で直近のダブルクリック判定を一度だけ無視するために使用
    func consumeDoubleClickFlag(_ type: Int) -> Bool {
        let flag = doubleClickDetected[type] ?? false
        if flag {
            doubleClickDetected[type] = false
        }
        return flag
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
            // 修正: デバウンス間隔の調整
            let debounce: TimeInterval = isCaptureMode ? 0.01 : 0.02
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
                    // 修正: 改善された遅延処理を使用
                    let delay = minimumHoldSeconds - sinceDown
                    pendingRelease.insert(type)
                    scheduleRelease(type, delay: delay)
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
        
        // Keep last known normalized position; do not recenter here
        
        // Clear any accumulated core-side deltas to prevent initial drift
        X68000_Mouse_ResetState()
        // Do not send any absolute/relative packet here; first real movement will initialize
        
        // Enable core-side capture as well（リセットは済）
        X68000_Mouse_StartCapture(1)
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
        lastSingleClickTime.removeAll()
        doubleClickDetected.removeAll()
        
        // Do not inject corrective deltas or center here to avoid side-effects
        
        // Disable core-side capture
        X68000_Mouse_StartCapture(0)
        // debugLog("Mouse controller capture mode disabled", category: .input)
    }
    
}

