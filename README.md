# mcpe-kbm

Keyboard + mouse, Win10 desktop GUI and a resizable window for the iOS build of
Minecraft PE 0.15.10 running natively on Apple Silicon macOS.

## Build and install

    make test       # host tests
    make install    # rebuilds MinecraftPE-Mac/minecraftpe2.app from the original + dylib
    make smoke      # launches it and checks the mod loaded
    open /Users/dayvid/Downloads/Payload/MinecraftPE-Mac/minecraftpe2.app

`make install` refuses to run while the game is open (quit it first so nothing is lost).
`make smoke` launches the game itself and closes it after 15 seconds.

The original `minecraftpe2.app` is never modified. Worlds live outside the bundle,
so `make install` keeps them.

## Controls

WASD move · mouse look · left click break/attack · right click place/use · Space jump ·
Shift sneak · 1–9 / scroll hotbar · E inventory · Esc pause · T chat

## Window

Resizable; the title bar is hidden and the game uses the full window. Move the pointer
to the top-left corner to reveal the title bar; it stays until the pointer moves below it.

The launch-time App Store receipt check ("Sign in with your Apple Account") is skipped;
in-app purchases are unavailable in this copy anyway.

## How it works

See `docs/superpowers/specs/2026-10-06-mcpe-kbm-design.md`.
