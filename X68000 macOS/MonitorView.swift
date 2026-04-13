//
//  MonitorView.swift
//  X68000 macOS
//
//  Machine Monitor — arbitrary memory/register read-write for development.
//

import SwiftUI

// MARK: - Tuple → indexed access bridge
// Swift imports C fixed-size arrays (e.g. unsigned int d[8]) as 8-tuples.
// This wrapper provides subscript access.

private struct C8 {
    let t: (UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32)
    subscript(i: Int) -> UInt32 {
        switch i {
        case 0: return t.0; case 1: return t.1; case 2: return t.2; case 3: return t.3
        case 4: return t.4; case 5: return t.5; case 6: return t.6; default: return t.7
        }
    }
}

private extension X68000MonitorCPUState {
    var dArr: C8 { C8(t: d) }
    var aArr: C8 { C8(t: a) }
}

// MARK: - View

struct MonitorView: View {
    @State private var output: String = "X68000 Machine Monitor\nType HELP for commands.\n"
    @State private var input: String = ""
    @State private var isPaused: Bool = false
    @State private var cpuState = X68000MonitorCPUState()
    @State private var cmdHistory: [String] = []
    @State private var historyIndex: Int = -1

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            cpuRegisterPanel
                .padding(8)
                .background(Color(nsColor: .controlBackgroundColor).opacity(0.12))

            Divider()

