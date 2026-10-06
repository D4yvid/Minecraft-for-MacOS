import sys, idapro
idapro.open_database(sys.argv[1], run_auto_analysis=False)
import idc, ida_bytes, ida_hexrays, idautils
ida_hexrays.init_hexrays_plugin()
for vt in (0x100e649c0, 0x100eabe00):
    print("==== vtable %x  refs:%s" % (vt, [hex(r) for r in idautils.DataRefsTo(vt-16)] + [hex(r) for r in idautils.DataRefsTo(vt)]))
    for off in (744, 768, 792):
        fn = ida_bytes.get_qword(vt+off)
        try: body = str(ida_hexrays.decompile(fn))
        except Exception as e: body = str(e)
        print("-- +%d -> %x %s\n%s" % (off, fn, idc.get_func_name(fn), body[:600]))
idapro.close_database(save=False)
