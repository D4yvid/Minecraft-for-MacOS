# macOS

Runs the iOS build natively on Apple Silicon as a Mac Catalyst app. See the top-level
README for build steps (`make app`, `make check`, `make run`).

- `src/main.mm` — entry point: Win10 UI + keyboard/mouse on the Apple address platform
- `src/mac_input.mm` — GameController keyboard/mouse, hover, touch suppression
- `src/pointer_lock.mm` — CoreGraphics pointer capture driven by the game
- `src/resize.mm` — tells the engine the window size; minimum window size
- `src/titlebar.mm` — hidden title bar that fades in from the top-left corner
- `tools/convert.sh` — builds `dist/minecraftpe.app` from your decrypted app
