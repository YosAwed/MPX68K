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

@available(iOS 13.4, *)
class GameViewController: UIViewController, UIContextMenuInteractionDelegate {
	func contextMenuInteraction(_ interaction: UIContextMenuInteraction, configurationForMenuAtLocation location: CGPoint) -> UIContextMenuConfiguration? {
		print("Context Menu")
		
		return nil
	}
	
    
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
        
		view.isUserInteractionEnabled = true
		let interaction = UIContextMenuInteraction(delegate: self)
		
		view.addInteraction(interaction)
		
		
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

	private func iOStoX68(_ keyCode :  UIKeyboardHIDUsage  ) -> UInt32 {
		switch keyCode {
		case .keyboardReturnOrEnter:	 return 0x0d
//		case .keyboardTab: 				 return 0x07
		case .keyboardDeleteOrBackspace: return 0x08
//		case .keyboardEscape: 			 return 0x1b
		case .keyboardSpacebar:			 return 0x20
		case .keyboardEscape:			 return 0xff	// 登録キー
		case .keyboardTab:			 return 0xfe	// 登録キー

		case .keyboardEqualSign: return 0x40	// +
		case .keyboardComma: 	return 0x2c
			
		case .keyboardHyphen:	return 0x2d
		case .keyboardPeriod:	return 0x2e
		case .keyboardSlash:	return 0x2f
			
		case .keyboardSemicolon: return 0x3a
		case .keyboardQuote: return 0x2b



		case .keyboardLeftShift:	return 0x130
		case .keyboardRightShift:	return 0x130
		case .keyboardLeftControl:	return 0x1e3
		case .keyboardRightControl:	return 0x1e3


		case .keyboard0:			return 0x30
		case .keyboard1:			return 0x31
		case .keyboard2:			return 0x32
		case .keyboard3: 			return 0x33
		case .keyboard4: 			return 0x34
		case .keyboard5: 			return 0x35
		case .keyboard6: 			return 0x36
		case .keyboard7:			return 0x37
		case .keyboard8:			return 0x38
		case .keyboard9:			return 0x39

		case .keyboardErrorRollOver:	return 0x00
		case .keyboardPOSTFail:			return 0x00
		case .keyboardErrorUndefined:return 0x00
			
		case .keyboardA:	return 0x41
		case .keyboardB:	return 0x42
		case .keyboardC:	return 0x43
		case .keyboardD:	return 0x44
		case .keyboardE:	return 0x45
		case .keyboardF:	return 0x46
		case .keyboardG:	return 0x47
		case .keyboardH:	return 0x48
		case .keyboardI:	return 0x49
		case .keyboardJ: return 0x4a
		case .keyboardK: return 0x4b
		case .keyboardL: return 0x4c
		case .keyboardM: return 0x4d
		case .keyboardN: return 0x4e
		case .keyboardO: return 0x4f
		case .keyboardP: return 0x50
		case .keyboardQ: return 0x51
		case .keyboardR: return 0x52
		case .keyboardS: return 0x53
		case .keyboardT: return 0x54
		case .keyboardU: return 0x55
		case .keyboardV: return 0x56
		case .keyboardW: return 0x57
		case .keyboardX: return 0x58
		case .keyboardY: return 0x59
		case .keyboardZ: return 0x5a
		case .keyboardOpenBracket:	return 0x5b
		case .keyboardBackslash:	return 0x5c
		case .keyboardCloseBracket:	return 0x5d

		case .keyboardUpArrow:		return 0x111
		case .keyboardDownArrow:	return 0x112
		case .keyboardRightArrow:	return 0x113
		case .keyboardLeftArrow:	return 0x114


		case .keyboardF1: return 0x1be
		case .keyboardF2: return 0x1bf
		case .keyboardF3: return 0x1c0
		case .keyboardF4: return 0x1c1
		case .keyboardF5: return 0x1c2
		case .keyboardF6: return 0x1c3
		case .keyboardF7: return 0x1c4
		case .keyboardF8: return 0x1c5
		case .keyboardF9: return 0x1c6
		case .keyboardF10: return 0x1c7
		case .keyboardF11: return 0x1c8
		case .keyboardF12: return 0x1c9
			
		case .keyboardGraveAccentAndTilde:	return 0x7e

		case .keyboardCapsLock:
			print("CAPS")
			return 0xe5
		case .keyboardInsert:	return 0x15a
		case .keyboardHome:
			print("HOME")
			return 0x150
		case .keyboardEnd:
			print("END")
			return 0x158
		case .keyboardPageUp:	return 0x157
		case .keyboardPageDown:	return 0x156
		case .keyboardLeftAlt: return 0xff	// [HELP]
		case .keyboardRightAlt:	return 0xfe // [登録]

/*
			

		case .keyboardNonUSPound:
			
			
			
			
		case .keyboardPrintScreen:
			
		case .keyboardScrollLock:
			
		case .keyboardPause:
			
			
		case .keyboardDeleteForward:
			
			
		case .keypadNumLock:
			
		case .keypadSlash:
			
		case .keypadAsterisk:
			
		case .keypadHyphen:
			
		case .keypadPlus:
			
		case .keypadEnter:
			
		case .keypad1:
			
		case .keypad2:
			
		case .keypad3:
			
		case .keypad4:
			
		case .keypad5:
			
		case .keypad6:
			
		case .keypad7:
			
		case .keypad8:
			
		case .keypad9:
			
		case .keypad0:
			
		case .keypadPeriod:
			
		case .keyboardNonUSBackslash:
			
		case .keyboardApplication:
			
		case .keyboardPower:
			
		case .keypadEqualSign:
			
		case .keyboardF13:
			
		case .keyboardF14:
			
		case .keyboardF15:
			
		case .keyboardF16:
			
		case .keyboardF17:
			
		case .keyboardF18:
			
		case .keyboardF19:
			
		case .keyboardF20:
			
		case .keyboardF21:
			
		case .keyboardF22:
			
		case .keyboardF23:
			
		case .keyboardF24:
			
		case .keyboardExecute:
			
		case .keyboardHelp:
			
		case .keyboardMenu:
			
		case .keyboardSelect:
			
		case .keyboardStop:
			
		case .keyboardAgain:
			
		case .keyboardUndo:
			
		case .keyboardCut:
			
		case .keyboardCopy:
			
		case .keyboardPaste:
			
		case .keyboardFind:
			
		case .keyboardMute:
			
		case .keyboardVolumeUp:
			
		case .keyboardVolumeDown:
			
		case .keyboardLockingCapsLock:
			
		case .keyboardLockingNumLock:
			
		case .keyboardLockingScrollLock:
			
			
		case .keypadEqualSignAS400:
			
		case .keyboardInternational1:
			
		case .keyboardInternational2:
			
		case .keyboardInternational3:
			
		case .keyboardInternational4:
			
		case .keyboardInternational5:
			
		case .keyboardInternational6:
			
		case .keyboardInternational7:
			
		case .keyboardInternational8:
			
		case .keyboardInternational9:
			
		case .keyboardLANG1:
			
		case .keyboardLANG2:
			
		case .keyboardLANG3:
			
		case .keyboardLANG4:
			
		case .keyboardLANG5:
			
		case .keyboardLANG6:
			
		case .keyboardLANG7:
			
		case .keyboardLANG8:
			
		case .keyboardLANG9:
			
		case .keyboardAlternateErase:
			
		case .keyboardSysReqOrAttention:
			
		case .keyboardCancel:
			
		case .keyboardClear:
			
		case .keyboardPrior:
			
		case .keyboardReturn:
			
		case .keyboardSeparator:
			
		case .keyboardOut:
			
		case .keyboardOper:
			
		case .keyboardClearOrAgain:
			
		case .keyboardCrSelOrProps:
			
		case .keyboardExSel:
			
			
			
			
		case .keyboardLeftGUI:
			
			
			
			
		case .keyboardRightGUI:
			
		case .keyboard_Reserved:
				
*/
		@unknown default:
			return 0x00
		}
		return 0x00
	}
    //MARK: - https://www.hackingwithswift.com/example-code/uikit/how-to-detect-keyboard-input-using-pressesbegan-and-pressesended
	override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
		for a in presses {
			let key = a.key!
			//guard let key = presses.first?.key else { return }
			print("KeyBegan:\(key.keyCode.rawValue)")
			
			X68000_Key_Down( iOStoX68(key.keyCode) )
		}
//			super.pressesBegan(presses, with: event)
	}
	override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
//		guard let key = presses.first?.key else { return }
		for a in presses {
			let key = a.key!
			print("KeyEnded:\(key.keyCode.rawValue)")
			X68000_Key_Up( iOStoX68(key.keyCode))
		}
//			super.pressesEnded(presses, with: event)
	}
	

     ////    case 123:          ret = 0x114;           break;    // ←
    //   case 124:          ret = 0x113;           break;    // →
    //   case 125:          ret = 0x112;           break;    // ↓
    //   case 126:          ret = 0x111;           break;    // ↑
    
    func saveSRAM() {
        print("🐛 iOS GameViewController.saveSRAM() called")
        if let skView = self.view as? SKView,
           let gameScene = skView.scene as? GameScene {
            gameScene.fileSystem?.saveSRAM()
        }
    }
}
