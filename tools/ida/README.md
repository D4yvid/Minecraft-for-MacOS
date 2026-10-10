# IDA scripts

Used to find everything in the stripped iOS binary (IDA Pro 9 + idalib, Python 3).
The database is `game-files/ida/minecraftpe2-arm64.i64` (`make game-files IOS=<app> IDA=1`).

```sh
IDA="/Applications/IDA Professional 9.4.app/Contents/MacOS"
python3 -m venv .venv && .venv/bin/pip install "$IDA"/idalib/python/idapro-*.whl
.venv/bin/python -I "$IDA/idalib/python/py-activate-idalib.py" -d "$IDA"
.venv/bin/python -I tools/ida/q.py game-files/ida/minecraftpe2-arm64.i64 d 0x10070f670
```

- `q.py <db> d <ea> | x <ea> | c <ea> | s <text>` — decompile, xrefs, callees, strings
- `recon.py <db>` — decompiles the touch/controller entry points and string users
- `platcalls2.py <db>` — every AppPlatform virtual call by vtable offset
- `slots.py <db>`, `vt.py <db> <fn>` — read AppPlatform vtable slots
- `funcs_range.py <db> <lo> <hi>` — list functions in an address range

Run them with `python -I`. Results feed `shared/apple/addresses_0_15_10.h`.
