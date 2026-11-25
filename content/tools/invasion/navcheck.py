import struct, sys, math
def load(path):
    d = open(path,'rb').read(); o = 0
    def u32():
        nonlocal o; v = struct.unpack_from('<I', d, o)[0]; o += 4; return v
    assert u32() == 1 and u32() == 3
    o += 20; n = u32(); o += n*24
    pc = u32(); recs = [struct.unpack_from('<BBHI3f', d, o+i*20) for i in range(pc)]; o += pc*20
    npn = u32(); o += npn*4
    vc = u32(); verts = [struct.unpack_from('<3f', d, o+i*12) for i in range(vc)]; o += vc*12
    ic = u32(); idx = struct.unpack_from('<%dI'%ic, d, o)
    polys = []
    for cnt, mat, cl, off, *nrm in recs:
        polys.append(([verts[idx[off+c]] for c in range(cnt)], cl))
    return polys
def inside(pts, x, z):
    c = False; n = len(pts)
    for i in range(n):
        a, b = pts[i], pts[i-1]
        if (a[2] > z) != (b[2] > z) and x < (b[0]-a[0])*(z-a[2])/(b[2]-a[2]) + a[0]: c = not c
    return c
def height(pts):
    return sum(p[1] for p in pts)/len(pts)
def query(polys, x, y, z):
    hits = [(abs(height(p)-y), height(p), cl) for p, cl in polys if inside(p, x, z)]
    if hits: return sorted(hits)[0][1:]
    best = None
    for p, cl in polys:
        for q in p:
            dd = math.hypot(q[0]-x, q[2]-z) + abs(q[1]-y)
            if best is None or dd < best[0]: best = (dd, q, cl)
    return ('MISS nearest %.1f m' % best[0], best[1], best[2])
mesh = load(sys.argv[1])
for a in sys.argv[2:]:
    name, pos = a.split('=')
    x, y, z = map(float, pos.split('/'))
    print(name, pos, query(mesh, x, y, z))
