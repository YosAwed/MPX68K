import Foundation

@main
struct SC55HostChecks {
    static func main() throws {
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
