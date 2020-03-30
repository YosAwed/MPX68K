//
//  JoyController.swift
//  X68000
//
//  Created by GOROman on 2020/03/30.
//  Copyright © 2020 GOROman. All rights reserved.
//

import GameController
import UserNotifications

let  JOY_UP   : UInt8 = 0x01
let  JOY_DOWN : UInt8 = 0x02
let  JOY_LEFT : UInt8 = 0x04
let  JOY_RIGHT: UInt8 = 0x08
let  JOY_TRG2 : UInt8 = 0x20
let  JOY_TRG1 : UInt8 = 0x40

class JoyController {

    var joydata : UInt8 = 0x00

    func initNotificationSetupCheck() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert])
        { (success, error) in
            if success {
                print("許可")
            } else {
                print("拒否")
            }
        }
    }
    // Setup: Game Controller
    func setup() {
        
        initNotificationSetupCheck()
        
        NotificationCenter.default.addObserver(
            self, selector: #selector(self.handleControllerDidConnect),
            name: NSNotification.Name.GCControllerDidConnect, object: nil)
        
        NotificationCenter.default.addObserver(
            self, selector: #selector(self.handleControllerDidDisconnect),
            name: NSNotification.Name.GCControllerDidDisconnect, object: nil)
        
        guard let controller = GCController.controllers().first else {
            return
        }
        registerGameController(controller)
    }
    
    // Notification: Connection
    @objc
    func handleControllerDidConnect(_ notification: Notification){
        print("ゲームコントローラーの接続が通知されました")

        guard let gameController = notification.object as? GCController else {
            return
        }

        registerGameController(gameController)
    }
    
    // Notification: Disconnection
    @objc
    func handleControllerDidDisconnect(_ notification: Notification){
        print("ゲームコントローラーの切断が通知されました")
        
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
        print("ゲームコントローラーが接続されました")
        print("Name: \(gameController.vendorName!)")
        print("Category: \(gameController.productCategory)")
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
            buttonX!.valueChangedHandler = buttonPutBox()
        }
        
        if buttonY != nil {
            buttonY!.valueChangedHandler = buttonResetScene()
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
        print("ゲームコントローラーが切断されました")
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
    func JoySet(_ port:UInt8,_ type:UInt8,_ pressed: Bool )
    {
        if ( pressed ) {
            joydata |= type
        } else {
            joydata &= ~type
        }
        X68000_Joystick_Set(port, joydata)

    }
    // Closure: Hide
    func onButtonA() -> GCControllerButtonValueChangedHandler {
        return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
            // ○
            self.JoySet(0, JOY_TRG1, pressed )
        }
    }
    
    // Closure: Put Box
    func buttonPutBox() -> GCControllerButtonValueChangedHandler {
        return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
            print("C")
        }
    }
    
    // Closure: Reset Scene
    func buttonResetScene() -> GCControllerButtonValueChangedHandler {
        return {(_ button: GCControllerButtonInput, _ value: Float, _ pressed: Bool) -> Void in
            print("D")
        }
    }
}
