# mcpe-kbm

Keyboard + mouse, Win10 desktop GUI and a resizable window for the iOS build of
Minecraft PE 0.15.10 running natively on Apple Silicon macOS.

## Build and install

    make test       # host tests
    make install    # rebuilds MinecraftPE-Mac/minecraftpe2.app from the original + dylib
    make smoke      # launches it and checks the mod loaded
    open /Users/dayvid/Downloads/Payload/MinecraftPE-Mac/minecraftpe2.app

The original `minecraftpe2.app` is never modified. Worlds live outside the bundle,
so `make install` keeps them.

## Controls

WASD move · mouse look · left click break/attack · right click place/use · Space jump ·
Shift sneak · 1–9 / scroll hotbar · E inventory · Esc pause · T chat

## How it works

See `docs/superpowers/specs/2026-10-06-mcpe-kbm-design.md`.
