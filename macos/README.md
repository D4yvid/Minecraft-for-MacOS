# macOS

Two macOS builds live here. The default is the **Mach-O launcher** (`launcher/`, `tools/make_launcher.sh`;
`make app`, `make check`, `make run`, see [docs/LAUNCHER.md](../docs/LAUNCHER.md)). The Mac
Catalyst app (`src/`, `tools/convert.sh`) is a **deprecated build mode**, kept working:
`make catalyst`, `make catalyst-check`, `make catalyst-run`.

## The launcher (`make app` → `dist/launcher/`)

- `launcher/main.mm` — `mcfm-launch`: loads the converted game (our loader, or dyld with
  `--loader dyld`), the AppKit window on ANGLE/Metal, the frame timer, the menu; `--frames N`,
  `--screenshot`, `--print-hooks`
- `launcher/input.mm`, `mac_keymap.cpp`, `mouse_math.h`, `resize_math.h` — keys, buttons,
  wheel, raw GCMouse look (`MCFM_LOOK_SCALE`), pointer capture, text entry, window size
- `launcher/loader_macos.cpp` — the `LoaderOS` of our loader (`shared/loader`) on macOS
- `launcher/audio_toolbox.cpp` — the AudioToolbox subset FMOD uses, on CoreAudio
  (`libmcfm_audiotoolbox.dylib`)
- `launcher/image_pick.mm` — custom skins through an open panel
- `launcher/stub_runtime.c` — the runtime of the generated framework stubs
- `tools/make_launcher.sh` — builds `dist/launcher` from your decrypted app (converted image,
  stubs, ANGLE, the audio provider, `mcfm-launch`; the game's `data/` is read from your app)
- `tools/loader_check.cpp` — `make loader-check`: our loader vs dyld on every fixup

Worlds: `~/Library/Application Support/MinecraftPE-mcfm/games/com.mojang/`.

## Mac Catalyst (deprecated, `make catalyst` → `dist/minecraftpe.app`)

- `src/main.mm` — entry point: Win10 UI + keyboard/mouse on the Apple address platform
- `src/mac_input.mm` — GameController keyboard/mouse, hover, touch suppression
- `src/pointer_lock.mm` — CoreGraphics pointer capture driven by the game
- `src/resize.mm` — tells the engine the window size; minimum window size
- `src/titlebar.mm` — hidden title bar that fades in from the top-left corner
- `tools/convert.sh` — builds `dist/minecraftpe.app` from your decrypted app

Worlds: `~/Documents/games/com.mojang/`.
