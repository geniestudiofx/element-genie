# Packs files for the installer: args are "folder/name=path" (or just a path -> Models/<name>).
import struct, sys, os
out = open(sys.argv[1], "wb"); items = []
for a in sys.argv[2:]:
    name, path = a.split("=", 1) if "=" in a else ("Models/" + os.path.basename(a), a)
    items.append((name, path))
out.write(struct.pack("<I", len(items)))
for name, path in items:
    n = name.encode(); b = open(path, "rb").read()
    out.write(struct.pack("<I", len(n)) + n + struct.pack("<I", len(b)) + b)
