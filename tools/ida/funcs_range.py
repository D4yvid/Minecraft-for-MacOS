import sys, idapro
idapro.open_database(sys.argv[1], run_auto_analysis=False)
import idautils, idc
lo, hi = int(sys.argv[2],16), int(sys.argv[3],16)
for f in idautils.Functions(lo, hi):
    refs = len(list(idautils.CodeRefsTo(f, False)))
    print("%x %-30s size=%-5d callers=%d" % (f, idc.get_func_name(f), idc.get_func_attr(f, idc.FUNCATTR_END)-f, refs))
idapro.close_database(save=False)
