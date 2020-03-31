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
        
        let documentsPath = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true)[0]
  //      let documentsPath = NSHomeDirectory() + "/Documents"
        let libraryPath = NSHomeDirectory() + "/Library"
        let applicationSupportPath = NSHomeDirectory() + "/Library/Application Support"
        let cachesPath = NSHomeDirectory() + "/Library/Caches"
//        let tmpDirectory = NSHomeDirectory() + "/tmp"
        let tmpDirectory = NSTemporaryDirectory()

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
        /*
        let containerURL = FileManager.default.url()
        let documentsURL = containerURL?.appendingPathComponent("Documents")
        let fileURL = documentsURL?.appendingPathComponent("my.diary")
         
        /* write */
        let todayText = Date().description
        do {
            try todayText.write(to: fileURL!, atomically: true, encoding: .utf8)
        }
        catch {
            print("write error")
        }
         */
        
        
//        loadBinary( dataURL : URL( fileURLWithPath : "\(documentsPath)/hello.txt" ) )

                // iCloudコンテナのURL
        // 特定のiCloudコンテナを指定する場合はnilのところに書き込む

        let containerURL = FileManager.default.url(forUbiquityContainerIdentifier: nil)

        // コンテナに追加するフォルダのパス
        if let documentsURL = containerURL?.appendingPathComponent("Documents") {
            let dir = getDir( documentsURL )

            for n in dir {
                if let filename = n {
                    if filename.absoluteString.contains("SX-") {
                        loadBinary( filename )

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
        for i in 0..<fileNames.count {
            
            print("\(i): \(fileNames[i])")
        }

        return fileNames
    }
    
    func loadBinary(_ dataURL : URL )
    {
        print("loadBinary:\(dataURL)")
        
        do {
            // ファイル読み込み
            let data = try Data(contentsOf: dataURL, options: [])
            print("loaded size:\(data.count)")
            var d = [UInt8](repeating: 0, count: data.count)

            for i in 0..<data.count {
                d[i] = data[i]
            }

            X68000_LoadFDD(0, dataURL.absoluteString ?? "", &d, data.count );
            // 先頭から1024バイトを抽出。
//            let kbData = binaryData.subdata(in: 0..<256)
            // 各バイトを16進数の文字列に変換。
  //          let stringArray = kbData.map{String(format: "%02X", $0)}
            // ハイフォンで16進数を結合する。
    //        let binaryString = stringArray.joined(separator: " ")
      //      print(binaryString)
        } catch {
            print("Failed to read the file.")
        }
    }
        
}
