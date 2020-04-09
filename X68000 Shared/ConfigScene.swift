//
//  ConfigScen e.swift
//  X68000
//
//  Created by GOROman on 2020/04/09.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Foundation
import SpriteKit

class ConfigScene : SKScene {
    class func newScene() -> ConfigScene {
        guard let scene = ConfigScene(fileNamed: "ConfigScene") as? ConfigScene else {
            print("Failed to load ConfigScene.sks")
            abort()
        }
        return scene
    }
        
}
