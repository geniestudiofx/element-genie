# Builds "Genie Lamp.glb": a smooth, high-poly sample model for comparing Element Genie against Element 3D.
# Lathed body + lid + knob, swept spout and handle, three PBR materials. Pure numpy, no other deps.
import json, struct, sys, numpy as np

def bez(p, t):
    p = np.asarray(p, float); t = np.asarray(t)[:, None]
    return (1-t)**3*p[0] + 3*(1-t)**2*t*p[1] + 3*(1-t)*t**2*p[2] + t**3*p[3]

def smooth_normals(V, F):
    N = np.zeros_like(V)
    fn = np.cross(V[F[:,1]]-V[F[:,0]], V[F[:,2]]-V[F[:,0]])
    for k in range(3): np.add.at(N, F[:,k], fn)
    return N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)

def grid_faces(rows, cols, wrap=True):
    F = []
    cc = cols if wrap else cols-1
    for i in range(rows-1):
        for j in range(cc):
            a = i*cols+j; b = i*cols+(j+1) % cols; c = a+cols; d = b+cols
            F += [[a, c, b], [b, c, d]]
    return np.array(F, np.uint32)

def lathe(profile, seg=160):
    # profile: list of (r, y) from bottom to top
    pr = np.asarray(profile, float); th = np.linspace(0, 2*np.pi, seg, endpoint=False)
    V = np.array([[r*np.cos(t), y, r*np.sin(t)] for r, y in pr for t in th])
    return V, grid_faces(len(pr), seg)

def sweep(path, radius, seg=48):
    path = np.asarray(path, float); n = len(path)
    T = np.gradient(path, axis=0); T /= np.linalg.norm(T, axis=1, keepdims=True)
    up = np.array([0, 0, 1.0])
    V = []
    Nrm = np.cross(T[0], up); Nrm /= np.linalg.norm(Nrm)
    for i in range(n):  # parallel-transport frame
        if i: Nrm = Nrm - T[i]*np.dot(Nrm, T[i]); Nrm /= np.linalg.norm(Nrm)
        B = np.cross(T[i], Nrm)
        for t in np.linspace(0, 2*np.pi, seg, endpoint=False):
            V.append(path[i] + radius[i]*(np.cos(t)*Nrm + np.sin(t)*B))
    V = np.array(V); F = grid_faces(n, seg)
    # cap the ends with a fan so nothing is open
    caps = []
    for ring, flip in ((0, True), (n-1, False)):
        c = len(V); V = np.vstack([V, path[ring]])
        for j in range(seg):
            a = ring*seg+j; b = ring*seg+(j+1) % seg
            caps.append([c, b, a] if flip else [c, a, b])
    return V, np.vstack([F, np.array(caps, np.uint32)])

def smoothstep_profile(pts, n):
    # Catmull-Rom through control points (r, y) for a silky lathe silhouette
    P = np.asarray(pts, float); P = np.vstack([P[0], P, P[-1]]); out = []
    for i in range(1, len(P)-2):
        for t in np.linspace(0, 1, n, endpoint=False):
            p0, p1, p2, p3 = P[i-1], P[i], P[i+1], P[i+2]
            out.append(0.5*((2*p1) + (-p0+p2)*t + (2*p0-5*p1+4*p2-p3)*t*t + (-p0+3*p1-3*p2+p3)*t**3))
    out.append(P[-2]); return out

parts = []
# Body: squat bowl on a foot
body = smoothstep_profile([(0.0,0.0),(0.34,0.0),(0.36,0.03),(0.26,0.08),(0.24,0.12),(0.55,0.28),(0.82,0.48),
                           (0.86,0.58),(0.70,0.74),(0.42,0.84),(0.40,0.88),(0.0,0.88)], 18)
parts.append(("body",) + lathe(body))
# Lid and knob
lid = smoothstep_profile([(0.0,0.86),(0.46,0.86),(0.47,0.90),(0.36,0.98),(0.16,1.04),(0.10,1.06),(0.13,1.12),
                          (0.10,1.20),(0.0,1.22)], 16)
