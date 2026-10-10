# usage: survey.py <db> <out.json>
# Stage 0 survey for the Mach-O launcher (docs/research/macho-launcher.md):
#   - every import, grouped by library, with the functions that use it (through stubs,
#     class refs and GOT slots), split into iOS glue and the rest;
#   - "seams": direct calls from the engine (below ENGINE_END) into code that reaches
#     Apple-only APIs within a few calls;
#   - the static initializer list.
# iOS glue = ObjC methods + AppPlatform_iOS vtable slots + functions only they call.
import collections, json, sys, idapro
idapro.open_database(sys.argv[1], run_auto_analysis=False)
import idautils, idc, ida_funcs, ida_nalt, ida_segment, ida_bytes

IOS_VTABLE, IOS_SLOTS = 0x100EABE00, 103
ENGINE_END = 0x100700000  # engine code ends here; third-party libs and iOS glue follow
APPLE_ONLY = ("libobjc", "Foundation", "UIKit", "CoreFoundation", "CFNetwork", "Security",
              "StoreKit", "XSAPITCUI", "AudioToolbox", "SystemConfiguration", "GameController",
              "WebKit", "AVFoundation", "CoreGraphics", "QuartzCore")


def func_start(ea):
    f = ida_funcs.get_func(ea)
    return f.start_ea if f else None


def is_thunk(start):
    f = ida_funcs.get_func(start)
    return bool(f and f.flags & ida_funcs.FUNC_THUNK) or idc.get_segm_name(start) in ("__stubs", "__stub_helper")


def users(ea, depth=0, seen=None):
    """Functions that reach `ea` through code refs, stubs and data slots (classrefs, GOT)."""
    seen = seen if seen is not None else set()
    out = set()
    if depth > 3 or ea in seen:
        return out
    seen.add(ea)
    for r in idautils.XrefsTo(ea):
        s = func_start(r.frm)
        if s is not None and not is_thunk(s):
            out.add(s)
        else:
            out |= users(s if s is not None else r.frm, depth + 1, seen)
    return out


def callees(f):
    out = set()
    for h in idautils.FuncItems(f):
        for x in idautils.CodeRefsFrom(h, False):
            s = func_start(x)
            if s is not None and s != f and idc.get_segm_name(s) == "__text":
                out.add(s)
    return out


def lib(module):
    return module.split("/")[-1].split(".")[0]


names = {s: idc.get_func_name(s) for s in idautils.Functions()}
label = lambda s: "%x %s" % (s, names.get(s, "?"))

# 1. imports by module
imports = {}
for i in range(ida_nalt.get_import_module_qty()):
    mod = ida_nalt.get_import_module_name(i) or "?"
    def cb(ea, name, ordinal, mod=mod):
        imports.setdefault(mod, []).append((ea, name or "#%d" % ordinal))
        return True
    ida_nalt.enum_import_names(i, cb)

# 2. iOS glue: ObjC methods + AppPlatform_iOS slots, then everything only they call
objc_methods = {s for s, n in names.items() if n[:2] in ("-[", "+[")}
ios_slots = {func_start(ida_bytes.get_qword(IOS_VTABLE + 8 * k)) for k in range(IOS_SLOTS)} - {None}
callers = {s: {c for c in (func_start(r) for r in idautils.CodeRefsTo(s, False)) if c not in (None, s)}
           for s in names}
glue = objc_methods | ios_slots
changed = True
while changed:
    changed = False
    for s, cs in callers.items():
        if s not in glue and cs and cs <= glue:
            glue.add(s)
            changed = True

# 3. users of each import, split glue / non-glue
report = {"functions": len(names), "engine_functions": sum(1 for s in names if s < ENGINE_END),
          "objc_methods": len(objc_methods), "ios_slots": len(ios_slots), "glue_functions": len(glue),
          "modules": {}}
apple_users = set(objc_methods)
for mod, syms in sorted(imports.items()):
    m = report["modules"].setdefault(mod, {})
    for ea, name in syms:
        us = users(ea)
        rest = sorted(u for u in us if u not in glue)
        m[name] = {"users": len(us), "non_glue": [label(u) for u in rest],
                   "engine": [label(u) for u in rest if u < ENGINE_END]}
        if lib(mod) in APPLE_ONLY:
            apple_users |= us

# 4. seams: engine -> code that reaches an Apple-only API within 4 calls
memo = {}
def reach(f, depth):
    if f in apple_users:
        return [f]
    if depth == 0 or (f, depth) in memo:
        return memo.get((f, depth))
    memo[(f, depth)] = None
    for c in callees(f):
        if c >= ENGINE_END:
            path = reach(c, depth - 1)
            if path:
                memo[(f, depth)] = [f] + path
                return memo[(f, depth)]
    return None

seams = collections.defaultdict(set)
for f in names:
    if f < ENGINE_END:
        for c in callees(f):
            if c >= ENGINE_END and reach(c, 4):
                seams[c].add(f)
report["seams"] = [{"target": label(c), "engine_callers": [label(f) for f in sorted(fs)],
                    "path": [label(p) for p in reach(c, 4)]} for c, fs in sorted(seams.items())]

# 5. static initializers
for s in idautils.Segments():
    seg = ida_segment.getseg(s)
    if ida_segment.get_segm_name(seg) == "__mod_init_func":
        report["mod_init_func"] = ["%x" % ida_bytes.get_qword(a) for a in range(seg.start_ea, seg.end_ea, 8)]

with open(sys.argv[2], "w") as fh:
    json.dump(report, fh, indent=1)
idapro.close_database(save=False)
