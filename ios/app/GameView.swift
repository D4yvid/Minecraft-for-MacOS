import ImageIO
import OpenGLES
import UIKit
import UniformTypeIdentifiers

/// The game's view: an OpenGL ES 3 drawable at the screen's native resolution, the source of
/// touches and hardware keys, and the soft keyboard's text input (to the engine's text box).
final class GameView: UIView, UIKeyInput {
    override class var layerClass: AnyClass { CAEAGLLayer.self }

    let context = EAGLContext(api: .openGLES3)!
    private var framebuffer: GLuint = 0, color: GLuint = 0, depth: GLuint = 0
    private(set) var pixelWidth: Int32 = 0, pixelHeight: Int32 = 0
    /// The drawable changed size: (width, height) in pixels.
    var onResize: ((Int32, Int32) -> Void)?

    override init(frame: CGRect) {
        super.init(frame: frame)
        let layer = self.layer as! CAEAGLLayer
        layer.isOpaque = true
        layer.drawableProperties = [kEAGLDrawablePropertyRetainedBacking: false,
                                    kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8]
        isMultipleTouchEnabled = true
    }

    required init?(coder: NSCoder) { fatalError("not from a nib") }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if let screen = window?.windowScene?.screen { contentScaleFactor = screen.nativeScale }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        var w: Int32 = 0, h: Int32 = 0
        mcfm_ios_pixel_size(Double(bounds.width), Double(bounds.height), Double(contentScaleFactor), &w, &h)
        if w > 0 && h > 0 && (w != pixelWidth || h != pixelHeight) { rebuildFramebuffer() }
    }

    /// One framebuffer: a colour renderbuffer from the layer and a depth/stencil one.
    private func rebuildFramebuffer() {
        EAGLContext.setCurrent(context)
        if framebuffer != 0 {
            glDeleteFramebuffers(1, &framebuffer)
            glDeleteRenderbuffers(1, &color)
            glDeleteRenderbuffers(1, &depth)
        }
        glGenFramebuffers(1, &framebuffer)
        glBindFramebuffer(GLenum(GL_FRAMEBUFFER), framebuffer)
        glGenRenderbuffers(1, &color)
        glBindRenderbuffer(GLenum(GL_RENDERBUFFER), color)
        context.renderbufferStorage(Int(GL_RENDERBUFFER), from: layer as! CAEAGLLayer)
        glFramebufferRenderbuffer(GLenum(GL_FRAMEBUFFER), GLenum(GL_COLOR_ATTACHMENT0), GLenum(GL_RENDERBUFFER), color)
        glGetRenderbufferParameteriv(GLenum(GL_RENDERBUFFER), GLenum(GL_RENDERBUFFER_WIDTH), &pixelWidth)
        glGetRenderbufferParameteriv(GLenum(GL_RENDERBUFFER), GLenum(GL_RENDERBUFFER_HEIGHT), &pixelHeight)
        glGenRenderbuffers(1, &depth)
        glBindRenderbuffer(GLenum(GL_RENDERBUFFER), depth)
        glRenderbufferStorage(GLenum(GL_RENDERBUFFER), GLenum(GL_DEPTH24_STENCIL8), pixelWidth, pixelHeight)
        glFramebufferRenderbuffer(GLenum(GL_FRAMEBUFFER), GLenum(GL_DEPTH_STENCIL_ATTACHMENT), GLenum(GL_RENDERBUFFER), depth)
        let status = glCheckFramebufferStatus(GLenum(GL_FRAMEBUFFER))
        if status != GL_FRAMEBUFFER_COMPLETE { print("mcfm: framebuffer incomplete (0x\(String(status, radix: 16)))") }
        onResize?(pixelWidth, pixelHeight)
    }

    /// Before each engine frame: iOS has no framebuffer 0, the game draws into ours (as its own
    /// -[EAGLView setFramebuffer] did).
    func bindFramebuffer() {
        EAGLContext.setCurrent(context)
        glBindFramebuffer(GLenum(GL_FRAMEBUFFER), framebuffer)
        glViewport(0, 0, pixelWidth, pixelHeight)
    }

    func present() {
        glBindRenderbuffer(GLenum(GL_RENDERBUFFER), color)
        context.presentRenderbuffer(Int(GL_RENDERBUFFER))
    }

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

    private let noKeyboard = UIView(frame: .zero)
    private var softKeyboard = false
    override var canBecomeFirstResponder: Bool { true }
    override var inputView: UIView? { softKeyboard ? nil : noKeyboard }

    func setSoftKeyboard(_ shown: Bool) {
        softKeyboard = shown
        reloadInputViews()
        if !isFirstResponder { becomeFirstResponder() }
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var unhandled = Set<UIPress>()
        for p in presses {
            if let key = p.key { mcfm_ios_key(Int32(key.keyCode.rawValue), 1) } else { unhandled.insert(p) }
        }
        super.pressesBegan(unhandled, with: event)
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var unhandled = Set<UIPress>()
        for p in presses {
            if let key = p.key { mcfm_ios_key(Int32(key.keyCode.rawValue), 0) } else { unhandled.insert(p) }
        }
        super.pressesEnded(unhandled, with: event)
    }

    override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        pressesEnded(presses, with: event)
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
