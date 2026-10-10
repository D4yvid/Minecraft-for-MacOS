# macOS

Two macOS builds live here. The default is the **Mach-O launcher** (`launcher/`, `tools/make_launcher.sh`;
`make app`, `make check`, `make run`, see [docs/LAUNCHER.md](../docs/LAUNCHER.md)). The Mac
Catalyst app (`src/`, `tools/convert.sh`) is a **deprecated build mode**, kept working:
`make catalyst`, `make catalyst-check`, `make catalyst-run`.

- `src/main.mm` — entry point: Win10 UI + keyboard/mouse on the Apple address platform
- `src/mac_input.mm` — GameController keyboard/mouse, hover, touch suppression
- `src/pointer_lock.mm` — CoreGraphics pointer capture driven by the game
- `src/resize.mm` — tells the engine the window size; minimum window size
- `src/titlebar.mm` — hidden title bar that fades in from the top-left corner
- `tools/convert.sh` — builds `dist/minecraftpe.app` from your decrypted app
