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
        gameView.bindFramebuffer()
        started = mcfm_ios_start(image, data, home, w, h, callbacks) != 0
    }

    @objc private func step() {
        guard started, active else { return }
        gameView.bindFramebuffer()
        mcfm_ios_frame()
        gameView.present()
    }

    /// The scene left or came back to the screen: suspend (saves) / resume, no frames between.
    func setActive(_ value: Bool) {
        active = value
        displayLink?.isPaused = !value
        if !value { glFinish() }
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
