// Surveille l'etat du bouton gauche vu par macOS (CGEventSource, pas besoin d'accessibilite)
// Usage : tools/button_watch <secondes>
import Foundation
import CoreGraphics
let secs = Double(CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "10") ?? 10
let end = Date().addingTimeInterval(secs)
var last = CGEventSource.buttonState(.combinedSessionState, button: .left)
print("debut etat=\(last)")
var n = 0
while Date() < end {
  let s = CGEventSource.buttonState(.combinedSessionState, button: .left)
  if s != last {
    let t = String(format: "%.3f", Date().timeIntervalSince1970)
    print("\(t) bouton gauche \(s ? "PRESSE" : "RELACHE")")
    last = s
    n += 1
  }
  usleep(500)
}
print("fin transitions=\(n)")
