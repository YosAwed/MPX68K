//
//  MIDIController.swift
//  X68000
//
//  Created by GOROman on 2020/04/06.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Foundation
import CoreMIDI


class MIDIController {
    var clientRef: MIDIClientRef = 0
    var extPointRef: MIDIEndpointRef = 0
    var inPortRef: MIDIPortRef = 0

    init() {
        Connect()
    }
    let clientName: CFString = "MyMidiInst" as CFString
        deinit {
            if inPortRef != 0 {
                MIDIPortDispose( inPortRef )
                inPortRef = 0
            }
     
            if clientRef != 0 {
                MIDIClientDispose( clientRef )
                clientRef = 0
            }
        }
    func Connect() {
        var status: OSStatus = noErr
        status = MIDIClientCreateWithBlock( "MyMidiInst" as CFString, &clientRef, MyMIDINotifyBlock )
              if status != noErr {
                  print( "cannot create MIDI client!" )
                  return
              }
        Get()
    }
    func MyMIDINotifyBlock(midiNotification: UnsafePointer<MIDINotification>) {
        print( "MyMIDINotifyBlock called!\n" )
        // todo: ここは何をしたらいんだろうね？
    }
    
/*
     
     iPad Core MIDIネットワーク通信アプリ開発始末記
     プログラミング iOS Objective-C
     色々あってCore MIDIを使ってネットワーク経由でMIDIを送信するiPadアプリを作ったので、メモを残しておく。

     開発にはXcode 4.0.2を使用した。

     MIDIの送信
     MIDIを送信する方法は、Mac OS XでCore MIDIを使う場合と同じ。

     MIDIClientCreate()でMIDIクライアントを作成する。
     作成したクライアントに対するMIDI出力ポートをMIDIOutputPortCreate()で作成する。
     送信先となるMIDIエンドポイント（destination）を取得する。
     送信したいMIDIデータからMIDIパケットリストを作成する。
     作成したパケットリストをMIDISend()で送信する。
     このあたりはhttp://objective-audio.jp/2008/06/core-midi-midipacketlist.htmlが参考になる。Mac OS X上での内容だが、iPadでも概ね同じ方法で大丈夫なようだ。

     注意点として、MIDIパケットリストを作成する時に再生時刻を設定できるのだが、現時点ではiOS SDKのCore AudioにAudioGetCurrentHostTime()やAudioConvertNanosToHostTime()といった時刻情報をシステムティック単位で扱える便利なAPIが存在しない。

     対策はApple DeveloperのQ&Aに書いてあったが、mach_absolute_time()やmach_timebase_info()あたりを使うことになるようだ。
     
     
     
     var client = MIDIClientRef()
     var port = MIDIPortRef()
     var dest = MIDIEndpointRef()

     MIDIClientCreate("jveditor" as CFString, nil, nil, &client)
     MIDIOutputPortCreate(client, "output" as CFString, &port)

     if false {
            dest = MIDIGetDestination(1)
     } else {
            var device = MIDIGetExternalDevice(0)
            var entity = MIDIDeviceGetEntity(device, 0)
            dest = MIDIEntityGetDestination(entity, 0)
     }

     var name: Unmanaged<CFString>?
     MIDIObjectGetStringProperty(dest, kMIDIPropertyDisplayName, &name)
     print(name?.takeUnretainedValue() as! String)

     var gmOn : [UInt8] = [ 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 ]

     var pktlist = MIDIPacketList()
     var current = MIDIPacketListInit(&pktlist)
     current = MIDIPacketListAdd(&pktlist, MemoryLayout<MIDIPacketList>.stride, current, 0, gmOn.count, &gmOn)

     MIDISend(port, dest, &pktlist)
     */
    
    func Get() {
        let n = MIDIGetNumberOfSources()
        for  i in 0..<n {
             let endPointRef = MIDIGetSource( i )
              
             var str: Unmanaged<CFString>?
             let status = MIDIObjectGetStringProperty( endPointRef, kMIDIPropertyDisplayName, &str )
             if status == noErr {
                
                var text = ""
                text += "MIDI: [\(i)]: "
                text += str!.takeUnretainedValue() as String
                 str!.release()
                print(text)
             }
         }
    }
    
}
