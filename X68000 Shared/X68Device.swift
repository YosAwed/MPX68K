//
//  X68Device.swift
//  X68000
//
//  Created by GOROman on 2020/04/08.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Foundation
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

class X68Device {
    
    
    func Reset() {
    }
    
    func Update(_ currentTime: TimeInterval) {
    }
    
    #if os(iOS)
    func touchesBegan(_ touches: Set<UITouch>) {
    }
    
    func touchesMoved(_ touches: Set<UITouch>) {
    }

    func touchesEnded(_ touches: Set<UITouch>) {
    }
    #endif
}
