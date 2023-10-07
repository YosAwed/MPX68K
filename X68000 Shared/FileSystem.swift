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
        
        createDocumentsFolder()
        
        
        DispatchQueue.main.async {
            do {
                // for iCloud
                //                let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
                let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
                
                try FileManager.default.startDownloadingUbiquitousItem(at: containerURL!)
            } catch let error as NSError {
                print(error)
            }
        }
    }
    
    func createDocumentsFolder() {
        // iCloudコンテナのURL
        //        let url = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let url = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        let path = (url?.appendingPathComponent("Documents"))!
        do {
            try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true, attributes: nil)
        } catch let error as NSError {
            print(error)
        }
        
#if true
        let fileURL = getDocumentsPath("README.txt")
        let todayText = "POWER TO MAKE YOUR DREAM COME TRUE. Plase put CGROM.DAT and IPLROM.DAT here."
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
        // for iCloud
        //      let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        
        let documentsURL = containerURL?.appendingPathComponent("Documents")
        let url = documentsURL?.appendingPathComponent(filename)
        return url
    }
    
    func boot()
    {
        // for iCloud
        //      let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)
        let containerURL = try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        
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
                                if ( extname?.lowercased() == "hdf"  ) {
                                    let p = X68000_GetDiskImageBufferPointer(4, data.count)
                                    data.copyBytes(to: p!, count: data.count)
                                    X68000_LoadHDD( url.absoluteString );
                                    X68000_Reset()
                                    
                                } else {
                                    var drive = 0
                                    if ( url.path.contains(" B.") ) {
                                        drive = 1
                                    } else if ( url.path.contains("_B.") ) {
                                        drive = 1
                                    }
                                    
                                    let p = X68000_GetDiskImageBufferPointer(drive, data.count)
                                    data.copyBytes(to: p!, count: data.count)
                                    X68000_LoadFDD(drive, url.absoluteString );
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
// Added by Awed 2023/10/7
    func loadCGROM()
    {
        print("==== Load CGROM ====")
        let url  = getDocumentsPath("CGROM.DAT")
        
        do {
            let data: Data? = try Data(contentsOf: url!)
            
            if let data = data {
                let p = X68000_GetCGROMPointer()
                data.copyBytes(to: p!, count: data.count)
                //print(data.count)
            }
        }
        catch let error as NSError {
            print(error)
        }
    }
    func loadIPLROM()
    {
        print("==== Load IPLROM ====")
        let url  = getDocumentsPath("IPLROM.DAT")
        
        do {
            let data: Data? = try Data(contentsOf: url!)
            
            if let data = data {
                let p = X68000_GetIPLROMPointer()
                data.copyBytes(to: p!, count: data.count)
            }
        }
        catch let error as NSError {
            print(error)
        }
    }
// end
    
    
}
