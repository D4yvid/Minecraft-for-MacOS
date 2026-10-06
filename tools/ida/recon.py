import os, sys, idapro
idapro.open_database(sys.argv[1], run_auto_analysis=False)
import idaapi, idautils, idc, ida_hexrays, ida_funcs, ida_bytes, ida_auto

OUT = os.environ.get("RECON_OUT", "/tmp/recon")
os.makedirs(OUT, exist_ok=True)
ida_auto.auto_wait()
ida_hexrays.init_hexrays_plugin()


def decomp(ea):
    f = ida_funcs.get_func(ea)
    if not f:
        return "// no func at %x\n" % ea
    try:
        return "// %s @ %x\n%s\n" % (idc.get_func_name(f.start_ea), f.start_ea, ida_hexrays.decompile(f.start_ea))
    except Exception as e:
        return "// decompile failed %x: %s\n" % (f.start_ea, e)


def callees(ea):
    f = ida_funcs.get_func(ea)
    out = set()
    for h in idautils.FuncItems(f.start_ea):
        for x in idautils.CodeRefsFrom(h, False):
            g = ida_funcs.get_func(x)
            if g and g.start_ea != f.start_ea:
                out.add(g.start_ea)
    return sorted(out)


# 1. ObjC entry points: decompile them and their direct callees
entries = {
    "touchesBegan": 0x10070fe84, "touchesMoved": 0x100710004, "touchesEnded": 0x100710184,
    "startTrackingTouch": 0x10070fbac, "stopTrackingTouch": 0x10070fd00,
    "processButton": 0x100707dcc, "processStick": 0x100707e48, "processTrigger": 0x100707f3c,
    "setupHandlers": 0x1007080f8, "drawFrame": 0x10070f670, "initView": 0x10070f850,
}
for name, ea in entries.items():
    with open(os.path.join(OUT, "entry_%s.c" % name), "w") as fh:
        fh.write(decomp(ea))
        for c in callees(ea):
            fh.write("\n// ===== callee of %s\n" % name)
            fh.write(decomp(c))

# 2. functions referencing interesting strings
needles = ["key.jump", "key.attack", "key.use", "key.inventory", "key.forward", "key.left",
           "ctrl_usetouchscreen", "ctrl_keyboardlayout", "ctrl_usetouchjoypad", "keyboard_layout_screen",
           "keyboardLayout", "mouse", "not_mouse", "gesture_mouse_delta_x", "ctrl_gamePadMap",
           "button.jump", "touch", "pocket", "win10", "iOS", "ios", "_ZN8Keyboard7_inputsE", "desktop_screen", "pocket_edition", "win10_edition"]
strs = {}
for s in idautils.Strings():
    v = str(s)
    if v in needles:
        strs.setdefault(v, []).append(s.ea)
with open(os.path.join(OUT, "string_refs.txt"), "w") as fh:
    funcs_seen = {}
    for v, eas in strs.items():
        for sea in eas:
            refs = list(idautils.DataRefsTo(sea))
            # follow one level through cfstring / pointer
            for r in list(refs):
                if not ida_funcs.get_func(r):
                    refs += list(idautils.DataRefsTo(r))
            for r in refs:
                f = ida_funcs.get_func(r)
                if f:
                    fh.write("%-28s str@%x ref@%x func %x %s\n" % (v, sea, r, f.start_ea, idc.get_func_name(f.start_ea)))
                    funcs_seen.setdefault(f.start_ea, set()).add(v)
with open(os.path.join(OUT, "string_ref_funcs.c"), "w") as fh:
    for ea, vs in sorted(funcs_seen.items()):
        fh.write("\n// ===== refs %s\n" % sorted(vs))
        fh.write(decomp(ea))

idapro.close_database(save=True)
