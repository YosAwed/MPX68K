//
//  FileSystem.swift
//  X68000
//
//  Created by GOROman on 2020/03/31.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Foundation

class FileSystem {
    init() {
        #if false
        let documentsPath = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true)[0]
        //      let documentsPath = NSHomeDirectory() + "/Documents"
        let libraryPath = NSHomeDirectory() + "/Library"
        let applicationSupportPath = NSHomeDirectory() + "/Library/Application Support"
        let cachesPath = NSHomeDirectory() + "/Library/Caches"
        //        let tmpDirectory = NSHomeDirectory() + "/tmp"
        let tmpDirectory = NSTemporaryDirectory()
        #endif
        /*
         print (documentsPath)
         print (libraryPath)
         print (applicationSupportPath)
         print (cachesPath)
         print (tmpDirectory)
         */
        /*
         let musicUrl = NSURL(string: "http://www.hurtrecord.com/se/operation/b1-007_computer_01.mp3")
         if let url = musicUrl {
         let musicData = NSData(contentsOf: url as URL)
         
         let dataName = "hoge.mp3"
         
         print(documentsPath)
         if let data = musicData {
         data.write(toFile: "\(documentsPath)/\(dataName)", atomically: true)
         }
         }
         */
        
        // iCloudコンテナのURL
        let url = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        print(url)
        let path = (url?.appendingPathComponent(""))!
        print("path>>>\(path)")
        do {
            try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true, attributes: nil)
        } catch let error as NSError {
            print(error)
        }
        
        let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        print(containerURL)
        let documentsURL = containerURL?.appendingPathComponent("Documents")
        let fileURL = documentsURL?.appendingPathComponent("README.txt")
        print(fileURL)
        let todayText = "POWER TO MAKE YOUR DREAM COME TRUE."
        do {
            try todayText.write(to: fileURL!, atomically: true, encoding: .utf8)
        }
        catch {
            print("write error")
        }
        
        
        DispatchQueue.main.async {
               do {
                   try FileManager.default.startDownloadingUbiquitousItem(at: containerURL!)
              } catch let error as NSError {
                   print(error)
              }
            }
        
        
        //        loadBinary( dataURL : URL( fileURLWithPath : "\(documentsPath)/hello.txt" ) )
        
        // iCloudコンテナのURL
        // 特定のiCloudコンテナを指定する場合はnilのところに書き込む
        
        
        
        
        
        
    }
    
    func boot()
    {
        let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        
        // コンテナに追加するフォルダのパス
        if let documentsURL = containerURL?.appendingPathComponent("Documents") {
            let dir = getDir( documentsURL )
            
            for n in dir {
                if let filename = n {
                    
                    if filename.absoluteString.contains("Hum") {
                        loadDiskImage( 0, filename )
                        
                    }
                    
                }
            }
            
        }
    }
    func getDir(_ path : URL ) -> [URL?]
    {
        guard let fileNames = try? FileManager.default.contentsOfDirectory(at: path, includingPropertiesForKeys: nil) else {
            return [nil]
        }
        //        for i in 0..<fileNames.count {
        //            print("\(i): \(fileNames[i])")
        //        }
        
        return fileNames
    }
    func loadAsynchronously(_ drive : Int,_ url: URL?) -> Void {
        
        if url == nil {
            return
        }
        
        DispatchQueue.global().async {
            do {
                let imageData: Data? = try Data(contentsOf: url!)
                DispatchQueue.main.async {
                    if let data = imageData {
                        let p = X68000_GetDiskImageBufferPointer(drive)
                        data.copyBytes(to: p!, count: data.count)
                        X68000_LoadFDD(drive, url?.absoluteString ?? "", data.count );
                    }
                    X68000_Reset()
                }
            }
            catch let error as NSError {
                print(error)
                
                DispatchQueue.main.async {
                    //                    self.image = defaultUIImage
                }
            }
        }
    }
    func loadDiskImage(_ drive : Int, _ url : URL )
    {
        X68000_Reset()
        loadAsynchronously( drive, url )
    }
    
}
