import Foundation

// Host checks for SCSIEXROMImage (no ROM files needed).

func image(_ count: Int, signature: String?, at offset: Int) -> Data {
    var bytes = [UInt8]((0..<count).map { UInt8(truncatingIfNeeded: $0 &* 7 &+ 3) })
    if let signature {
        for (i, b) in signature.utf8.enumerated() { bytes[offset + i] = b }
    }
    return Data(bytes)
}

func expectFailure(_ data: Data, _ problem: SCSIEXROMImage.Problem, _ name: String) {
    guard case .failure(let got) = SCSIEXROMImage.normalize(data), got == problem else {
        fatalError("FAIL: \(name)")
    }
    print("PASS: \(name)")
}

@main
struct SCSIROMChecks {
    static func main() {
        // 8192-byte dump: "SCSIEX" at $44, used as is.
        let full = image(0x2000, signature: "SCSIEX", at: 0x44)
        guard case .success(let out) = SCSIEXROMImage.normalize(full), out == full else {
            fatalError("FAIL: 8192-byte SCSIEX dump is accepted unchanged")
        }
        print("PASS: 8192-byte SCSIEX dump is accepted unchanged")

        // 8160-byte dump without the SPC registers: "SCSIEX" at $24 in the
        // file, padded with $FF back to $EA0000.
        let short = image(0x2000 - 0x20, signature: "SCSIEX", at: 0x24)
        guard case .success(let padded) = SCSIEXROMImage.normalize(short),
              padded.count == 0x2000,
              padded.prefix(0x20).allSatisfy({ $0 == 0xFF }),
              padded.suffix(from: 0x20) == short,
              Array(padded[0x44..<0x4A]) == Array("SCSIEX".utf8) else {
            fatalError("FAIL: 8160-byte dump is padded to 8192 with $FF")
        }
        print("PASS: 8160-byte dump is padded to 8192 with $FF")

        expectFailure(image(0x2000, signature: "SCSIIN", at: 0x24), .internalROM,
                      "internal SCSIIN ROM (8192 bytes) is rejected as internal")
        expectFailure(image(0x2000 - 0x20, signature: "SCSIIN", at: 0x24), .internalROM,
                      "internal SCSIIN ROM (8160 bytes) is rejected as internal")
        // The old check accepted any "SCSI" in the first 256 bytes.
        expectFailure(image(0x2000, signature: "SCSI--", at: 0x44), .noSignature,
                      "\"SCSI\" alone is not the SCSIEX signature")
        expectFailure(image(0x2000, signature: "SCSIEX", at: 0x40), .noSignature,
                      "SCSIEX at the wrong offset is rejected")
        expectFailure(image(0x1000, signature: nil, at: 0), .wrongSize(0x1000),
                      "wrong size is rejected")
        expectFailure(Data(repeating: 0xFF, count: 0x2000), .blank,
                      "blank dump is rejected")

        // A real dump next to the repo, if the developer has one (not in CI).
        if let path = ProcessInfo.processInfo.environment["SCSIEXROM"],
           let data = FileManager.default.contents(atPath: path) {
            guard case .success = SCSIEXROMImage.normalize(data) else {
                fatalError("FAIL: real SCSIEXROM.DAT at \(path) is accepted")
            }
            print("PASS: real SCSIEXROM.DAT is accepted")
        }
        print("all tests passed")
    }
}
