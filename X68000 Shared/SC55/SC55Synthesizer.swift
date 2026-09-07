import AVFoundation
import Foundation

/// One Nuked SC-55 instance; all core and audio scheduling work is serialized.
final class SC55Synthesizer {
    static let shared = SC55Synthesizer()
    private let worker = DispatchQueue(label: "MPX68K.SC55", qos: .userInitiated)
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private let format = AVAudioFormat(standardFormatWithSampleRate: 64000, channels: 2)!
    private var generation = 0
    private var running = false
    private var pendingMIDI: [[UInt8]] = []
    private var pendingBytes = 0
    var onFailure: ((String) -> Void)? // Installed and called on the main queue.

    // Host output gain, independent of MIDI channel volume and firmware resets.
    // Read/write from the main thread; audio changes are serialized with playback.
    var volume: Double {
        get {
            let saved = UserDefaults.standard.object(forKey: "SC55OutputVolume") as? NSNumber
            let value = saved?.doubleValue ?? 1.0
            return value.isFinite ? min(1.0, max(0.0, value)) : 1.0
        }
        set {
            guard newValue.isFinite else { return }
            let value = min(1.0, max(0.0, newValue))
            UserDefaults.standard.set(value, forKey: "SC55OutputVolume")
            worker.async { self.engine.mainMixerNode.outputVolume = Float(value) }
        }
    }

    private init() {
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: format)
        engine.mainMixerNode.outputVolume = Float(volume)
    }

    static let requiredROMs: [(String, Set<Int>)] = [
        ("sc55_rom1.bin", [0x8000]),
        ("sc55_rom2.bin", [0x40000, 0x80000]),
        ("sc55_waverom1.bin", [0x100000]),
        ("sc55_waverom2.bin", [0x100000]),
        ("sc55_waverom3.bin", [0x100000])
    ]

    static func readROMs(from folder: URL) throws -> [Data] {
        let accessed = folder.startAccessingSecurityScopedResource()
        defer { if accessed { folder.stopAccessingSecurityScopedResource() } }
        return try requiredROMs.map { name, sizes in
            let url = folder.appendingPathComponent(name)
            let values = try url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
            guard values.isRegularFile == true, let size = values.fileSize, sizes.contains(size) else {
                throw failure("\(name): ROMサイズが正しくありません（必要: \(sizes.sorted().map(String.init).joined(separator: " / ")) bytes）。")
            }
            let handle = try FileHandle(forReadingFrom: url)
            defer { try? handle.close() }
            // Bound the read even if the file changes after the metadata check.
            let data = try handle.read(upToCount: sizes.max()! + 1) ?? Data()
            guard sizes.contains(data.count), data.contains(where: { $0 != 0 && $0 != 255 }) else {
                throw failure("\(name): 空または無効なROMです。")
            }
            return data
        }
    }

    static func failure(_ message: String) -> NSError {
        NSError(domain: "MPX68K.SC55", code: 1, userInfo: [NSLocalizedDescriptionKey: message])
    }

    func start(roms: [Data], completion: @escaping (Error?) -> Void) {
        worker.async {
            self.stopOnWorker()
            let loaded = roms[0].withUnsafeBytes { p1 in
                roms[1].withUnsafeBytes { p2 in
                    roms[2].withUnsafeBytes { w1 in
                        roms[3].withUnsafeBytes { w2 in
                            roms[4].withUnsafeBytes { w3 in
                                X68SC55_Load(p1.bindMemory(to: UInt8.self).baseAddress, p1.count,
                                            p2.bindMemory(to: UInt8.self).baseAddress, p2.count,
                                            w1.bindMemory(to: UInt8.self).baseAddress,
                                            w2.bindMemory(to: UInt8.self).baseAddress,
                                            w3.bindMemory(to: UInt8.self).baseAddress, w1.count)
                            }
                        }
                    }
                }
            }
            do {
                guard loaded else { throw Self.failure("SC-55 ROMの読み込みに失敗しました。") }
                try self.warmUpFirmware()
                try self.engine.start()
                self.running = true
                for _ in 0..<3 { self.scheduleBuffer(generation: self.generation) }
                guard self.running else { throw Self.failure("SC-55の初期化に失敗しました。") }
                self.player.play()
                DispatchQueue.main.async { completion(nil) }
            } catch {
                self.stopOnWorker()
                DispatchQueue.main.async { completion(error) }
            }
        }
    }

    func stop() { worker.async { self.stopOnWorker() } }

    private func stopOnWorker() {
        generation += 1
        running = false
        player.stop()
        engine.stop()
        pendingMIDI.removeAll()
        pendingBytes = 0
    }

    func reset() {
        worker.async {
            guard self.running else { return }
            self.generation += 1
            self.player.stop()
            self.pendingMIDI.removeAll()
            self.pendingBytes = 0
            X68SC55_Reset()
            do { try self.warmUpFirmware() }
            catch {
                self.fail(error.localizedDescription)
                return
            }
            for _ in 0..<3 { self.scheduleBuffer(generation: self.generation) }
            self.player.play()
        }
    }

    func setPaused(_ paused: Bool) {
        worker.async {
            guard self.running else { return }
            if paused { self.player.pause() } else { self.player.play() }
        }
    }

    func send(_ event: [UInt8]) {
        worker.async {
            guard self.running else { return }
            guard self.pendingBytes + event.count <= 65536 else {
                self.fail("SC-55のMIDI入力が処理上限を超えました。音源を再選択してください。")
                return
            }
            self.pendingMIDI.append(event)
            self.pendingBytes += event.count
        }
    }

    private func fail(_ message: String) {
        stopOnWorker()
        DispatchQueue.main.async { self.onFailure?(message) }
    }

    private func warmUpFirmware() throws {
        // SC-55 v1.21 ignores MIDI during its cold-start initialization. Advance four
        // emulated seconds silently before reporting ready (also after a system reset).
        var left = [Float](repeating: 0, count: 1024)
        var right = left
        for _ in 0..<250 {
            guard X68SC55_Render(&left, &right, 1024) else {
                throw Self.failure("SC-55の起動に失敗しました。ROMセットを確認してください。")
            }
        }
    }

    private func scheduleBuffer(generation: Int) {
        guard running, self.generation == generation,
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 1024),
              let channels = buffer.floatChannelData else { return }
        // Feed bounded chunks; preserve byte order across long SysEx messages.
        while let event = pendingMIDI.first {
            let chunk = Array(event.prefix(1024))
            guard X68SC55_Send(chunk, chunk.count) else { break }
            pendingBytes -= chunk.count
            if chunk.count == event.count { pendingMIDI.removeFirst() }
            else { pendingMIDI[0].removeFirst(chunk.count) }
        }
        guard X68SC55_Render(channels[0], channels[1], 1024) else {
            fail("SC-55音源の処理が停止しました。ROMセットを確認してください。")
            return
        }
        buffer.frameLength = 1024
        player.scheduleBuffer(buffer, completionCallbackType: .dataConsumed) { [weak self] _ in
            guard let self else { return }
            self.worker.async { self.scheduleBuffer(generation: generation) }
        }
    }
}
