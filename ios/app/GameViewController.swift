import AVFoundation
import PhotosUI
import UIKit

/// The launcher's callbacks come on the main thread (the engine's); should one ever come from
/// another thread, it runs on the main thread all the same.
private func onMain(_ work: @escaping () -> Void) {
    if Thread.isMainThread { work() } else { DispatchQueue.main.async(execute: work) }
}

/// The game, full screen in landscape: a display link runs one engine frame per screen refresh
/// on the main thread (the engine's thread) while the app is active.
final class GameViewController: UIViewController, PHPickerViewControllerDelegate {
    private let gameView = GameView(frame: .zero)
    private var displayLink: CADisplayLink?
    private var started = false, starting = false, active = true, fatalShown = false
    private let splash = SplashView(frame: .zero)
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
        // After an interruption (a call, an alarm, Siri) the session must be active again, or the
        // game stays silent.
        NotificationCenter.default.addObserver(forName: AVAudioSession.interruptionNotification, object: nil,
                                               queue: .main) { note in
            let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt
            if raw.flatMap(AVAudioSession.InterruptionType.init) == .ended {
                try? AVAudioSession.sharedInstance().setActive(true)
            }
        }
        // The device check's screenshot is written by this launch, never one left by an earlier one.
        if let name = argument("--screenshot") {
            try? FileManager.default.removeItem(at: documents.appendingPathComponent(name))
        }
        gameView.onResize = { [weak self] w, h in self?.resized(w, h) }
        gameView.onGraphicsFailure = { [weak self] in
            self?.showFatal("This device cannot draw the game: OpenGL ES 3 (ANGLE on Metal) did not start.")
        }
        splash.frame = gameView.bounds
        splash.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        gameView.addSubview(splash)
        let link = CADisplayLink(target: self, selector: #selector(step))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
        link.add(to: .main, forMode: .common)
        displayLink = link
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        gameView.becomeFirstResponder()
    }

    /// The first drawable starts the game, one run loop turn later so the splash is on screen
    /// first (the load blocks the main thread); later ones resize it.
    private func resized(_ w: Int32, _ h: Int32) {
        if started { mcfm_ios_resize(w, h); return }
        if starting { return }
        starting = true
        DispatchQueue.main.async { [weak self] in self?.start() }
    }

    private func start() {
        let w = gameView.pixelWidth, h = gameView.pixelHeight
        let bundle = Bundle.main
        let image = (bundle.privateFrameworksPath ?? "") + "/libminecraftpe.dylib"
        let data = (bundle.resourcePath ?? "") + "/game/data/"
        // Worlds and options in Documents (as the original iOS game): the Files app shows them.
        // The game's temp files go to the app's tmp (ios/launcher/launcher.mm).
        let home = documents.path
        let callbacks = McfmIosCallbacks(
            show_keyboard: { onMain { GameViewController.current?.gameView.setSoftKeyboard(true) } },
            hide_keyboard: { onMain { GameViewController.current?.gameView.setSoftKeyboard(false) } },
            pick_image: { onMain { GameViewController.current?.pickImage() } },
            fatal: { message in
                let text = message.map { String(cString: $0) } ?? "unknown error"  // copied before any hop
                onMain { GameViewController.current?.showFatal(text) }
            })
        mcfm_ios_egl_begin_frame()
        started = mcfm_ios_start(image, data, home, w, h, callbacks) != 0
        // The scene's state now, whatever events came before the first drawable.
        let state = view.window?.windowScene?.activationState
        if started { setActive(state == .foregroundActive) }
    }

    // --frames N [--screenshot <file in Documents>]: after N frames, the last one read back and
    // written as PPM (the device check, ios/tools/app_check.sh), then exit.
    private lazy var framesLeft: Int = argument("--frames").flatMap { Int($0) } ?? 0
    private var framesDone = 0

    @objc private func step() {
        guard started, active else { return }
        gameView.beginFrame()
        mcfm_ios_frame()
        framesDone += 1
        if framesLeft > 0 && framesDone == framesLeft { finishFrames() }
        let error = gameView.endFrame()
        if framesDone == 1 { splash.dismiss() }  // the first frame is on screen
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
        if let name = argument("--screenshot") {
            let url = documents.appendingPathComponent(name)
            var data = Data("P6\n\(w) \(h)\n255\n".utf8)
            data.append(contentsOf: rgb)
            try? data.write(to: url)
            print("mcfm: screenshot \(url.lastPathComponent)")
        }
        exit(0)
    }

    /// The scene left or came back to the screen: suspend (saves) / resume, no frames between.
    /// Leaving, the save runs inside a background task (time to finish should the app go on to
    /// the background), then the GPU finishes the game's last GL work. The screen does not sleep
    /// while the game is on it.
    func setActive(_ value: Bool) {
        print("mcfm: active \(value)")
        active = value
        displayLink?.isPaused = !value
        UIApplication.shared.isIdleTimerDisabled = value
        if value {
            mcfm_ios_pause(0)
            return
        }
        let app = UIApplication.shared
        var task = UIBackgroundTaskIdentifier.invalid
        task = app.beginBackgroundTask(withName: "mcfm: save") {
            app.endBackgroundTask(task)
            task = .invalid
        }
        mcfm_ios_pause(1)
        mcfm_ios_egl_finish()
        if task != .invalid { app.endBackgroundTask(task) }
    }

    // MARK: "Choose New Skin": the photo picker, the picture as PNG in the app's tmp.

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
        let png = FileManager.default.temporaryDirectory.appendingPathComponent("newSkin.png")
        item.loadFileRepresentation(forTypeIdentifier: UTType.image.identifier) { url, _ in
            let ok = url.map { writePNG(from: $0, to: png) } ?? false  // the file is gone after this block
            DispatchQueue.main.async {
                if ok { mcfm_ios_image_picked(png.path) } else { mcfm_ios_image_picked(nil) }
            }
        }
    }

    // MARK: The game cannot run.

    /// Once (the first reason is the one that matters), one run loop turn later: this may come
    /// during layout, before the view is on screen.
    func showFatal(_ message: String) {
        guard !fatalShown else { return }
        fatalShown = true
        print("mcfm: cannot start: \(message)")
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            let alert = UIAlertController(title: "Minecraft PE cannot start", message: message, preferredStyle: .alert)
            alert.addAction(UIAlertAction(title: "Close", style: .default) { _ in exit(0) })
            self.present(alert, animated: true)
        }
    }
}

private var documents: URL { FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0] }

/// The value after a launch argument (--frames 300), if any.
private func argument(_ name: String) -> String? {
    let args = CommandLine.arguments
    guard let i = args.firstIndex(of: name), i + 1 < args.count else { return nil }
    return args[i + 1]
}
