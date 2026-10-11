# Copy to config.mk (git-ignored) and point at your own copies of the game.
# Keep comments on their own lines: make keeps spaces before an inline "#".

# Decrypted iOS Minecraft PE 0.15.10 .app, unzipped (the macOS launcher: make app; the legacy
# IPA mod also takes an .ipa). Default: game-files/ios/Payload/minecraftpe2.app (make game-files).
GAME = /path/to/minecraftpe2.app

# The decrypted .ipa itself, bundled by the Android and iOS apps (make android-app / ios-app).
# Default: the .ipa in game-files/ios/.
# IPA = /path/to/minecraftpe2.ipa

# Android Minecraft PE 0.15.10 APK (the legacy mod: make android-apk)
# APK = /path/to/minecraftpe-0.15.10.apk

# Android NDK r10c, if not in the default place
# NDK = $(HOME)/Library/Android/ndk/android-ndk-r10c
