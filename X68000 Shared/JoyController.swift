//
//  JoyController.swift
//  X68000
//
//  Created by GOROman on 2020/03/30.
//  Copyright © 2020 GOROman. All rights reserved.
//

import GameController
import IOKit.hid
import UserNotifications

let  JOY_UP   : UInt8 = 0x01
let  JOY_DOWN : UInt8 = 0x02
let  JOY_LEFT : UInt8 = 0x04
let  JOY_RIGHT: UInt8 = 0x08
let  JOY_TRG2 : UInt8 = 0x20
let  JOY_TRG1 : UInt8 = 0x40

// 連射用
let RAPID_FIRE_INTERVAL = 1.0 / 30;   //　連写速度 sec

class JoyController {

    var joydata : UInt8 = 0x00
    // State from generic USB HID joysticks that GameController does not
    // recognize (e.g. the X68000 Z JOYCARD). Merged with joydata on output.
    private var hidJoydata : UInt8 = 0x00
    private var hidManager : IOHIDManager?
    
    func initNotificationSetupCheck() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert])
        { (success, error) in
            if success {
                debugLog("Notification permission granted", category: .input)
            } else {
                warningLog("Notification permission denied", category: .input)
            }
        }
    }
    enum Status {
        case Unknown
        case Connected
        case Disconnected
    }
    var event : Optional<(Status) -> Void>
    
    init() {
        event = nil
    }
    //callback: () -> Void
    // Setup: Game Controller
    func setup( callback: @escaping (Status) -> Void ) {
        self.event = callback

        initNotificationSetupCheck()
        
        NotificationCenter.default.addObserver(
            self, selector: #selector(self.handleControllerDidConnect),
            name: NSNotification.Name.GCControllerDidConnect, object: nil)
        
        NotificationCenter.default.addObserver(
            self, selector: #selector(self.handleControllerDidDisconnect),
            name: NSNotification.Name.GCControllerDidDisconnect, object: nil)
        
        setupHIDJoysticks()

        guard let controller = GCController.controllers().first else {
            return
        }
        registerGameController(controller)
    }

    // MARK: - Generic HID joysticks

    private func setupHIDJoysticks() {
        let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
        let matching: [[String: Int]] = [
            [kIOHIDDeviceUsagePageKey: kHIDPage_GenericDesktop, kIOHIDDeviceUsageKey: kHIDUsage_GD_Joystick],
            [kIOHIDDeviceUsagePageKey: kHIDPage_GenericDesktop, kIOHIDDeviceUsageKey: kHIDUsage_GD_GamePad],
        ]
        IOHIDManagerSetDeviceMatchingMultiple(manager, matching as CFArray)

        let context = Unmanaged.passUnretained(self).toOpaque()
        IOHIDManagerRegisterDeviceMatchingCallback(manager, { context, _, _, device in
            guard let context = context else { return }
            Unmanaged<JoyController>.fromOpaque(context).takeUnretainedValue().hidDeviceMatched(device)
        }, context)
        IOHIDManagerRegisterDeviceRemovalCallback(manager, { context, _, _, device in
            guard let context = context else { return }
            Unmanaged<JoyController>.fromOpaque(context).takeUnretainedValue().hidDeviceRemoved(device)
        }, context)
        IOHIDManagerRegisterInputValueCallback(manager, { context, _, _, value in
            guard let context = context else { return }
            Unmanaged<JoyController>.fromOpaque(context).takeUnretainedValue().hidValueChanged(value)
        }, context)

        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
        let result = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        if result != kIOReturnSuccess {
            warningLog("IOHIDManagerOpen failed: \(result)", category: .input)
        }
        hidManager = manager
    }

    // Devices GameController actually surfaced (Xbox, DualSense, MFi...)
    // are left to registerGameController so they are not read twice.
    // GCController.supportsHIDDevice is not usable for this: it returns
    // true for the X68000 Z JOYCARD, which GameController never surfaces.
    private func isHandledByGameController(_ device: IOHIDDevice) -> Bool {
        let name = hidDeviceName(device)
        return GCController.controllers().contains { $0.vendorName == name }
    }

    private func hidDeviceName(_ device: IOHIDDevice) -> String {
        return IOHIDDeviceGetProperty(device, kIOHIDProductKey as CFString) as? String ?? "Unknown"
    }

    private func hidDeviceMatched(_ device: IOHIDDevice) {
        if isHandledByGameController(device) { return }
        infoLog("HID joystick connected: \(hidDeviceName(device))", category: .input)
        self.event?(.Connected)
    }

    private func hidDeviceRemoved(_ device: IOHIDDevice) {
        if isHandledByGameController(device) { return }
        infoLog("HID joystick disconnected: \(hidDeviceName(device))", category: .input)
        hidJoydata = 0
        X68000_Joystick_Set(0, joydata | hidJoydata)
        self.event?(.Disconnected)
    }

    private func hidSet(_ type: UInt8, _ on: Bool) {
        if on {
            hidJoydata |= type
        } else {
            hidJoydata &= ~type
        }
    }

    private func hidValueChanged(_ value: IOHIDValue) {
        let element = IOHIDValueGetElement(value)
        let device = IOHIDElementGetDevice(element)
        if isHandledByGameController(device) { return }

        let usagePage = Int(IOHIDElementGetUsagePage(element))
        let usage = Int(IOHIDElementGetUsage(element))
        let raw = IOHIDValueGetIntegerValue(value)
        let old = hidJoydata

        switch usagePage {
        case kHIDPage_GenericDesktop:
            let minValue = IOHIDElementGetLogicalMin(element)
            let maxValue = IOHIDElementGetLogicalMax(element)
            switch usage {
            case kHIDUsage_GD_X, kHIDUsage_GD_Y:
                guard maxValue > minValue else { return }
                // Normalize to -1...1 and use a wide dead zone; digital
                // pads report only min/center/max anyway.
                let n = (Double(raw - minValue) / Double(maxValue - minValue)) * 2.0 - 1.0
                if usage == kHIDUsage_GD_X {
                    hidSet(JOY_LEFT, n < -0.5)
                    hidSet(JOY_RIGHT, n > 0.5)
                } else {
                    hidSet(JOY_UP, n < -0.5)
                    hidSet(JOY_DOWN, n > 0.5)
                }
            case kHIDUsage_GD_Hatswitch:
                // 8-way hat: 0 = up, clockwise; out of range = centered.
                let position = raw - minValue
                let count = maxValue - minValue + 1
                let centered = position < 0 || position >= count || (count != 8 && count != 4)
                let dir = centered ? -1 : (count == 4 ? position * 2 : position)
                hidSet(JOY_UP, dir == 7 || dir == 0 || dir == 1)
                hidSet(JOY_RIGHT, dir == 1 || dir == 2 || dir == 3)
                hidSet(JOY_DOWN, dir == 3 || dir == 4 || dir == 5)
                hidSet(JOY_LEFT, dir == 5 || dir == 6 || dir == 7)
            default:
                return
            }
        case kHIDPage_Button:
            // X68000 Z JOYCARD reports A as button 3 and B as button 2;
            // button 1 is also treated as A for generic pads.
            switch usage {
            case 1, 3: hidSet(JOY_TRG2, raw != 0)
            case 2: hidSet(JOY_TRG1, raw != 0)
            default:
                debugLog("HID joystick button \(usage) = \(raw)", category: .input)
                return
            }
        default:
            return
        }

        if hidJoydata != old {
            X68000_Joystick_Set(0, joydata | hidJoydata)
        }
    }
    
    // Notification: Connection
    @objc
    func handleControllerDidConnect(_ notification: Notification){
        self.event?(.Connected)
        debugLog("Game controller connection notified", category: .input)

        guard let gameController = notification.object as? GCController else {
            return
        }

        registerGameController(gameController)
    }
    
    // Notification: Disconnection
    @objc
    func handleControllerDidDisconnect(_ notification: Notification){
        self.event?(.Disconnected)
        debugLog("Game controller disconnection notified", category: .input)
        
        guard let gameController = notification.object as? GCController else {
            return
        }
        
        unregisterGameController()

        for controller: GCController in GCController.controllers() where gameController != controller {
            registerGameController(controller)
        }
    }

    // Connection
    func registerGameController(_ gameController: GCController){
#if false
        let center = UNUserNotificationCenter.current()
        // ① NotificationContent
        let content = UNMutableNotificationContent()
        content.title = "サンプルのローカルPushです"
        content.subtitle = "サンプルのsubtitleです"
        content.body = "サンプルのbodyです"
        content.badge = 1
//        content.sound = .default()

        // ② NotificationTrigger
        let trigger = UNTimeIntervalNotificationTrigger(timeInterval: 5, repeats: false)

        // ③ NotificationRequest
        let request = UNNotificationRequest(identifier: "SamplePush", content: content, trigger: trigger)

        // ④ 通知の追加
        center.add(request)
#endif
        infoLog("Game controller connected", category: .input)
        infoLog("Name: \(gameController.vendorName!)", category: .input)
        infoLog("Category: \(gameController.productCategory)", category: .input)
//        print("PlayerIndex: \(gameController.playerIndex)")
        
        var leftThumbstick:  GCControllerDirectionPad?
        var rightThumbstick: GCControllerDirectionPad?
        var directionPad:    GCControllerDirectionPad?
        
        var buttonA: GCControllerButtonInput?
        var buttonB: GCControllerButtonInput?
        var buttonX: GCControllerButtonInput?
        var buttonY: GCControllerButtonInput?
        
        var leftShoulder:  GCControllerButtonInput?
        var rightShoulder: GCControllerButtonInput?
        var leftTrigger:   GCControllerButtonInput?
        var rightTrigger:  GCControllerButtonInput?
        
        var buttonMenu:    GCControllerButtonInput?
        var buttonOptions: GCControllerButtonInput?
        
        // Extended Gamepad
        if let gamepad = gameController.extendedGamepad {
            
            directionPad    = gamepad.dpad
            leftThumbstick  = gamepad.leftThumbstick
            rightThumbstick = gamepad.rightThumbstick
            
            buttonA = gamepad.buttonA
            buttonB = gamepad.buttonB
            buttonX = gamepad.buttonX
            buttonY = gamepad.buttonY

            leftShoulder  = gamepad.leftShoulder
            rightShoulder = gamepad.rightShoulder
            leftTrigger   = gamepad.leftTrigger
            rightTrigger  = gamepad.rightTrigger

            buttonMenu    = gamepad.buttonMenu
            buttonOptions = gamepad.buttonOptions
            
        // Micro Controller (Siri Remote)
        }else if let gamepad = gameController.microGamepad {
            // Support Rotation
            gamepad.allowsRotation = true
            
            directionPad = gamepad.dpad
            buttonA = gamepad.buttonA
            buttonB = gamepad.buttonX
            
            buttonMenu = gamepad.buttonMenu
        }
#if true
        // Direction Pad
        directionPad!.valueChangedHandler = directionPadValue()
                
        // Stick
        if leftThumbstick != nil {
            leftThumbstick!.valueChangedHandler = directionPadValue()
        }
        
        if rightThumbstick != nil {
            rightThumbstick!.valueChangedHandler = cameraDirection()
        }
        
        // Buttons
        buttonMenu!.valueChangedHandler = buttonResetScene()
        buttonA!.valueChangedHandler = onButtonB()
        buttonB!.valueChangedHandler = onButtonA()
        
        if buttonX != nil {
            //buttonX!.valueChangedHandler = buttonPutBox()
            buttonX!.valueChangedHandler = rapidFireButton(JOY_TRG2)  // RAPID FIRE A
        }
        
        if buttonY != nil {
            //buttonY!.valueChangedHandler = buttonResetScene()
            buttonY!.valueChangedHandler = rapidFireButton(JOY_TRG1)  // RAPI FIRE B
        }
        
        // Shoulder
        if leftTrigger != nil {
            leftShoulder!.valueChangedHandler = onButtonA()
        }
        
        if leftTrigger != nil {
            rightShoulder!.valueChangedHandler = onButtonA()
        }
        
        // Trigger
        if leftTrigger != nil {
            leftTrigger!.valueChangedHandler = onButtonB()
        }
        
        if rightTrigger != nil {
            rightTrigger!.valueChangedHandler = onButtonB()
        }
        
        // Option
        if buttonOptions != nil {
            buttonOptions!.valueChangedHandler = buttonPutBox()
        }
#endif
        
    }
    
    // Disconnection
    func unregisterGameController() {
        infoLog("Game controller disconnected", category: .input)
    }
    func JoySet(_ port:UInt8,_ type:UInt8,_ pressed: Bool )
    {
        if ( pressed ) {
            joydata |= type
        } else {
            joydata &= ~type
        }
        X68000_Joystick_Set(port, joydata | hidJoydata)

    }

    // Closure: DirectionPad
    func directionPadValue() -> GCControllerDirectionPadValueChangedHandler {
        return {(_ dpad: GCControllerDirectionPad, _ xValue: Float, _ yValue: Float) -> Void in

//            print(dpad)
            self.JoySet(0, JOY_UP,    ( yValue >  0.5 ) )
            self.JoySet(0, JOY_DOWN,  ( yValue < -0.5 ) )
            self.JoySet(0, JOY_LEFT,  ( xValue < -0.5 ) )
            self.JoySet(0, JOY_RIGHT, ( xValue >  0.5 ) )
        }
    }
    
    // Closure: Camera Direction
    func cameraDirection() -> GCControllerDirectionPadValueChangedHandler {
        return {(_ dpad: GCControllerDirectionPad, _ xValue: Float, _ yValue: Float) -> Void in
//            self.cameraDirection = SIMD2<Float>(xValue, yValue)
        }
    }
    
    // Closure: B
    func onButtonB() -> GCControllerButtonValueChangedHandler {
        return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
            // ❌
            self.JoySet(0, JOY_TRG2, pressed )
        }
    }
    // Closure: Hide
    func onButtonA() -> GCControllerButtonValueChangedHandler {
        return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
            // ○
            self.JoySet(0, JOY_TRG1, pressed )
        }
    }
    
    // RAPID FIRE BUTTON
    //
    func rapidFireButton(_ trigger:UInt8) -> GCControllerButtonValueChangedHandler {
        var timer: Timer?   // 停止用に Timer を closure に保存
        
        return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
            if (pressed) {
                timer = Timer.scheduledTimer(
                    timeInterval: RAPID_FIRE_INTERVAL,
                    target: self,
                    selector: #selector(JoyController.rapidFire),
                    userInfo: trigger,
                    repeats: true)
            } else {
                timer?.invalidate() // 停止
            }
            self.JoySet(0, trigger, pressed)  // すぐに反応するように、初回は手動で発火
        }
    }


    // 連射関数 呼ばれるたびに、指定トリガを反転させる
    //
    @objc func rapidFire(sender:Timer) {
        let button = sender.userInfo as! UInt8
        
        self.JoySet(0, button, (self.joydata & button) == 0)
    }
    
    // Closure: Put Box
    func buttonPutBox() -> GCControllerButtonValueChangedHandler {
        return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
            debugLog("Button C pressed", category: .input)
        }
    }
    
    // Closure: Reset Scene
    func buttonResetScene() -> GCControllerButtonValueChangedHandler {
        return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
            debugLog("Button D pressed", category: .input)
        }
    }
}
