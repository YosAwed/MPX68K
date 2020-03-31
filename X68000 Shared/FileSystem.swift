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
        
        print (documentsPath)
        print (libraryPath)
        print (applicationSupportPath)
        print (cachesPath)
        print (tmpDirectory)
        
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

        print("containerURL:\(containerURL)")
        // コンテナに追加するフォルダのパス
        let documentsURL = containerURL?.appendingPathComponent("Documents")
        let fileURL = documentsURL?.appendingPathComponent("text.txt")
        print(fileURL)
        let todayText = Date().description
        do {
            try todayText.write(to: fileURL!, atomically: true, encoding: .utf8)
        }
        catch {
            print("write error")
        }

        loadBinary( fileURL! )


        guard let fileNames = try? FileManager.default.contentsOfDirectory(atPath: documentsPath) else {
            return
        }
        print(fileNames)


                do {
// iCloudコンテナにフォルダの作成
//                    try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true, attributes: nil)
  //              } catch let error as NSError {
    //                print(error)
                }
        
    }
    
    func loadBinary(_ dataURL : URL )
    {
        do {
            // ファイル読み込み
            let binaryData = try Data(contentsOf: dataURL, options: [])
            // 先頭から1024バイトを抽出。
            let kbData = binaryData.subdata(in: 0..<10)
            // 各バイトを16進数の文字列に変換。
            let stringArray = kbData.map{String(format: "%02X", $0)}
            // ハイフォンで16進数を結合する。
            let binaryString = stringArray.joined(separator: "-")
            print(binaryString)
        } catch {
            print("Failed to read the file.")
        }
    }
        
}
