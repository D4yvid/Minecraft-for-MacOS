import sys, idapro, re, collections
idapro.open_database(sys.argv[1], run_auto_analysis=False)
import idautils, idc, ida_bytes, ida_funcs, ida_hexrays
ida_hexrays.init_hexrays_plugin()
PLAT=0x100F5E850; IOSVT, BASEVT = 0x100eabe00, 0x100e649c0
funcs = {ida_funcs.get_func(x).start_ea for x in idautils.DataRefsTo(PLAT) if ida_funcs.get_func(x)}
pat = re.compile(r'\*\(_QWORD \*\)qword_100F5E850 \+ (\d+)LL\)')
calls = collections.defaultdict(set)
for f in funcs:
    try: t = str(ida_hexrays.decompile(f))
    except Exception: continue
    for m in pat.finditer(t): calls[int(m.group(1))].add(idc.get_func_name(f))
def first(fn):
    e = fn; out=[]
    for _ in range(3): out.append(idc.GetDisasm(e).split(';')[0].strip()); e = idc.next_head(e)
    return " | ".join(out)
for off in sorted(calls):
    b = ida_bytes.get_qword(BASEVT+off); i = ida_bytes.get_qword(IOSVT+off)
    print("+%-4d callers=%-3d base=%x {%s} ios=%s  %s" % (off, len(calls[off]), b, first(b)[:60], "same" if i==b else "%x {%s}" % (i, first(i)[:60]), sorted(calls[off])[:3]))
idapro.close_database(save=False)
