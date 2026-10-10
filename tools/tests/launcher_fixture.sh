#!/bin/bash
# usage: launcher_fixture.sh <outdir>
# Builds a stand-in for the game binary: a thin arm64 executable tagged for iOS, with
# classic (opcode) fixups like the 2016 game, importing a fake framework and libobjc,
# with an ObjC class and a static initializer that calls into both.
set -euo pipefail
OUT="$1"; mkdir -p "$OUT"
CC=(clang -arch arm64 -mmacosx-version-min=11.0)
cat > "$OUT/fakekit.c" <<'EOF'
int fakekit_hello(void) { return 1; }
int fakekit_callback(void) { return 2; }
char kFakeKitValue[16] = "real";
EOF
"${CC[@]}" -dynamiclib "$OUT/fakekit.c" \
  -install_name /System/Library/Frameworks/FakeKit.framework/FakeKit -o "$OUT/fakekit.dylib"
cat > "$OUT/fixture.m" <<'EOF'
#import <objc/NSObject.h>
int fakekit_hello(void);
extern char kFakeKitValue[16];
int fakekit_callback(void);
// Reached only through a data pointer (like a callback table): bound in __const, not lazily.
int (*const fixture_callbacks[])(void) = {fakekit_callback};
@interface MCFMFixture : NSObject
@end
@implementation MCFMFixture
+ (void)poke {}
@end
__attribute__((constructor)) static void fixture_init(void) {
  fakekit_hello();
  volatile char first = kFakeKitValue[0];  // a real read, so the data import exists
  (void)first;
  [MCFMFixture poke];
}
int main(void) { return 0; }
EOF
"${CC[@]}" ${FIXTURE_LDFLAGS:--Wl,-headerpad,0x1000} -fno-objc-arc -fno-objc-msgsend-selector-stubs \
  -Wl,-no_fixup_chains "$OUT/fixture.m" -lobjc "$OUT/fakekit.dylib" -o "$OUT/fixture.mac"
vtool -set-build-version ios 15.0 15.0 -replace -output "$OUT/fixture" "$OUT/fixture.mac"
rm -f "$OUT/fixture.mac"
