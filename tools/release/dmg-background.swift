// Renders the DMG window background: the app on the left, the Applications
// folder on the right, an arrow and a short instruction between them.
// Usage: swift dmg-background.swift <out.png> <scale>
import AppKit

let args = CommandLine.arguments
guard args.count == 3, let scale = Double(args[2]) else {
    FileHandle.standardError.write("usage: dmg-background.swift <out.png> <scale>\n".data(using: .utf8)!)
    exit(2)
}
let size = NSSize(width: 640, height: 400)
let px = NSSize(width: size.width * scale, height: size.height * scale)

guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(px.width), pixelsHigh: Int(px.height),
                                 bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                 colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { exit(1) }
rep.size = size
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)

// Background: soft vertical gradient.
NSGradient(starting: NSColor(calibratedRed: 0.96, green: 0.97, blue: 0.99, alpha: 1),
           ending: NSColor(calibratedRed: 0.86, green: 0.89, blue: 0.94, alpha: 1))!
    .draw(in: NSRect(origin: .zero, size: size), angle: -90)

// Arrow between the two icon slots (icons sit at x=170 and x=470, y=190 from top).
let arrowY = size.height - 190
let arrow = NSBezierPath()
arrow.move(to: NSPoint(x: 262, y: arrowY))
arrow.line(to: NSPoint(x: 362, y: arrowY))
arrow.lineWidth = 6
arrow.lineCapStyle = .round
NSColor(calibratedRed: 0.32, green: 0.42, blue: 0.62, alpha: 0.9).setStroke()
arrow.stroke()
let head = NSBezierPath()
head.move(to: NSPoint(x: 380, y: arrowY))
head.line(to: NSPoint(x: 356, y: arrowY + 16))
head.line(to: NSPoint(x: 356, y: arrowY - 16))
head.close()
NSColor(calibratedRed: 0.32, green: 0.42, blue: 0.62, alpha: 0.9).setFill()
head.fill()

// Instructions.
func centered(_ text: String, y: CGFloat, font: NSFont, color: NSColor) {
    let style = NSMutableParagraphStyle()
    style.alignment = .center
    let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color, .paragraphStyle: style]
    (text as NSString).draw(in: NSRect(x: 0, y: y, width: size.width, height: font.pointSize * 1.6), withAttributes: attrs)
}
let ink = NSColor(calibratedWhite: 0.22, alpha: 1)
centered("MPX68K を Applications フォルダへドラッグしてください", y: 78,
         font: .systemFont(ofSize: 15, weight: .semibold), color: ink)
centered("Drag MPX68K to the Applications folder to install", y: 54,
         font: .systemFont(ofSize: 12), color: NSColor(calibratedWhite: 0.38, alpha: 1))

NSGraphicsContext.restoreGraphicsState()
guard let png = rep.representation(using: .png, properties: [:]) else { exit(1) }
try png.write(to: URL(fileURLWithPath: args[1]))
