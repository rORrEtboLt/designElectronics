// stack OUT.png A.png B.png ... : stacks images in a 2-column grid with numbered labels
import AppKit
let args = CommandLine.arguments
let imgs: [NSImage] = args.dropFirst(2).compactMap { path in
    guard let im = NSImage(contentsOfFile: path), let rep = im.representations.first else { return nil }
    im.size = NSSize(width: rep.pixelsWide, height: rep.pixelsHigh)  // 1 point = 1 pixel
    return im
}
let cols = Int(ProcessInfo.processInfo.environment["COLS"] ?? "3")!
let cw = imgs.map { $0.size.width }.max() ?? 1, ch = imgs.map { $0.size.height }.max() ?? 1
let rows = (imgs.count + cols - 1) / cols
let size = NSSize(width: cw * CGFloat(cols), height: ch * CGFloat(rows))
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width), pixelsHigh: Int(size.height), bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
NSColor.white.setFill(); NSRect(origin: .zero, size: size).fill()
for (i, im) in imgs.enumerated() {
    let x = CGFloat(i % cols) * cw, y = size.height - CGFloat(i / cols + 1) * ch
    im.draw(in: NSRect(x: x, y: y, width: im.size.width, height: im.size.height))
    NSColor.red.setStroke(); NSBezierPath(rect: NSRect(x: x, y: y, width: cw, height: ch)).stroke()
    ("#\(i + 1)" as NSString).draw(at: NSPoint(x: x + 4, y: y + ch - 22), withAttributes: [.font: NSFont.boldSystemFont(ofSize: 18), .foregroundColor: NSColor.red])
}
NSGraphicsContext.restoreGraphicsState()
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[1]))
