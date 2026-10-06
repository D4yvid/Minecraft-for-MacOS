# usage: q.py <db> cmd arg [cmd arg ...]
#   d <ea>      decompile function containing ea
#   x <ea>      list code+data xrefs to ea (with containing func)
#   c <ea>      list callees of function containing ea
#   s <text>    find strings containing text, with referencing funcs
import sys, idapro
idapro.open_database(sys.argv[1], run_auto_analysis=False)
import idautils, idc, ida_hexrays, ida_funcs

ida_hexrays.init_hexrays_plugin()


def fn(ea):
    f = ida_funcs.get_func(ea)
    return (f.start_ea, idc.get_func_name(f.start_ea)) if f else (None, "?")


def decomp(ea):
    f = ida_funcs.get_func(ea)
    if not f:
        return "// no func at %x" % ea
    try:
        return "// %s @ %x\n%s" % (idc.get_func_name(f.start_ea), f.start_ea, ida_hexrays.decompile(f.start_ea))
    except Exception as e:
        return "// decompile failed %x: %s" % (f.start_ea, e)


args = sys.argv[2:]
for cmd, a in zip(args[::2], args[1::2]):
    print("\n######## %s %s" % (cmd, a))
    if cmd == "d":
        print(decomp(int(a, 16)))
    elif cmd == "x":
        ea = int(a, 16)
        for r in idautils.XrefsTo(ea):
            s, n = fn(r.frm)
            print("%x  type=%d  in %s %s" % (r.frm, r.type, hex(s) if s else "-", n))
    elif cmd == "c":
        f = ida_funcs.get_func(int(a, 16))
        seen = set()
        for h in idautils.FuncItems(f.start_ea):
            for x in idautils.CodeRefsFrom(h, False):
                s, n = fn(x)
                if s and s != f.start_ea and s not in seen:
                    seen.add(s)
                    print("%x %s" % (s, n))
    elif cmd == "s":
        for st in idautils.Strings():
            v = str(st)
            if a in v:
                refs = [(r, fn(r)) for r in idautils.DataRefsTo(st.ea)]
                print("%x %r -> %s" % (st.ea, v[:80], ", ".join("%x(%s)" % (r, n) for r, (s, n) in refs)))
idapro.close_database(save=False)
