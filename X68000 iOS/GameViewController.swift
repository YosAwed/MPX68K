//
//  GameViewController.swift
//  X68000 iOS
//
//  Created by GOROman on 2020/03/28.
//  Copyright © 2020 GOROman. All rights reserved.
//

import UIKit
import SpriteKit
import GameplayKit

class GameViewController: UIViewController {
    
    var gameScene : GameScene?
    var configScene : ConfigScene?

    // ディスクイメージのロード(AppDelegateから呼ばれる)
    func load(_ url : URL)
    {
        gameScene?.load( url: url )
    }

    //MARK: -
    @objc func timerUpdate() {
        print(#function)
        let skView = self.view as! SKView
        skView.presentScene(configScene!, transition: .crossFade(withDuration: 2))
    }
    
    //MARK: -
    override func viewDidLoad() {
        super.viewDidLoad()
        
        let appDelegate:AppDelegate = UIApplication.shared.delegate as! AppDelegate
        
        //        let appDelegate = UIApplication.shared.delegate as! AppDelegate
        appDelegate.viewController = self
        
        gameScene = GameScene.newGameScene()
        configScene = ConfigScene.newScene()
        
        // Present the scene
        let skView = self.view as! SKView
        skView.presentScene(gameScene)
        skView.ignoresSiblingOrder = true
        skView.showsFPS = true
        skView.showsNodeCount = true
        skView.showsDrawCount = true

			//		gameScene?.backgroundColor = .clear
//		skView.allowsTransparency = true
//		skView.backgroundColor = .clear

        
//          Timer.scheduledTimer(timeInterval: 3, target: self, selector: #selector(GameViewController.timerUpdate), userInfo: nil, repeats: false)

//        skView.preferredFramesPerSecond = 120
        
    }
	
	override func viewDidAppear(_ animated: Bool) {
		print(#function)

	}
	override func viewWillAppear(_ animated: Bool) {
		print(#function)
	}
	override func viewDidDisappear(_ animated: Bool) {
		print(#function)

	}

    //MARK: -
    func applicationWillResignActive() {
        gameScene?.applicationWillResignActive()
    }

    //MARK: -
    func applicationWillEnterForeground() {
        gameScene?.applicationWillEnterForeground()
    }

    //MARK: -
    override var shouldAutorotate: Bool {
        return true
    }
    
    //MARK: -
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask {
        if UIDevice.current.userInterfaceIdiom == .phone {
            return .allButUpsideDown
        } else {
            return .all
        }
    }
    
    //MARK: -
    override var prefersStatusBarHidden: Bool {
        return true
    }
    
    //MARK: -
    override func becomeFirstResponder() -> Bool {
        return true
    }
    
    //MARK: -
    override var keyCommands: [UIKeyCommand]? {
        
        return [
            UIKeyCommand(input: "0", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "1", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "2", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "3", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "4", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "5", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "6", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "7", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "8", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "9", modifierFlags: [], action: #selector(selectKey(sender:))),
            
            UIKeyCommand(input: "a", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "b", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "c", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "d", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "e", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "f", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "g", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "h", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "i", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "j", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "k", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "l", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "m", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "n", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "o", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "p", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "q", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "r", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "s", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "t", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "u", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "v", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "w", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "x", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "y", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "z", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "-", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "=", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: ":", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: ".", modifierFlags: [], action: #selector(selectKey(sender:))),
            
            UIKeyCommand(input: "!", modifierFlags: [.shift], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "\r", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "\t", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "\u{8}", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "\u{9}", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: "\u{10}", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: " ", modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: UIKeyCommand.inputUpArrow, modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: UIKeyCommand.inputDownArrow, modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: UIKeyCommand.inputLeftArrow, modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: UIKeyCommand.inputRightArrow, modifierFlags: [], action: #selector(selectKey(sender:))),
            UIKeyCommand(input: UIKeyCommand.inputEscape, modifierFlags: [], action: #selector(selectKey(sender:))),
        ]
        
        
    }
    
    func sendKey(_ code : UInt32 )
    {
        X68000_Key_Down(code);
        X68000_Key_Up(code);
    }
    ////    case 123:          ret = 0x114;           break;    // ←
    //   case 124:          ret = 0x113;           break;    // →
    //   case 125:          ret = 0x112;           break;    // ↓
    //   case 126:          ret = 0x111;           break;    // ↑
    
    @objc func selectKey(sender: UIKeyCommand) {
        if ( sender.modifierFlags == .shift ) {
            switch sender.input {
            case "!":   sendKey( 0x21 );
            default:
                print( sender.input ?? "" )
            }
        } else {
            switch sender.input {
            case UIKeyCommand.inputEscape: sendKey( 0x1b  )
            case UIKeyCommand.inputUpArrow: sendKey( 0x111  )
            case UIKeyCommand.inputDownArrow: sendKey( 0x112  )
            case UIKeyCommand.inputLeftArrow: sendKey( 0x114  )
            case UIKeyCommand.inputRightArrow: sendKey( 0x113  )
            case "\t":   sendKey( 0x08 );
            case " ":   sendKey( 0x20 );
                
            case "0":   sendKey( 0x30 );
            case "1":   sendKey( 0x31 );
            case "2":   sendKey( 0x32 );
            case "3":   sendKey( 0x33 );
            case "4":   sendKey( 0x34 );
            case "5":   sendKey( 0x35 );
            case "6":   sendKey( 0x36 );
            case "7":   sendKey( 0x37 );
            case "8":   sendKey( 0x38 );
            case "9":   sendKey( 0x39 );
                
            case "a":   sendKey( 0x41 );
            case "b":   sendKey( 0x42 );
            case "c":   sendKey( 0x43 );
            case "d":   sendKey( 0x44 );
            case "e":   sendKey( 0x45 );
            case "f":   sendKey( 0x46 );
            case "g":   sendKey( 0x47 );
            case "h":   sendKey( 0x48 );
            case "i":   sendKey( 0x49 );
            case "j":   sendKey( 0x4a );
            case "k":   sendKey( 0x4b );
            case "l":   sendKey( 0x4c );
            case "m":   sendKey( 0x4d );
            case "n":   sendKey( 0x4e );
            case "o":   sendKey( 0x4f );
            case "p":   sendKey( 0x50 );
            case "q":   sendKey( 0x51 );
            case "r":   sendKey( 0x52 );
            case "s":   sendKey( 0x53 );
            case "t":   sendKey( 0x54 );
            case "u":   sendKey( 0x55 );
            case "v":   sendKey( 0x56 );
            case "w":   sendKey( 0x57 );
            case "x":   sendKey( 0x58 );
            case "y":   sendKey( 0x59 );
            case "z":   sendKey( 0x5a );
            case "\r":  sendKey( 0x0d );
            case "\u{8}":  sendKey( 0x08 );
                
            case "-":   sendKey( 0x2d );
            case "=":   sendKey( 0x3d );
            case ":":   sendKey( 0x3a );
            case ".":   sendKey( 0x2e );
            default:
                print( sender.input ?? "" )
            }
            
        }
    }
}
