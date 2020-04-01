//
//  AppDelegate.swift
//  X68000 iOS
//
//  Created by GOROman on 2020/03/28.
//  Copyright © 2020 GOROman. All rights reserved.
//

import UIKit

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate {

    var window: UIWindow?

    var viewController: GameViewController!

    
    func application(_ app: UIApplication, open url: URL, options: [UIApplication.OpenURLOptionsKey : Any] = [:]) -> Bool {

//        print("url: ", url)


        viewController.load(url)
    
        return true
    }
/*
    func application(_ application: UIApplication, open url: URL, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        // Override point for customization after application launch.
        print("⭐️application:\(url)")
        return true
    }
 */

    func applicationWillResignActive(_ application: UIApplication) {
        // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
        // Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
        print("⭐️applicationWillResignActive")
    }

    func applicationDidEnterBackground(_ application: UIApplication) {
        // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
        // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
        print("⭐️applicationDidEnterBackground")
    }

    func applicationWillEnterForeground(_ application: UIApplication, open url: URL) {
        // Called as part of the transition from the background to the active state; here you can undo many of the changes made on entering the background.
        print("⭐️applicationWillEnterForeground:\(url)")
    }

    func applicationDidBecomeActive(_ application: UIApplication, open url: URL) {
        // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
        print("⭐️applicationDidBecomeActive:\(url) ")
    }

    func applicationWillTerminate(_ application: UIApplication) {
        // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
        print("⭐️applicationWillTerminate")
    }


}

