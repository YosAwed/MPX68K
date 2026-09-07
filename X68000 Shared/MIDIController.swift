//
//  MIDIController.swift
//  Xcode11
//
//  Created by GOROman on 2020/04/06.
//  Copyright © 2020 GOROman. All rights reserved.
//

import Foundation
import CoreMIDI
import AppKit

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
        assert(totalBytesInAllEvents <= 256,
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
    private(set) var usesInternalSC55 = false
    private var configurationGeneration = 0
    private let sc55BookmarkKey = "SC55ROMFolderBookmark"
    private let sc55BookmarkScopedKey = "SC55ROMFolderBookmarkIsScoped"
    private let sc55EnabledKey = "SC55InternalEnabled"

    func configureSC55(folder: URL, completion: @escaping (Error?) -> Void) {
        do {
            let accessed = folder.startAccessingSecurityScopedResource()
            defer { if accessed { folder.stopAccessingSecurityScopedResource() } }
            let roms = try SC55Synthesizer.readROMs(from: folder)
            let bookmark: Data
            let scoped: Bool
            do {
                bookmark = try folder.bookmarkData(options: [.withSecurityScope, .securityScopeAllowOnlyReadAccess],
                                                   includingResourceValuesForKeys: nil, relativeTo: nil)
                scoped = true
            } catch {
                // Non-sandboxed development builds may not have the app-scope entitlement.
                // An ordinary bookmark remembers the selection without granting any access.
                bookmark = try folder.bookmarkData(options: [.minimalBookmark],
                                                   includingResourceValuesForKeys: nil, relativeTo: nil)
                scoped = false
            }
            configurationGeneration += 1
            let generation = configurationGeneration
            SC55Synthesizer.shared.start(roms: roms) { [weak self] error in
                guard let self, self.configurationGeneration == generation else { return }
                self.clearPendingMIDI()
                if error == nil && !self.usesInternalSC55 {
                    // Silence the previous destination before changing the route.
                    for channel in UInt8(0)..<16 {
                        self.sendEventImmediate([0xB0 | channel, 120, 0])
                    }
                }
                self.usesInternalSC55 = error == nil
                UserDefaults.standard.set(error == nil, forKey: self.sc55EnabledKey)
                if error == nil {
                    UserDefaults.standard.set(bookmark, forKey: self.sc55BookmarkKey)
                    UserDefaults.standard.set(scoped, forKey: self.sc55BookmarkScopedKey)
                    infoLog("Internal SC-55 enabled", category: .audio)
                }
                completion(error)
            }
        } catch { completion(error) }
    }

    func useExternalMIDI() {
        configurationGeneration += 1
        clearPendingMIDI()
        usesInternalSC55 = false
        SC55Synthesizer.shared.stop()
        UserDefaults.standard.set(false, forKey: sc55EnabledKey)
    }

    func restoreSC55(completion: @escaping (Error?) -> Void) {
        guard let bookmark = UserDefaults.standard.data(forKey: sc55BookmarkKey) else {
            completion(SC55Synthesizer.failure("SC-55 ROMフォルダーを選択してください。"))
            return
        }
        do {
            var stale = false
            let scoped = UserDefaults.standard.object(forKey: sc55BookmarkScopedKey) as? Bool ?? true
            let folder = try URL(resolvingBookmarkData: bookmark, options: scoped ? [.withSecurityScope] : [],
                                 relativeTo: nil, bookmarkDataIsStale: &stale)
            // configureSC55 refreshes the bookmark after validating and loading the files.
            configureSC55(folder: folder, completion: completion)
        } catch { completion(error) }
    }

    private func clearPendingMIDI() {
        pendingEvents.removeAll()
        pendingIndex = 0
        runningStatus = nil
        pendingStatus = nil
        pendingExpected = 0
        pendingData.removeAll()
        inSysEx = false
        sysExBuffer.removeAll()
    }

    func resetInternalSC55() {
        clearPendingMIDI()
        if usesInternalSC55 { SC55Synthesizer.shared.reset() }
    }

    private func reportSC55Error(_ error: Error) {
        errorLog("SC-55: \(error.localizedDescription)", category: .audio)
        let alert = NSAlert()
        alert.messageText = "SC-55 Internal MIDI"
        alert.informativeText = error.localizedDescription + "\nSystem → MIDI Output から設定を確認してください。"
        alert.runModal()
    }
    var clientRef:   MIDIClientRef = 0
    var inPortRef:   MIDIPortRef = 0
    var outPortRef:  MIDIPortRef = 0
    var outPortRef2: MIDIPortRef = 0

    var midiSource : MIDIEndpointRef = 0
    var midiDst0 : MIDIEndpointRef = 0
    var midiDst1 : MIDIEndpointRef = 0
    var midiDst2 : MIDIEndpointRef = 0
    private var midiDests: [MIDIEndpointRef] = []
    private var outputDelayMs: Double = 0.0

    private struct PendingEvent {
        let dueTime: CFTimeInterval
        let data: [UInt8]
    }
    private var pendingEvents: [PendingEvent] = []
    private var pendingIndex: Int = 0

    private var runningStatus: UInt8? = nil
    private var pendingStatus: UInt8? = nil
    private var pendingExpected: Int = 0
    private var pendingData: [UInt8] = []
    private var inSysEx: Bool = false
    private var sysExBuffer: [UInt8] = []
    
    
    init() {
        Connect()
        SC55Synthesizer.shared.onFailure = { [weak self] message in
            self?.useExternalMIDI()
            self?.reportSC55Error(SC55Synthesizer.failure(message))
        }
        if UserDefaults.standard.bool(forKey: sc55EnabledKey) {
            DispatchQueue.main.async { [weak self] in
                self?.restoreSC55 { error in
                    if let error { self?.reportSC55Error(error) }
                }
            }
        }
    }
    let clientName: CFString = "MPX68K" as CFString
    deinit {
        SC55Synthesizer.shared.stop()
        if outPortRef2 != 0 {
            MIDIPortDispose( outPortRef2 )
            outPortRef2 = 0
        }
        if outPortRef != 0 {
            MIDIPortDispose( outPortRef )
            outPortRef = 0
        }
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
            debugLog("MIDI: \(str)", category: .network)
            packet = MIDIPacketNext( &packet ).pointee
        }
    }
    
    func Connect() {
        var status: OSStatus = noErr
        
        status = MIDIClientCreateWithBlock( "MPX68K" as CFString, &clientRef, MIDINotifyBlock )
        if status != noErr {
            errorLog("Cannot create MIDI client!", category: .network)
            return
        }
        #if false
        status = MIDIInputPortCreateWithBlock( clientRef, "MPX68K MIDI In" as CFString, &inPortRef, MyMIDIReadBlock )
        if status != noErr {
            errorLog("Cannot create MIDI In!", category: .network)
            return
        }
        #endif
        status = MIDIOutputPortCreate( clientRef, "MPX68K MIDI Out" as CFString, &outPortRef )
        
        if status != noErr {
            errorLog("Cannot create MIDI Out!", category: .network)
            return
        }
        status = MIDIOutputPortCreate( clientRef, "MPX68K MIDI Out" as CFString, &outPortRef2 )
        
        if status != noErr {
            errorLog("Cannot create MIDI Out 2!", category: .network)
            return
        }

        
        self.GetDest()
        self.GetSource()
    }
    
    func Send(_ buffer: UnsafeMutablePointer<UInt8>?, _ count: Int) {
        sendStream(buffer, count)
    }

    func sendStream(_ buffer: UnsafePointer<UInt8>?, _ count: Int) {
        guard let buffer, count > 0 else { return }
        for i in 0..<count {
            handleIncomingByte(buffer[i])
        }
    }

    private func handleIncomingByte(_ byte: UInt8) {
        if inSysEx {
            if byte >= 0xF8 { // realtime can interleave with SysEx
                sendEvent([byte])
                return
            }
            sysExBuffer.append(byte)
            if byte == 0xF7 {
                sendEvent(sysExBuffer)
                sysExBuffer.removeAll(keepingCapacity: true)
                inSysEx = false
            }
            return
        }

        if byte >= 0x80 {
            if byte == 0xF0 {
                inSysEx = true
                sysExBuffer.removeAll(keepingCapacity: true)
                sysExBuffer.append(byte)
                pendingStatus = nil
                pendingExpected = 0
                pendingData.removeAll(keepingCapacity: true)
                return
            }
            if byte >= 0xF8 {
                sendEvent([byte])
                return
            }
            if byte >= 0xF0 {
                runningStatus = nil
                pendingStatus = byte
                pendingExpected = expectedDataLength(for: byte)
                pendingData.removeAll(keepingCapacity: true)
                if pendingExpected == 0 {
                    sendEvent([byte])
                    pendingStatus = nil
                    pendingExpected = 0
                }
                return
            }
            runningStatus = byte
            pendingStatus = byte
            pendingExpected = expectedDataLength(for: byte)
            pendingData.removeAll(keepingCapacity: true)
            return
        }

        if pendingStatus == nil {
            if let running = runningStatus {
                pendingStatus = running
                pendingExpected = expectedDataLength(for: running)
                pendingData.removeAll(keepingCapacity: true)
            } else {
                return
            }
        }

        pendingData.append(byte)
        if pendingData.count >= pendingExpected {
            if let status = pendingStatus {
                var event = [status]
                event.append(contentsOf: pendingData.prefix(pendingExpected))
                sendEvent(event)
                if status >= 0xF0 {
                    runningStatus = nil
                }
            }
            pendingStatus = nil
            pendingExpected = 0
            pendingData.removeAll(keepingCapacity: true)
        }
    }

    private func expectedDataLength(for status: UInt8) -> Int {
        if status >= 0xF0 {
            switch status {
            case 0xF1: return 1
            case 0xF2: return 2
            case 0xF3: return 1
            case 0xF6: return 0
            default: return 0
            }
        }
        let upper = status & 0xF0
        if upper == 0xC0 || upper == 0xD0 {
            return 1
        }
        return 2
    }

    private func sendEvent(_ event: [UInt8]) {
        guard !event.isEmpty else { return }

        if outputDelayMs > 0.0 {
            let due = CFAbsoluteTimeGetCurrent() + (outputDelayMs / 1000.0)
            pendingEvents.append(PendingEvent(dueTime: due, data: event))
            return
        }

        sendEventImmediate(event)
    }

    private func sendPackets(_ packets: inout MIDIPacketList) {
        if !midiDests.isEmpty {
            for dest in midiDests {
                MIDISend(outPortRef, dest, &packets)
            }
            return
        }
        if midiDst0 != 0 {
            MIDISend(outPortRef, midiDst0, &packets)
        }
        if midiDst1 != 0 {
            MIDISend(outPortRef2, midiDst1, &packets)
        }
    }

    func setOutputDelayMs(_ ms: Double) {
        outputDelayMs = max(0.0, ms)
    }

    func flushDelayedEvents(_ now: CFTimeInterval = CFAbsoluteTimeGetCurrent()) {
        guard pendingIndex < pendingEvents.count else { return }
        while pendingIndex < pendingEvents.count {
            let item = pendingEvents[pendingIndex]
            if item.dueTime > now {
                break
            }
            sendEventImmediate(item.data)
            pendingIndex += 1
        }
        if pendingIndex > 256 {
            pendingEvents.removeFirst(pendingIndex)
            pendingIndex = 0
        }
    }

    private func sendEventImmediate(_ event: [UInt8]) {
        guard !event.isEmpty else { return }
        if usesInternalSC55 {
            SC55Synthesizer.shared.send(event)
            return
        }
        if event.count <= 255 {
            var packets = MIDIPacketList(midiEvents: [event])
            sendPackets(&packets)
            return
        }
        var offset = 0
        while offset < event.count {
            let end = min(offset + 255, event.count)
            let slice = Array(event[offset..<end])
            var packets = MIDIPacketList(midiEvents: [slice])
            sendPackets(&packets)
            offset = end
        }
    }
    
    func MIDINotifyBlock(midiNotification: UnsafePointer<MIDINotification>) {
        debugLog("\(#function) \(midiNotification.pointee.messageID.rawValue)", category: .network)
        switch midiNotification.pointee.messageID {
        case .msgPropertyChanged:
            debugLog("msgPropertyChanged", category: .network)
            GetDest()
            GetSource()
        case .msgSetupChanged:
            debugLog("msgSetupChanged", category: .network)
            GetDest()
            GetSource()
        case .msgObjectAdded:   // 接続を検知
            midiDst1 = 0
            debugLog("msgObjectAdded", category: .network)
            GetDest()
            GetSource()
        case .msgObjectRemoved: // 解除を検知
            debugLog("msgObjectRemoved", category: .network)
        case .msgIOError:       // エラーを検知
            errorLog("msgIOError", category: .network)
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
                debugLog("MIDI device: \(text)", category: .network)
                MIDIPortConnectSource(inPortRef,endPointRef,nil)
                
                
            }
            
        }
        
    }
    func GetDest()
    {
        midiDst0 = 0
        midiDst1 = 0
        midiDst2 = 0
        midiDests.removeAll(keepingCapacity: true)

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
                debugLog("MIDI device: \(text)", category: .network)
                midiDests.append(endPointRef)
                if ( i == 0 ) {
                    debugLog("Set! 0", category: .network)
                    midiDst0 = endPointRef
                    //                    var packets = MIDIPacketList(midiEvents: [[0x90, 0x3f, 0x78]])
                    //                    MIDISend(outPortRef, midiDst0, &packets)
                    
                }
                if ( i > 0 ) {
                    debugLog("Set! 1", category: .network)
                    midiDst1 = endPointRef
                }
            }
        }

        if midiDests.isEmpty {
            warningLog("No MIDI destinations found", category: .network)
        }
        
    }
    
    
}