parts.append(("trim",) + lathe(lid))
# Spout: tapering swept tube rising out of the body
t = np.linspace(0, 1, 140)
sp = bez([(0.60,0.50,0),(1.10,0.52,0),(1.30,0.70,0),(1.62,1.02,0)], t)
parts.append(("body",) + sweep(sp, 0.17*(1-t)**1.4 + 0.035, 56))
# Spout tip ring
tip = sp[-1] + np.array([0.012,0.012,0]); ang = np.linspace(0, 2*np.pi, 120)
ring = np.array([tip + np.array([-0.7071*np.sin(a)*0.05, 0.7071*np.sin(a)*0.05, np.cos(a)*0.05]) for a in ang])
parts.append(("trim",) + sweep(ring, np.full(len(ring), 0.014), 24))
# Handle: loop on the back
h = bez([(-0.70,0.70,0),(-1.35,0.95,0),(-1.40,0.25,0),(-0.72,0.30,0)], np.linspace(0, 1, 140))
parts.append(("trim",) + sweep(h, np.full(len(h), 0.055), 40))
# Belt ring around the widest point
ang = np.linspace(0, 2*np.pi, 240); belt = np.c_[0.865*np.cos(ang), np.full_like(ang, 0.56), 0.865*np.sin(ang)]
parts.append(("gem",) + sweep(belt, np.full(len(belt), 0.022), 24))

mats = {"body": ([0.78,0.76,0.86,1], 1.0, 0.22), "trim": ([0.55,0.48,1.0,1], 1.0, 0.16), "gem": ([0.45,0.36,1.0,1], 0.0, 0.08)}
order = list(mats)

bin_ = bytearray(); views = []; accs = []; prims = {m: [] for m in order}
def add(data, target):
    while len(bin_) % 4: bin_.append(0)
    off = len(bin_); bin_.extend(data.tobytes()); views.append({"buffer":0,"byteOffset":off,"byteLength":data.nbytes,"target":target}); return len(views)-1
for mat, V, F in parts:
    V = V.astype(np.float32); N = smooth_normals(V, F).astype(np.float32)
    vp = add(V, 34962); vn = add(N, 34962); vi = add(F.astype(np.uint32).ravel(), 34963)
    accs.append({"bufferView":vp,"componentType":5126,"count":len(V),"type":"VEC3","min":V.min(0).tolist(),"max":V.max(0).tolist()})
    accs.append({"bufferView":vn,"componentType":5126,"count":len(V),"type":"VEC3"})
    accs.append({"bufferView":vi,"componentType":5125,"count":int(F.size),"type":"SCALAR"})
    prims[mat].append({"attributes":{"POSITION":len(accs)-3,"NORMAL":len(accs)-2},"indices":len(accs)-1,"material":order.index(mat)})
allp = [p for m in order for p in prims[m]]
gl = {"asset":{"version":"2.0","generator":"Element Genie sample"},"scene":0,"scenes":[{"nodes":[0]}],
      "nodes":[{"mesh":0,"name":"Genie Lamp"}],"meshes":[{"name":"Genie Lamp","primitives":allp}],
      "materials":[{"name":m,"pbrMetallicRoughness":{"baseColorFactor":c,"metallicFactor":me,"roughnessFactor":r}} for m,(c,me,r) in mats.items()],
      "accessors":accs,"bufferViews":views,"buffers":[{"byteLength":len(bin_)}]}
js = json.dumps(gl, separators=(",",":")).encode()
js += b" " * ((4 - len(js) % 4) % 4)
while len(bin_) % 4: bin_.append(0)
out = struct.pack("<III", 0x46546C67, 2, 12+8+len(js)+8+len(bin_)) + struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(bin_), 0x004E4942) + bin_
open(sys.argv[1], "wb").write(out)
print("tris", sum(len(F) for _,_,F in parts), "bytes", len(out))
