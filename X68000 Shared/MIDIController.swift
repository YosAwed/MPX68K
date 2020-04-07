//
//  MIDIController.swift
//  Xcode11
//
//  Created by GOROman on 2020/04/06.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Foundation
import CoreMIDI

/// A UInt8 array, usually 3 bytes long
public typealias MidiEvent = [UInt8]

extension MIDIPacketList {
    init(midiEvents: [MidiEvent]) {
        
        let timestamp = MIDITimeStamp(0)
        let totalBytesInAllEvents = midiEvents.reduce(0) { total, event in
            return total + event.count
        }
        
        // Without this, we'd run out of space for the last few MidiEvents
        let listSize = MemoryLayout<MIDIPacketList>.size + totalBytesInAllEvents
        
        // CoreMIDI supports up to 65536 bytes, but in practical tests it seems
        // certain devices accept much less than that at a time. Unless you're
        // turning on / off ALL notes at once, 256 bytes should be plenty.
        assert(totalBytesInAllEvents < 256,
               "The packet list was too long! Split your data into multiple lists.")
        
        // Allocate space for a certain number of bytes
        let byteBuffer = UnsafeMutablePointer<UInt8>.allocate(capacity: listSize)
        
        // Use that space for our MIDIPacketList
        self = byteBuffer.withMemoryRebound(to: MIDIPacketList.self, capacity: 1) { packetList -> MIDIPacketList in
            var packet = MIDIPacketListInit(packetList)
            midiEvents.forEach { event in
                packet = MIDIPacketListAdd(packetList, listSize, packet, timestamp, event.count, event)
            }
            
            return packetList.pointee
        }
        
        byteBuffer.deallocate() // release the manually managed memory
    }
}


class MIDIController {
    var clientRef: MIDIClientRef = 0
    var extPointRef: MIDIEndpointRef = 0
    var inPortRef: MIDIPortRef = 0
    var outPortRef: MIDIPortRef = 0
    
    var midiSource : MIDIEndpointRef = 0
    var midiDst0 : MIDIEndpointRef = 0
    var midiDst1 : MIDIEndpointRef = 0
    var midiDst2 : MIDIEndpointRef = 0
    
    
    init() {
        Connect()
    }
    let clientName: CFString = "X68000" as CFString
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
    func MyMIDIReadBlock(packetList: UnsafePointer<MIDIPacketList>,
                         srcConnRefCon: UnsafeMutableRawPointer?) -> Void {
        let packets: MIDIPacketList = packetList.pointee
        var packet: MIDIPacket = packets.packet
        for _ in 0 ..< packets.numPackets {
            if packet.data.0 == 0xF8 || packet.data.0 == 0xFE {
                continue
            }
            var str = ""
            switch packet.length {
            case 1: str = String(format: "%02X", packet.data.0)
            case 2: str = String(format: "%02X %02X", packet.data.0, packet.data.1)
            case 3: str = String(format: "%02X %02X %02X", packet.data.0, packet.data.1, packet.data.2)
            default: str = "length: \(packet.length)"
            }
            print(str)
            packet = MIDIPacketNext( &packet ).pointee
        }
    }
    
    func Connect() {
        var status: OSStatus = noErr
        
        status = MIDIClientCreateWithBlock( "X68000" as CFString, &clientRef, MIDINotifyBlock )
        if status != noErr {
            print( "cannot create MIDI client!" )
            return
        }
        
        status = MIDIInputPortCreateWithBlock( clientRef, "X68000 MIDI In" as CFString, &inPortRef, MyMIDIReadBlock )
        if status != noErr {
            print( "cannot create MIDI In!" )
            return
        }
        status = MIDIOutputPortCreate( clientRef, "X68000 MIDI Out" as CFString, &outPortRef )
        
        if status != noErr {
            print( "cannot create MIDI Out!" )
            return
        }
        
        
        self.GetDest()
        self.GetSource()
    }
    
