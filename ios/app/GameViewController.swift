import AVFoundation
import PhotosUI
import UIKit

/// The game, full screen in landscape: a display link runs one engine frame per screen refresh
/// on the main thread (the engine's thread) while the app is active.
final class GameViewController: UIViewController, PHPickerViewControllerDelegate {
    private let gameView = GameView(frame: .zero)
    private var displayLink: CADisplayLink?
    private var started = false, active = true
    fileprivate static weak var current: GameViewController?

    override func loadView() { view = gameView }

    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
    override var preferredScreenEdgesDeferringSystemGestures: UIRectEdge { .all }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }

    override func viewDidLoad() {
        super.viewDidLoad()
        GameViewController.current = self
        // FMOD plays through RemoteIO, which needs an active audio session; ambient mixes with
        // other apps' audio, as the game's own iOS code asked for.
        try? AVAudioSession.sharedInstance().setCategory(.ambient)
        try? AVAudioSession.sharedInstance().setActive(true)
        gameView.onResize = { [weak self] w, h in self?.resized(w, h) }
        let link = CADisplayLink(target: self, selector: #selector(step))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
        link.add(to: .main, forMode: .common)
        displayLink = link
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        gameView.becomeFirstResponder()
    }

    /// The first drawable starts the game; later ones resize it.
    private func resized(_ w: Int32, _ h: Int32) {
        if started { mcfm_ios_resize(w, h); return }
        let bundle = Bundle.main
        let image = (bundle.privateFrameworksPath ?? "") + "/libminecraftpe.dylib"
        let data = (bundle.resourcePath ?? "") + "/game/data/"
        // Worlds and options in Documents (as the original iOS game): the Files app shows them.
        let home = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].path
        let callbacks = McfmIosCallbacks(
            show_keyboard: { GameViewController.current?.gameView.setSoftKeyboard(true) },
            hide_keyboard: { GameViewController.current?.gameView.setSoftKeyboard(false) },
            pick_image: { GameViewController.current?.pickImage() },
            fatal: { message in
                let text = message.map { String(cString: $0) } ?? "unknown error"
                GameViewController.current?.showFatal(text)
            })
        mcfm_ios_egl_begin_frame()
        started = mcfm_ios_start(image, data, home, w, h, callbacks) != 0
        // The scene's state now, whatever events came before the first drawable.
        let state = view.window?.windowScene?.activationState
        if started { setActive(state == .foregroundActive) }
    }

    // --frames N [--screenshot <file in Documents>]: after N frames, the last one read back and
    // written as PPM (the device check, ios/tools/app_check.sh), then exit.
    private lazy var framesLeft: Int = {
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--frames"), i + 1 < args.count { return Int(args[i + 1]) ?? 0 }
        return 0
    }()
    private var framesDone = 0

    @objc private func step() {
        guard started, active else { return }
        gameView.beginFrame()
        mcfm_ios_frame()
        framesDone += 1
        if framesLeft > 0 && framesDone == framesLeft { finishFrames() }
        let error = gameView.endFrame()
        if error != 0 && framesDone < 4 { print("mcfm: GL error 0x\(String(error, radix: 16)) in frame \(framesDone)") }
    }

    private func finishFrames() {
        let w = Int(gameView.pixelWidth), h = Int(gameView.pixelHeight)
        var rgba = [UInt8](repeating: 0, count: w * h * 4)
        mcfm_ios_egl_read_pixels(Int32(w), Int32(h), &rgba)
        var rgb = [UInt8](repeating: 0, count: w * h * 3)
        var lit = 0
        for y in 0..<h {  // GL rows go bottom-up; PPM top-down
            for x in 0..<w {
                let s = ((h - 1 - y) * w + x) * 4, d = (y * w + x) * 3
                rgb[d] = rgba[s]; rgb[d + 1] = rgba[s + 1]; rgb[d + 2] = rgba[s + 2]
                if Int(rgba[s]) + Int(rgba[s + 1]) + Int(rgba[s + 2]) > 30 { lit += 1 }
            }
        }
        print("mcfm: \(framesDone) frames rendered (\(w)x\(h), \(100 * lit / max(1, w * h))% not black)")
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--screenshot"), i + 1 < args.count {
            let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent(args[i + 1])
            var data = Data("P6\n\(w) \(h)\n255\n".utf8)
            data.append(contentsOf: rgb)
            try? data.write(to: url)
            print("mcfm: screenshot \(url.lastPathComponent)")
        }
        exit(0)
    }

    /// The scene left or came back to the screen: suspend (saves) / resume, no frames between.
    func setActive(_ value: Bool) {
        print("mcfm: active \(value)")
        active = value
        displayLink?.isPaused = !value
        if !value { mcfm_ios_egl_finish() }
        mcfm_ios_pause(value ? 0 : 1)
    }

    // MARK: "Choose New Skin": the photo picker, the picture as PNG in the game's temp dir.

    func pickImage() {
        var config = PHPickerConfiguration()
        config.filter = .images
        config.selectionLimit = 1
        let picker = PHPickerViewController(configuration: config)
        picker.delegate = self
        present(picker, animated: true)
    }

    func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
        picker.dismiss(animated: true)
        guard let item = results.first?.itemProvider, item.hasItemConformingToTypeIdentifier(UTType.image.identifier) else {
            mcfm_ios_image_picked(nil)
            return
        }
        let home = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let png = home.appendingPathComponent("tmp/newSkin.png")
        item.loadFileRepresentation(forTypeIdentifier: UTType.image.identifier) { url, _ in
            let ok = url.map { writePNG(from: $0, to: png) } ?? false  // the file is gone after this block
            DispatchQueue.main.async {
                if ok { mcfm_ios_image_picked(png.path) } else { mcfm_ios_image_picked(nil) }
            }
        }
    }

    // MARK: The game cannot run.

    func showFatal(_ message: String) {
        let alert = UIAlertController(title: "Minecraft PE cannot start", message: message, preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "Close", style: .default) { _ in exit(0) })
        present(alert, animated: true)
    }
}
