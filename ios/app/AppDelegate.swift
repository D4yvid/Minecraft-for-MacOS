import UIKit

/// Our iOS app (docs/LAUNCHER.md, Stage 4): a launcher for the game bundled in it. The scene
/// delegate owns the window and the game's lifecycle (the UIScene lifecycle is required from the
/// SDK after iOS 26).
@main
final class AppDelegate: UIResponder, UIApplicationDelegate {
    func application(_ application: UIApplication,
                     configurationForConnecting session: UISceneSession,
                     options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let config = UISceneConfiguration(name: "Game", sessionRole: session.role)
        config.delegateClass = SceneDelegate.self
        return config
    }
}

final class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?

    func scene(_ scene: UIScene, willConnectTo session: UISceneSession, options: UIScene.ConnectionOptions) {
        guard let scene = scene as? UIWindowScene else { return }
        let window = UIWindow(windowScene: scene)
        window.rootViewController = GameViewController()
        window.makeKeyAndVisible()
        self.window = window
    }

    // Leaving the screen (home, app switcher, lock, a call): the game saves before this returns,
    // and nothing is drawn until it comes back (iOS ends apps that use GL in the background).
    func sceneWillResignActive(_ scene: UIScene) { gameController?.setActive(false) }
    func sceneDidBecomeActive(_ scene: UIScene) { gameController?.setActive(true) }

    private var gameController: GameViewController? { window?.rootViewController as? GameViewController }
}
