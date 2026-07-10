import AppKit
import CoreGraphics
import Foundation

private let cyan = NSColor(calibratedRed: 0.25, green: 0.86, blue: 0.94, alpha: 1)
private let amber = NSColor(calibratedRed: 1.0, green: 0.66, blue: 0.20, alpha: 1)
private let navy = NSColor(calibratedRed: 0.035, green: 0.065, blue: 0.12, alpha: 1)

private func savePNG(_ image: NSImage, to url: URL) throws {
    guard let tiff = image.tiffRepresentation,
          let bitmap = NSBitmapImageRep(data: tiff),
          let data = bitmap.representation(using: .png, properties: [:]) else {
        throw NSError(domain: "GenerateMPX68KArtwork", code: 1)
    }
    try data.write(to: url)
}

private func drawText(_ text: String, at point: CGPoint, size: CGFloat, color: NSColor, in context: CGContext) {
    let font = NSFont(name: "Menlo-Bold", size: size) ?? NSFont.monospacedSystemFont(ofSize: size, weight: .bold)
    let attributes: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color]
    let string = NSAttributedString(string: text, attributes: attributes)
    let line = CTLineCreateWithAttributedString(string)
    context.saveGState()
    context.textPosition = point
    CTLineDraw(line, context)
    context.restoreGState()
}

private func makeIcon(size: Int) -> NSImage {
    let image = NSImage(size: NSSize(width: size, height: size))
    image.lockFocus()
    guard let context = NSGraphicsContext.current?.cgContext else { return image }
    let s = CGFloat(size)
    context.setAllowsAntialiasing(true)

    let bounds = CGRect(x: 0, y: 0, width: s, height: s)
    let rounded = CGPath(roundedRect: bounds.insetBy(dx: s * 0.025, dy: s * 0.025), cornerWidth: s * 0.19, cornerHeight: s * 0.19, transform: nil)
    context.addPath(rounded)
    context.clip()
    let colors = [navy.cgColor, NSColor(calibratedRed: 0.04, green: 0.16, blue: 0.28, alpha: 1).cgColor] as CFArray
    context.drawLinearGradient(CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: [0, 1])!, start: CGPoint(x: 0, y: 0), end: CGPoint(x: s, y: s), options: [])

    // Circuit traces and connection points.
    context.setStrokeColor(cyan.withAlphaComponent(0.38).cgColor)
    context.setLineWidth(max(2, s * 0.012))
    for i in 0..<4 {
        let offset = s * (0.16 + CGFloat(i) * 0.055)
        context.move(to: CGPoint(x: 0, y: offset))
        context.addLine(to: CGPoint(x: s * 0.12, y: offset))
        context.addLine(to: CGPoint(x: s * 0.19, y: offset + s * 0.07))
        context.move(to: CGPoint(x: s, y: s - offset))
        context.addLine(to: CGPoint(x: s * 0.88, y: s - offset))
        context.addLine(to: CGPoint(x: s * 0.81, y: s - offset - s * 0.07))
    }
    context.strokePath()
    context.setFillColor(amber.cgColor)
    for i in 0..<4 {
        let y = s * (0.16 + CGFloat(i) * 0.055)
        context.fillEllipse(in: CGRect(x: s * 0.025, y: y - s * 0.014, width: s * 0.028, height: s * 0.028))
        context.fillEllipse(in: CGRect(x: s * 0.947, y: s - y - s * 0.014, width: s * 0.028, height: s * 0.028))
    }

    // Original monogram: deliberately not based on the X68000 wordmark.
    drawText("MPX", at: CGPoint(x: s * 0.17, y: s * 0.48), size: s * 0.24, color: .white, in: context)
    drawText("68K", at: CGPoint(x: s * 0.24, y: s * 0.25), size: s * 0.16, color: amber, in: context)
    context.setStrokeColor(cyan.cgColor)
    context.setLineWidth(max(3, s * 0.018))
    context.move(to: CGPoint(x: s * 0.22, y: s * 0.20))
    context.addLine(to: CGPoint(x: s * 0.78, y: s * 0.20))
    context.strokePath()

    image.unlockFocus()
    return image
}

let arguments = CommandLine.arguments
guard arguments.count == 2 else {
    fputs("usage: GenerateMPX68KArtwork <icon-directory>\n", stderr)
    exit(2)
}

let iconDirectory = URL(fileURLWithPath: arguments[1], isDirectory: true)
let iconSizes: [(String, Int)] = [
    ("16.png", 16), ("32.png", 32), ("32-1.png", 32), ("64.png", 64),
    ("128.png", 128), ("256.png", 256), ("256-1.png", 256), ("512.png", 512),
    ("512-1.png", 512), ("X68000Icon1024-1.png", 1024)
]

do {
    for (name, size) in iconSizes {
        try savePNG(makeIcon(size: size), to: iconDirectory.appendingPathComponent(name))
    }
} catch {
    fputs("generation failed: \(error)\n", stderr)
    exit(1)
}
