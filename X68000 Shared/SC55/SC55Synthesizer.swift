import AVFoundation
import Foundation

/// Monotonic deadlines and FIFO byte order, including SysEx across UART backpressure.
struct SC55MIDIEventQueue {
    private struct Event {
        let bytes: [UInt8]
        let deadline: TimeInterval
    }
    private var events: [Event] = []
    private var head = 0
    private var byteOffset = 0
    private(set) var pendingBytes = 0

    mutating func enqueue(_ bytes: [UInt8], deadline: TimeInterval) -> Bool {
        guard deadline.isFinite, bytes.count <= 65536 - pendingBytes else { return false }
        guard !bytes.isEmpty else { return true }
        // A delay reduction must not send a later note-off ahead of its note-on.
        let orderedDeadline = max(deadline, events.last?.deadline ?? deadline)
        events.append(Event(bytes: bytes, deadline: orderedDeadline))
        pendingBytes += bytes.count
        return true
    }

    mutating func flush(dueBy now: TimeInterval, send: ([UInt8]) -> Bool) {
        while head < events.count, events[head].deadline <= now {
            let event = events[head]
            let end = min(byteOffset + 1024, event.bytes.count)
            let chunk = Array(event.bytes[byteOffset..<end])
            guard send(chunk) else { break }
            pendingBytes -= chunk.count
            byteOffset = end
            if byteOffset == event.bytes.count {
                head += 1
                byteOffset = 0
            }
        }
        if head == events.count || head >= 256 {
            events.removeFirst(head)
            head = 0
        }
    }
}

/// One Nuked SC-55 instance; all core and audio scheduling work is serialized.
final class SC55Synthesizer {
    static let shared = SC55Synthesizer()
    private let worker = DispatchQueue(label: "MPX68K.SC55", qos: .userInteractive)
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private let format = AVAudioFormat(standardFormatWithSampleRate: 64000, channels: 2)!
    private var generation = 0
    private var running = false
    private var pendingMIDI = SC55MIDIEventQueue()
    private let framesPerBuffer: AVAudioFrameCount = 512
    private var audioBuffers: [AVAudioPCMBuffer] = []
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
                if self.audioBuffers.isEmpty {
                    var buffers: [AVAudioPCMBuffer] = []
                    for _ in 0..<3 {
                        guard let buffer = AVAudioPCMBuffer(pcmFormat: self.format, frameCapacity: self.framesPerBuffer) else {
                            throw Self.failure("SC-55音声バッファを確保できませんでした。")
                        }
                        buffers.append(buffer)
                    }
                    self.audioBuffers = buffers
                }
                try self.engine.start()
                self.running = true
                for buffer in self.audioBuffers { self.scheduleBuffer(buffer, generation: self.generation) }
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
        pendingMIDI = SC55MIDIEventQueue()
    }

    func reset() {
        worker.async {
            guard self.running else { return }
            self.generation += 1
            self.player.stop()
            self.pendingMIDI = SC55MIDIEventQueue()
            X68SC55_Reset()
            do { try self.warmUpFirmware() }
            catch {
                self.fail(error.localizedDescription)
                return
            }
            for buffer in self.audioBuffers { self.scheduleBuffer(buffer, generation: self.generation) }
            self.player.play()
        }
    }

    func setPaused(_ paused: Bool) {
        worker.async {
            guard self.running else { return }
            if paused { self.player.pause() } else { self.player.play() }
        }
    }

    func send(_ event: [UInt8], delayMs: Double = 0) {
        guard delayMs.isFinite else { return }
        // Capture arrival before dispatch: worker congestion must not shift the deadline.
        let deadline = ProcessInfo.processInfo.systemUptime + max(0, delayMs) / 1000
        worker.async {
            guard self.running else { return }
            guard self.pendingMIDI.enqueue(event, deadline: deadline) else {
                self.fail("SC-55のMIDI入力が処理上限を超えました。音源を再選択してください。")
                return
            }
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

    private func scheduleBuffer(_ buffer: AVAudioPCMBuffer, generation: Int) {
        guard running, self.generation == generation,
              let channels = buffer.floatChannelData else { return }
        // Check deadlines every 8 ms of audio, independently of SpriteKit frames.
        pendingMIDI.flush(dueBy: ProcessInfo.processInfo.systemUptime) { chunk in
            X68SC55_Send(chunk, chunk.count)
        }
        guard X68SC55_Render(channels[0], channels[1], Int(framesPerBuffer)) else {
            fail("SC-55音源の処理が停止しました。ROMセットを確認してください。")
            return
        }
        buffer.frameLength = framesPerBuffer
        player.scheduleBuffer(buffer, completionCallbackType: .dataConsumed) { [weak self] _ in
            guard let self else { return }
            self.worker.async { self.scheduleBuffer(buffer, generation: generation) }
        }
    }
}
