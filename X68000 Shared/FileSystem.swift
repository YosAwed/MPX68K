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
        createDocumentsFolder()


        DispatchQueue.main.async {
            do {
                let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
                try FileManager.default.startDownloadingUbiquitousItem(at: containerURL!)
            } catch let error as NSError {
                print(error)
            }
        }
    }
    
    func createDocumentsFolder() {
        // iCloudコンテナのURL
        let url = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let path = (url?.appendingPathComponent("Documents"))!
        do {
            try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true, attributes: nil)
        } catch let error as NSError {
            print(error)
        }
        
#if true
        let fileURL = getDocumentsPath("README.txt")
        let todayText = "POWER TO MAKE YOUR DREAM COME TRUE."
        if ( FileManager.default.fileExists( atPath: fileURL!.path ) == true ) {
        } else {
            do {
                try todayText.write(to: fileURL!, atomically: true, encoding: .utf8)
            }
            catch {
                print("write error")
            }
        }
#endif
    }
    
    func getDocumentsPath(_ filename: String )->URL? {
        let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let documentsURL = containerURL?.appendingPathComponent("Documents")
        let url = documentsURL?.appendingPathComponent(filename)
        return url
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
                        loadDiskImage( filename )
                        
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
    
    
    
    func loadAsynchronously(_ url: URL?) -> Void {
        
        if let url = url {
            
            DispatchQueue.global().async {
                do {
                    if ( url.startAccessingSecurityScopedResource() ) {
                        let imageData: Data? = try Data(contentsOf: url)
                        url.stopAccessingSecurityScopedResource()
                        
                        DispatchQueue.main.async {
                            if let data = imageData {
                                print("size:\(data.count)")
                                
                                let extname = url.pathExtension.removingPercentEncoding
                                if ( extname?.lowercased() == "hds"  ) {
//                                    let p = X68000_GetHDDImageBufferPointer(drive)
//                                    data.copyBytes(to: p!, count: data.count)
//                                    X68000_LoadHDD(drive, url.absoluteString , data.count );
                                } else {
                                    var drive = 0
                                    if ( url.path.contains(" B.") ) {
                                        drive = 1
                                    }
                                    if ( url.path.contains("_B.") ) {
                                        drive = 1
                                    }

                                    let p = X68000_GetDiskImageBufferPointer(drive)
                                    data.copyBytes(to: p!, count: data.count)
                                    X68000_LoadFDD(drive, url.absoluteString , data.count );
                                    if ( drive == 0 ) {
                                        X68000_Reset()
                                    }
                                }
                            }
                        }
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
    }
    func loadDiskImage( _ url : URL )
    {
        saveSRAM()
//        X68000_Reset()
        loadAsynchronously( url )
    }
    func saveSRAM()
    {
        print("==== Save SRAM ====")
        let data = Data(bytes: X68000_GetSRAMPointer(), count: 0x400)
        let url  = getDocumentsPath("SRAM.DAT")
        do {
            try data.write(to: url!)
        }
        catch let error as NSError {
            print(error)
        }
    }

    func loadSRAM()
    {
        print("==== Load SRAM ====")
        let url  = getDocumentsPath("SRAM.DAT")

        do {
            let data: Data? = try Data(contentsOf: url!)
            
            if let data = data {
                let p = X68000_GetSRAMPointer()
                data.copyBytes(to: p!, count: data.count)
            }
        }
        catch let error as NSError {
            print(error)
        }
    }
}