    func Send(_ buffer : UnsafeMutablePointer<UInt8>?, _ count : Int  )
    {
        var i = 0
        while ( i < count ) {
            let cmd : UInt8 = buffer![i] & 0xf0;
            
            switch(cmd) {
            case 0xC0...0xD0:   // 2bytes
                var packets = MIDIPacketList(midiEvents: [[buffer![i],buffer![i+1]]])
                MIDISend(outPortRef, midiDst0, &packets)
                MIDISend(outPortRef, midiDst1, &packets)
                i += 1
            case 0x80...0xB0:   // 3bytes
                //                if ( cmd == 0x90 ) && ( buffer![i+2] == 0x00) {
                //                    // patch!
                //                    var packets = MIDIPacketList(midiEvents: [[0x80,buffer![i+1],buffer![i+2]]])
                //                    MIDISend(outPortRef, midiDst0, &packets)
                //                } else {
                
                var packets = MIDIPacketList(midiEvents: [[buffer![i],buffer![i+1],buffer![i+2]]])
                MIDISend(outPortRef, midiDst0, &packets)
                MIDISend(outPortRef, midiDst1, &packets)
                //                }
                i += 2
            case 0xE0:
                var packets = MIDIPacketList(midiEvents: [[buffer![i],buffer![i+1],buffer![i+2]]])
                MIDISend(outPortRef, midiDst0, &packets)
                MIDISend(outPortRef, midiDst1, &packets)
                i += 2
            default:
                var packets = MIDIPacketList(midiEvents: [[buffer![i]]])
                MIDISend(outPortRef, midiDst0, &packets)
                MIDISend(outPortRef, midiDst1, &packets)
            }
            //            MIDISend(outPortRef, midiDst1, &packets)
            //            MIDISend(outPortRef, midiDst2, &packets)
            i += 1
        }
        //var packets = MIDIPacketList(midiEvents: [[0x90, 0x3f, 0x78]])
        
        //    MIDIReceived(midiDst, &packets)
    }
    
    func MIDINotifyBlock(midiNotification: UnsafePointer<MIDINotification>) {
        print( "\(#function) \(midiNotification.pointee.messageID.rawValue)")
        switch midiNotification.pointee.messageID {
        case .msgPropertyChanged:
            print("msgPropertyChanged")
            break;
        case .msgSetupChanged:
            print("msgSetupChanged")
        case .msgObjectAdded:   // 接続を検知
            midiDst1 = 0
            print("msgObjectAdded")
            GetDest()
            GetSource()
        case .msgObjectRemoved: // 解除を検知
            print("msgObjectRemoved")
        case .msgIOError:       // エラーを検知
            print("msgIOError")
        default:
            break
        }
        
    }
    
    func GetSource() {
        let n = MIDIGetNumberOfSources()
        for  i in 0..<n {
            // エンドポイントを取得
            let endPointRef = MIDIGetSource( i )
            
            var str: Unmanaged<CFString>?
            let status = MIDIObjectGetStringProperty( endPointRef, kMIDIPropertyDisplayName, &str )
            if status == noErr {
                
                var text = ""
                text += "MIDI:SRC [\(i)]: "
                text += str!.takeUnretainedValue() as String
                str!.release()
                print(text)
                MIDIPortConnectSource(inPortRef,endPointRef,nil)
                
                
            }
            
        }
        
    }
    func GetDest()
    {
        let n = MIDIGetNumberOfDestinations()
        for  i in 0..<n {
            // エンドポイントを取得
            let endPointRef = MIDIGetDestination( i )
            
            var str: Unmanaged<CFString>?
            let status = MIDIObjectGetStringProperty( endPointRef, kMIDIPropertyDisplayName, &str )
            if status == noErr {
                
                var text = "MIDI:DST [\(i)]: "
                text += str!.takeUnretainedValue() as String
                str!.release()
                print(text)
                if ( i == 0 ) {
                    //                    print("Set! 0")
                    midiDst0 = endPointRef
                    //                    var packets = MIDIPacketList(midiEvents: [[0x90, 0x3f, 0x78]])
                    //                    MIDISend(outPortRef, midiDst0, &packets)
                    
                }
                if ( i == 1 ) && midiDst1 == 0 {
                    print("Set! 1 ")
                    midiDst1 = endPointRef
                    var packets = MIDIPacketList(midiEvents: [[0x98, 0x6f, 0x78]])
                    MIDISend(outPortRef, endPointRef, &packets)
                }
                if ( i == 2 ) && midiDst2 == 0  {
                    print("Set2! 2")
                    midiDst2 = endPointRef
                }
            }
        }
        
    }
    
    
}
