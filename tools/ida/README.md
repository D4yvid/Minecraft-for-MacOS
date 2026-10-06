# IDA scripts

Used to find everything in the stripped iOS binary (IDA Pro 9 + idalib, Python 3).

```sh
python3 -m venv .venv && .venv/bin/pip install "<IDA>/idalib/python/idapro-*.whl"
.venv/bin/python "<IDA>/idalib/python/py-activate-idalib.py" -d "<IDA>"
idat -A -B minecraftpe            # one-time auto-analysis -> minecraftpe.i64
```

- `q.py <db> d <ea> | x <ea> | c <ea> | s <text>` — decompile, xrefs, callees, strings
- `recon.py <db>` — decompiles the touch/controller entry points and string users
- `platcalls2.py <db>` — every AppPlatform virtual call by vtable offset
- `slots.py <db>`, `vt.py <db> <fn>` — read AppPlatform vtable slots
- `funcs_range.py <db> <lo> <hi>` — list functions in an address range

Run them with `python -I`. Results feed `shared/apple/addresses_0_15_10.h`.
