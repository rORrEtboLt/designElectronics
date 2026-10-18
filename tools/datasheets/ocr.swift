// ocr.swift IMAGE.png -> recognized text lines with their positions (x,y in 0..1000, y from top)
import Foundation
import Vision
import AppKit
let url = URL(fileURLWithPath: CommandLine.arguments[1])
guard let img = NSImage(contentsOf: url), let cg = img.cgImage(forProposedRect: nil, context: nil, hints: nil) else { exit(1) }
let req = VNRecognizeTextRequest()
req.recognitionLevel = .accurate
req.usesLanguageCorrection = false
try VNImageRequestHandler(cgImage: cg, options: [:]).perform([req])
var rows: [(Int, Int, String)] = []
for o in req.results ?? [] {
    guard let t = o.topCandidates(1).first else { continue }
    let b = o.boundingBox
    rows.append((Int((1 - b.midY) * 1000), Int(b.minX * 1000), t.string))
}
rows.sort { $0.0 / 8 != $1.0 / 8 ? $0.0 < $1.0 : $0.1 < $1.1 }
for r in rows { print("\(r.0)\t\(r.1)\t\(r.2)") }
