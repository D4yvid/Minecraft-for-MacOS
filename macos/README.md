# macOS

Runs the iOS build natively on Apple Silicon as a Mac Catalyst app. **Deprecated build
mode**: it keeps working until the Mach-O launcher ([docs/LAUNCHER.md](../docs/LAUNCHER.md))
replaces it. Build steps: `make catalyst`, `make catalyst-check`, `make catalyst-run` (the old
names `app`/`check`/`run` are aliases).

- `src/main.mm` — entry point: Win10 UI + keyboard/mouse on the Apple address platform
- `src/mac_input.mm` — GameController keyboard/mouse, hover, touch suppression
- `src/pointer_lock.mm` — CoreGraphics pointer capture driven by the game
- `src/resize.mm` — tells the engine the window size; minimum window size
- `src/titlebar.mm` — hidden title bar that fades in from the top-left corner
- `tools/convert.sh` — builds `dist/minecraftpe.app` from your decrypted app