            ScrollViewReader { proxy in
                ScrollView(.vertical) {
                    Text(output)
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.green)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(6)
                        .id("bottom")
                }
                .onChange(of: output) { _ in
                    withAnimation { proxy.scrollTo("bottom", anchor: .bottom) }
                }
            }
            .background(Color.black)

            Divider()

            HStack(spacing: 6) {
                Text(">")
                    .font(.system(.body, design: .monospaced))
                    .foregroundColor(.green)
                TextField("", text: $input)
                    .font(.system(.body, design: .monospaced))
                    .foregroundColor(.green)
                    .textFieldStyle(.plain)
                    .onSubmit { submitCommand() }
                Button(isPaused ? "Resume (G)" : "Pause (P)") { togglePause() }
                    .controlSize(.small)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(Color.black)
        }
        .background(Color.black)
        .frame(minWidth: 640, minHeight: 480)
        .onAppear {
            isPaused = (X68000_Monitor_IsPaused() != 0)
            refreshCPUState()
        }
    }

    // MARK: - CPU Register Panel

    private var cpuRegisterPanel: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 12) {
                ForEach(0..<4, id: \.self) { i in
                    Text(String(format: "D%d:%08X", i, cpuState.dArr[i]))
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(.cyan)
                }
                Spacer()
                ForEach(0..<4, id: \.self) { i in
                    Text(String(format: "A%d:%08X", i, cpuState.aArr[i]))
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(.yellow)
                }
            }
            HStack(spacing: 12) {
                ForEach(4..<8, id: \.self) { i in
                    Text(String(format: "D%d:%08X", i, cpuState.dArr[i]))
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(.cyan)
                }
                Spacer()
                ForEach(4..<8, id: \.self) { i in
                    Text(String(format: "A%d:%08X", i, cpuState.aArr[i]))
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(.yellow)
                }
                Text(String(format: "PC:%08X", cpuState.pc))
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundColor(.orange)
                Text(String(format: "SR:%04X", cpuState.sr))
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundColor(.orange)
            }
        }
    }

    // MARK: - Command dispatch

    private func submitCommand() {
        let cmd = input.trimmingCharacters(in: .whitespaces)
        guard !cmd.isEmpty else { return }
        cmdHistory.insert(cmd, at: 0)
        historyIndex = -1
        input = ""
        output += "> \(cmd)\n"
        output += executeCommand(cmd)
    }

    private func togglePause() {
        isPaused.toggle()
        X68000_Monitor_SetPaused(isPaused ? 1 : 0)
        output += isPaused ? "-- PAUSED --\n" : "-- RESUMED --\n"
        if isPaused { refreshCPUState() }
    }

    private func refreshCPUState() {
        X68000_Monitor_GetCPUState(&cpuState)
    }

    private func executeCommand(_ raw: String) -> String {
        let parts = raw.uppercased()
            .split(separator: " ", omittingEmptySubsequences: true)
            .map(String.init)
        guard let cmd = parts.first else { return "" }

        switch cmd {
        case "HELP":
            return helpText

        case "P":
            if !isPaused {
                isPaused = true
                X68000_Monitor_SetPaused(1)
                refreshCPUState()
                return "-- PAUSED --\n"
            }
            return "(already paused)\n"

        case "G":
            if isPaused {
                isPaused = false
                X68000_Monitor_SetPaused(0)
                return "-- RESUMED --\n"
            }
            return "(not paused)\n"

        case "RESET":
            X68000_Reset()
            return "Reset.\n"

        case "REG":
            refreshCPUState()
            return formatCPUState()

        case "D":
            guard let addr = parts.count > 1 ? parseAddress(parts[1]) : 0 else {
                return invalidHexMessage("address", parts[1])
            }
            guard let len = parts.count > 2 ? parseHex(parts[2]) : 0x100 else {
                return invalidHexMessage("length", parts[2])
            }
            return hexDump(addr: addr, length: len)

        case "R":
            guard parts.count > 1 else { return "Usage: R addr [B|W|D]\n" }
            guard let addr = parseAddress(parts[1]) else {
                return invalidHexMessage("address", parts[1])
            }
            let size = parts.count > 2 ? parts[2] : "B"
            return readValue(addr: addr, size: size)

        case "M":
            guard parts.count > 2 else { return "Usage: M addr b0 [b1 ...]\n" }
            return writeBytes(addrStr: parts[1], values: Array(parts.dropFirst(2)))

        case "MW":
            guard parts.count == 3 else { return "Usage: MW addr word\n" }
            guard let addr = parseAddress(parts[1]) else {
                return invalidHexMessage("address", parts[1])
            }
            guard let val = parseHex(parts[2]), val <= UInt32(UInt16.max) else {
                return invalidHexMessage("word", parts[2])
            }
            let word = UInt16(val)
            X68000_Monitor_WriteW(addr, word)
            return String(format: "[%06X] <- %04X\n", addr, val)

        case "MD":
            guard parts.count == 3 else { return "Usage: MD addr dword\n" }
            guard let addr = parseAddress(parts[1]) else {
                return invalidHexMessage("address", parts[1])
            }
            guard let val = parseHex(parts[2]) else {
                return invalidHexMessage("dword", parts[2])
            }
            X68000_Monitor_WriteD(addr, val)
            return String(format: "[%06X] <- %08X\n", addr, val)

        case "SET":
            return setRegister(parts: parts)

        default:
            return "Unknown command '\(raw)'. Type HELP.\n"
        }
    }

    // MARK: - Helpers

    private func parseHex(_ s: String) -> UInt32? {
        let clean: String
        if s.hasPrefix("0X") {
            clean = String(s.dropFirst(2))
        } else if s.hasPrefix("$") {
            clean = String(s.dropFirst())
        } else {
            clean = s
        }
        guard !clean.isEmpty else { return nil }
        return UInt32(clean, radix: 16)
    }

    private func parseAddress(_ s: String) -> UInt32? {
        guard let value = parseHex(s), value <= 0x00ff_ffff else { return nil }
        return value
    }

    private func invalidHexMessage(_ label: String, _ value: String) -> String {
        "Invalid \(label) '\(value)'. Numbers must be hex.\n"
    }

    private func hexDump(addr: UInt32, length: UInt32) -> String {
        var result = ""
        let len = min(length, 4096)
        var i: UInt32 = 0
        while i < len {
            let rowAddr = (addr + i) & 0x00ff_ffff
            result += String(format: "%06X: ", rowAddr)
            var ascii = ""
            for j: UInt32 in 0..<16 {
                if i + j < len {
                    let byteAddr = (addr + i + j) & 0x00ff_ffff
                    let b = X68000_Monitor_ReadB(byteAddr)
                    result += String(format: "%02X ", b)
                    ascii += (b >= 0x20 && b < 0x7f) ? String(UnicodeScalar(b)) : "."
                } else {
                    result += "   "
                    ascii += " "
                }
                if j == 7 { result += " " }
            }
            result += " |\(ascii)|\n"
            i += 16
        }
        return result
    }

    private func readValue(addr: UInt32, size: String) -> String {
        switch size {
        case "W":
            return String(format: "[%06X] = %04X\n", addr, X68000_Monitor_ReadW(addr))
        case "D":
            return String(format: "[%06X] = %08X\n", addr, X68000_Monitor_ReadD(addr))
        case "B":
            return String(format: "[%06X] = %02X\n", addr, X68000_Monitor_ReadB(addr))
        default:
            return "Usage: R addr [B|W|D]\n"
        }
    }

    private func writeBytes(addrStr: String, values: [String]) -> String {
        guard var addr = parseAddress(addrStr) else {
            return invalidHexMessage("address", addrStr)
        }
        var result = ""
        for s in values {
            guard let value = parseHex(s), value <= UInt32(UInt8.max) else {
                return invalidHexMessage("byte", s)
            }
            let b = UInt8(value)
            X68000_Monitor_WriteB(addr, b)
            result += String(format: "[%06X] <- %02X\n", addr, b)
            addr = (addr + 1) & 0x00ff_ffff
        }
        return result
    }

    private func setRegister(parts: [String]) -> String {
        guard parts.count == 3 else { return "Usage: SET Dn|An|PC|SR value\n" }
        let reg = parts[1]
        guard let val = parseHex(parts[2]) else {
            return invalidHexMessage("value", parts[2])
        }
        if reg.hasPrefix("D"), let n = Int(reg.dropFirst()), n >= 0, n <= 7 {
            X68000_Monitor_SetDReg(Int32(n), val)
            refreshCPUState()
            return String(format: "D%d <- %08X\n", n, val)
        } else if reg.hasPrefix("A"), let n = Int(reg.dropFirst()), n >= 0, n <= 7 {
            X68000_Monitor_SetAReg(Int32(n), val)
            refreshCPUState()
            return String(format: "A%d <- %08X\n", n, val)
        } else if reg == "PC" {
            guard val <= 0x00ff_ffff else {
                return invalidHexMessage("PC", parts[2])
            }
            X68000_Monitor_SetPC(val)
            refreshCPUState()
            return String(format: "PC <- %08X\n", val)
        } else if reg == "SR" {
            guard val <= UInt32(UInt16.max) else {
                return invalidHexMessage("SR", parts[2])
            }
            X68000_Monitor_SetSR(val)
            refreshCPUState()
            return String(format: "SR <- %04X\n", val)
        }
        return "Unknown register '\(reg)'\n"
    }

    private func formatCPUState() -> String {
        var s = ""
        for i in 0..<4 { s += String(format: "D%d=%08X  ", i, cpuState.dArr[i]) }
        s += "\n"
        for i in 4..<8 { s += String(format: "D%d=%08X  ", i, cpuState.dArr[i]) }
        s += "\n"
        for i in 0..<4 { s += String(format: "A%d=%08X  ", i, cpuState.aArr[i]) }
        s += "\n"
        for i in 4..<8 { s += String(format: "A%d=%08X  ", i, cpuState.aArr[i]) }
        s += "\n"
        s += String(format: "PC=%08X  SR=%04X\n", cpuState.pc, cpuState.sr)
        return s
    }

    private var helpText: String {
        """
        Commands (all numbers are hex):
          D [addr] [len]       Hex dump  (default: addr=0, len=100)
          R addr [B|W|D]       Read byte / word / dword
          M addr b0 [b1 ...]   Write bytes
          MW addr val          Write word
          MD addr val          Write dword
          REG                  Show CPU registers
          SET Dn|An|PC|SR val  Set register (e.g. SET D0 DEADBEEF)
          P                    Pause emulation
          G                    Resume emulation
          RESET                Reset emulator
          HELP                 This help

        Memory map:
          000000-BFFFFF  Main RAM
          C00000-DFFFFF  GVRAM
          E00000-E7FFFF  TVRAM
          E80000+        I/O (CRTC, DMA, MFP, OPM, FDC, SASI, SCC...)
          ED0000-ED3FFF  SRAM
          F00000-FBFFFF  Font ROM
          FC0000-FFFFFF  IPL ROM\n
        """
    }
}
