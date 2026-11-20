import AppKit

let size = NSSize(width: 520, height: 264)
let image = NSImage(size: size)
image.lockFocus()
NSColor(srgbRed: 0.93, green: 0.96, blue: 0.95, alpha: 1).setFill()
NSBezierPath(rect: NSRect(origin: .zero, size: size)).fill()

let instruction = NSAttributedString(
    string: "将 picut 拖到「应用程序」",
    attributes: [
        .font: NSFont.systemFont(ofSize: 20, weight: .semibold),
        .foregroundColor: NSColor(srgbRed: 0.12, green: 0.22, blue: 0.18, alpha: 1),
    ]
)
instruction.draw(at: NSPoint(x: (size.width - instruction.size().width) / 2, y: 207))

let arrow = NSBezierPath()
let middle = size.height - 152
arrow.move(to: NSPoint(x: 233, y: middle))
arrow.line(to: NSPoint(x: 287, y: middle))
arrow.move(to: NSPoint(x: 273, y: middle + 13))
arrow.line(to: NSPoint(x: 287, y: middle))
arrow.line(to: NSPoint(x: 273, y: middle - 13))
arrow.lineWidth = 4
arrow.lineCapStyle = .round
arrow.lineJoinStyle = .round
NSColor(srgbRed: 0.24, green: 0.46, blue: 0.39, alpha: 1).setStroke()
arrow.stroke()
image.unlockFocus()

guard let tiff = image.tiffRepresentation,
      let png = NSBitmapImageRep(data: tiff)?.representation(using: .png, properties: [:]) else {
    fatalError("Could not render the installer background")
}
try png.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
