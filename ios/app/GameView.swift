import ImageIO
import QuartzCore
import UIKit
import UniformTypeIdentifiers

/// The game's view: a Metal layer ANGLE draws into (OpenGL ES 3 on Metal, ios/launcher/egl_view.mm)
/// at the screen's native resolution, the source of touches and hardware keys, and the soft
/// keyboard's text input (to the engine's text box).
final class GameView: UIView, UIKeyInput {
    override class var layerClass: AnyClass { CAMetalLayer.self }

    private var eglReady = false, eglFailed = false
    private(set) var pixelWidth: Int32 = 0, pixelHeight: Int32 = 0
    /// The drawable changed size: (width, height) in pixels.
    var onResize: ((Int32, Int32) -> Void)?
    /// ANGLE cannot draw into the view (called once; it is not tried again).
    var onGraphicsFailure: (() -> Void)?

    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.isOpaque = true
        isMultipleTouchEnabled = true
    }

    required init?(coder: NSCoder) { fatalError("not from a nib") }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if let screen = window?.windowScene?.screen {
            contentScaleFactor = screen.nativeScale
            layer.contentsScale = screen.nativeScale
        }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        var w: Int32 = 0, h: Int32 = 0
        mcfm_ios_pixel_size(Double(bounds.width), Double(bounds.height), Double(contentScaleFactor), &w, &h)
        guard w > 0 && h > 0 && !eglFailed else { return }
        // bounds x scale, as ANGLE keeps it (ios/launcher/layout.h): never another shape.
        (layer as! CAMetalLayer).drawableSize = CGSize(width: CGFloat(w), height: CGFloat(h))
        if !eglReady {
            eglReady = mcfm_ios_egl_init(Unmanaged.passUnretained(layer).toOpaque()) != 0
            if !eglReady {
                eglFailed = true
                onGraphicsFailure?()
                return
            }
        }
        checkSize()
    }

    /// The surface follows the layer: a new size reaches the game before its next frame.
    func checkSize() {
        var w: Int32 = 0, h: Int32 = 0
        mcfm_ios_egl_size(&w, &h)
        if w > 0 && h > 0 && (w != pixelWidth || h != pixelHeight) {
            pixelWidth = w
            pixelHeight = h
            onResize?(w, h)
        }
    }

    /// Before each engine frame: ANGLE current, the window surface (framebuffer 0) bound.
    func beginFrame() {
        checkSize()
        mcfm_ios_egl_begin_frame()
    }

    /// After it: shown; the frame's GL error (0: none).
    func endFrame() -> UInt32 { mcfm_ios_egl_end_frame() }

    // MARK: Touches: a small, stable id per finger, pixels.

    private var touchIds: [ObjectIdentifier: Int32] = [:]

    private func feed(_ touches: Set<UITouch>, _ action: Int32) {
        for t in touches {
            let key = ObjectIdentifier(t)
            var id = touchIds[key]
            if id == nil {
                guard action == 0 else { continue }
                var free: Int32 = 0
                while touchIds.values.contains(free) { free += 1 }
                touchIds[key] = free
                id = free
            }
            let p = t.location(in: self)
            mcfm_ios_touch(action, id!, Float(p.x * contentScaleFactor), Float(p.y * contentScaleFactor))
            if action >= 2 { touchIds[key] = nil }
        }
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { feed(touches, 0) }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { feed(touches, 1) }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { feed(touches, 2) }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { feed(touches, 3) }

    // MARK: Keys and text. The view stays first responder so hardware keys always arrive; the
    // soft keyboard shows only while the game has a text box open (an empty input view hides it).
    // While a text box is open the launcher hands hardware presses back to UIKit too
    // (ios/launcher/ios_keys.h): UIKit types into this UIKeyInput only for presses that reach
    // super, through insertText / deleteBackward, Return as insertText("\n") (Enter once).

    private let noKeyboard = UIView(frame: .zero)
    private var softKeyboard = false
    override var canBecomeFirstResponder: Bool { true }
    override var inputView: UIView? { softKeyboard ? nil : noKeyboard }

    func setSoftKeyboard(_ shown: Bool) {
        softKeyboard = shown
        reloadInputViews()
        if !isFirstResponder { becomeFirstResponder() }
    }

    /// The engine gets each key press; the result is the presses UIKit gets too.
    private func feed(_ presses: Set<UIPress>, down: Bool) -> Set<UIPress> {
        presses.filter { p in
            guard let key = p.key else { return true }
            return mcfm_ios_key(Int32(key.keyCode.rawValue), down ? 1 : 0) != 0
        }
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        let rest = feed(presses, down: true)
        if !rest.isEmpty { super.pressesBegan(rest, with: event) }
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        let rest = feed(presses, down: false)
        if !rest.isEmpty { super.pressesEnded(rest, with: event) }
    }

    override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        let rest = feed(presses, down: false)
        if !rest.isEmpty { super.pressesCancelled(rest, with: event) }
    }

    var hasText: Bool { true }  // backspace reaches the game's text even before anything is typed here
    var autocorrectionType: UITextAutocorrectionType { get { .no } set {} }
    var autocapitalizationType: UITextAutocapitalizationType { get { .none } set {} }
    var spellCheckingType: UITextSpellCheckingType { get { .no } set {} }
    var returnKeyType: UIReturnKeyType { get { .done } set {} }

    func insertText(_ text: String) {
        if text == "\n" { mcfm_ios_return(softKeyboard ? 1 : 0) } else { mcfm_ios_text(text) }
    }

    func deleteBackward() { mcfm_ios_backspace() }
}

/// A picked picture as the PNG the engine reads (its pixels whatever the file's DPI, as the Mac
/// launcher's image_pick.mm).
func writePNG(from source: URL, to png: URL) -> Bool {
    guard let src = CGImageSourceCreateWithURL(source as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(src, 0, nil) else { return false }
    try? FileManager.default.createDirectory(at: png.deletingLastPathComponent(), withIntermediateDirectories: true)
    try? FileManager.default.removeItem(at: png)
    guard let dest = CGImageDestinationCreateWithURL(png as CFURL, UTType.png.identifier as CFString, 1, nil) else { return false }
    CGImageDestinationAddImage(dest, image, nil)
    return CGImageDestinationFinalize(dest)
}
