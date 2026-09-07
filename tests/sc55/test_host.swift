import Foundation

@main
struct SC55HostChecks {
    static func checkMIDIDeadlines() {
        var queue = SC55MIDIEventQueue()
        let noteOn: [UInt8] = [0x90, 60, 100]
        let noteOff: [UInt8] = [0x80, 60, 0]
        var sent: [[UInt8]] = []
        precondition(queue.enqueue(noteOn, deadline: 10.300))
        // A later event with a reduced delay must still follow the earlier event.
        precondition(queue.enqueue(noteOff, deadline: 10.100))
        queue.flush(dueBy: 10.299) { sent.append($0); return true }
        precondition(sent.isEmpty)
        queue.flush(dueBy: 10.300) { sent.append($0); return true }
        precondition(sent == [noteOn, noteOff] && queue.pendingBytes == 0)
        precondition(!queue.enqueue(noteOn, deadline: .nan))

        let sysEx = [UInt8](repeating: 0x42, count: 2499) + [0xF7]
        precondition(queue.enqueue(sysEx, deadline: 11))
        precondition(queue.enqueue(noteOn, deadline: 11.1))
        var bytes: [UInt8] = []
        queue.flush(dueBy: 12) { chunk in
            guard bytes.isEmpty else { return false } // Simulate UART becoming full.
            bytes += chunk
            return true
        }
        precondition(bytes.count == 1024 && queue.pendingBytes == sysEx.count + 3 - 1024)
        queue.flush(dueBy: 12) { bytes += $0; return true }
        precondition(bytes == sysEx + noteOn && queue.pendingBytes == 0)
        precondition(queue.enqueue([UInt8](repeating: 0xFE, count: 65536), deadline: 20))
        precondition(!queue.enqueue(noteOn, deadline: 20))
        queue = SC55MIDIEventQueue() // Stop/reset cancels future messages.
        queue.flush(dueBy: 30) { _ in preconditionFailure("Reset leaked delayed MIDI") }
    }

    static func main() throws {
        checkMIDIDeadlines()
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        do {
            _ = try SC55Synthesizer.readROMs(from: folder)
            assertionFailure("Missing ROM files must fail")
        } catch {}
        for (name, sizes) in SC55Synthesizer.requiredROMs {
            var data = Data(repeating: 0, count: sizes.min()!)
            if name == "sc55_rom1.bin" {
                data[2] = 1
                data[0x100] = 0x1a // Synthetic H8 SLEEP, not Roland firmware.
            } else { data[0] = 1 }
            try data.write(to: folder.appendingPathComponent(name))
        }
        let roms = try SC55Synthesizer.readROMs(from: folder)
        let waveURL = folder.appendingPathComponent("sc55_waverom3.bin")
        try Data([1]).write(to: waveURL)
        do {
            _ = try SC55Synthesizer.readROMs(from: folder)
            assertionFailure("Truncated ROM must fail")
        } catch {}
        try Data(repeating: 0, count: 0x100000).write(to: waveURL)
        do {
            _ = try SC55Synthesizer.readROMs(from: folder)
            assertionFailure("Blank ROM must fail")
        } catch {}
        let synth = SC55Synthesizer.shared
        var done = false
        synth.onFailure = { message in fatalError(message) }
        synth.start(roms: roms) { error in
            precondition(error == nil, "\(String(describing: error))")
            synth.send([0x90, 60, 100])
            synth.send([0x80, 60, 0], delayMs: 300)
            synth.setPaused(true)
            synth.setPaused(false)
            synth.reset()
            synth.stop()
            // A new start after pending callbacks must remain independent of the old generation.
            synth.start(roms: roms) { error in
                precondition(error == nil, "\(String(describing: error))")
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
                    synth.stop()
                    done = true
                }
            }
        }
        let deadline = Date().addingTimeInterval(15)
        while !done && Date() < deadline {
            RunLoop.main.run(until: Date().addingTimeInterval(0.01))
        }
        precondition(done, "Audio lifecycle checks timed out")
        print("SC-55 host validation and audio lifecycle checks passed (synthetic silent firmware)")
    }
}
