import sys, idapro
idapro.open_database(sys.argv[1], run_auto_analysis=False)
import idautils, idc, ida_bytes, ida_hexrays
ida_hexrays.init_hexrays_plugin()
base_fn = int(sys.argv[2],16)   # base getEdition
slots = [744, 768, 792, 216, 752, 760]
for ref in idautils.DataRefsTo(base_fn):
    # vtable slot address -> vtable start = ref - 744
    vt = ref - 744
    print("vtable candidate %x (slot ref %x) name=%s" % (vt, ref, idc.get_name(vt)))
# find all vtables whose slot 744 is a function that refs string "pocket"/"iOS"/etc: scan data segment for pointers to functions at offset
# simpler: list every data ref to functions returning edition strings
for s in idautils.Strings():
    if str(s) in ("iOS","ios","pocket","win10"):
        for r in idautils.DataRefsTo(s.ea):
            f = idc.get_func_attr(r, idc.FUNCATTR_START)
            if f != idc.BADADDR:
                for vr in idautils.DataRefsTo(f):
                    print("str %-6s func %x  referenced from data %x (vt if slot744 -> %x)" % (str(s), f, vr, vr-744))
idapro.close_database(save=False)
