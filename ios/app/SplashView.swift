import UIKit

/// Shown while the game loads (the image, the engine, the first frame take a second or two):
/// the launch screen's look with a spinner, faded out once the first frame is on screen.
final class SplashView: UIView {
    private let spinner = UIActivityIndicatorView(style: .large)

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .black
        let title = UILabel()
        title.text = "Minecraft PE"
        title.font = .systemFont(ofSize: 34, weight: .bold)
        title.textColor = .white
        let subtitle = UILabel()
        subtitle.text = "Loading…"
        subtitle.font = .systemFont(ofSize: 17)
        subtitle.textColor = UIColor(white: 0.7, alpha: 1)
        spinner.color = .white
        spinner.startAnimating()  // a Core Animation animation: it keeps turning while the load blocks the main thread
        let stack = UIStackView(arrangedSubviews: [title, spinner, subtitle])
        stack.axis = .vertical
        stack.alignment = .center
        stack.spacing = 16
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        NSLayoutConstraint.activate([stack.centerXAnchor.constraint(equalTo: centerXAnchor),
                                     stack.centerYAnchor.constraint(equalTo: centerYAnchor)])
    }

    required init?(coder: NSCoder) { fatalError("not from a nib") }

    func dismiss() {
        UIView.animate(withDuration: 0.25, animations: { self.alpha = 0 }, completion: { _ in self.removeFromSuperview() })
    }
}
