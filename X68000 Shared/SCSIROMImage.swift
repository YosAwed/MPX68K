import Foundation

/// Checks a SCSIEXROM.DAT dump and returns the 8KB image for $EA0000-$EA1FFF.
///
/// The external SCSI board's ROM (CZ-6BS1) identifies itself with the 6 bytes
/// "SCSIEX" at $EA0044 (Inside X68000 P.466, as corrected by the errata: 6
/// bytes, not 5). Two dump sizes are accepted, as XEiJ does:
/// - 8192 bytes: the whole $EA0000-$EA1FFF window ("SCSIEX" at offset $44)
/// - 8160 bytes: without the first $20 bytes (the SPC registers); "SCSIEX" is
///   at offset $24, and the missing bytes are filled with $FF
/// The internal SCSI ROM (SCSIIN, of the X68000 models with built-in SCSI) has
/// a different layout and cannot stand in for it.
enum SCSIEXROMImage {
    static let size = 0x2000
    static let sizeWithoutSPC = 0x2000 - 0x20
    static let signatureOffset = 0x44
    static let signature = Array("SCSIEX".utf8)
    static let internalSignature = Array("SCSIIN".utf8)

    enum Problem: Error, Equatable {
        case wrongSize(Int)
        case blank
        case internalROM
        case noSignature
    }

    static func normalize(_ data: Data) -> Result<Data, Problem> {
        let bytes = [UInt8](data)
        let image: [UInt8]
        switch bytes.count {
        case size:
            image = bytes
        case sizeWithoutSPC:
            image = [UInt8](repeating: 0xFF, count: size - sizeWithoutSPC) + bytes
        default:
            return .failure(.wrongSize(bytes.count))
        }
        if let first = bytes.first, !bytes.contains(where: { $0 != first }) {
            return .failure(.blank)
        }
        if hasSignature(signature, in: image) {
            return .success(Data(image))
        }
        // An internal ROM carries "SCSIIN" in its own header, at $24 or $44.
        if hasSignature(internalSignature, in: image) ||
            hasSignature(internalSignature, in: image, at: 0x24) {
            return .failure(.internalROM)
        }
        return .failure(.noSignature)
    }

    private static func hasSignature(_ sig: [UInt8], in image: [UInt8], at offset: Int = signatureOffset) -> Bool {
        image.count >= offset + sig.count && Array(image[offset..<offset + sig.count]) == sig
    }
}
