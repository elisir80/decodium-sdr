# -*- coding: utf-8 -*-
"""Da GeoJSON Natural Earth a una risorsa binaria compatta.

Douglas-Peucker con tolleranza in gradi, poi due int16 per punto (centesimi di
grado): 0.01 gradi sono circa un chilometro, che su una mappa da mille punti di
larghezza e' un decimo di pixel — piu' precisione sarebbe byte sprecati.

Formato:
    uint32  numero di polilinee
    per ogni polilinea:
        uint32 numero di punti
        int16  lon * 100, int16 lat * 100   (per punto)
"""
import json, struct, sys, math

def perp(p, a, b):
    (x, y), (x1, y1), (x2, y2) = p, a, b
    dx, dy = x2 - x1, y2 - y1
    if dx == 0 and dy == 0:
        return math.hypot(x - x1, y - y1)
    t = max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - (x1 + t * dx), y - (y1 + t * dy))

def simplify(pts, tol):
    if len(pts) < 3:
        return pts
    dmax, index = 0.0, 0
    for i in range(1, len(pts) - 1):
        d = perp(pts[i], pts[0], pts[-1])
        if d > dmax:
            dmax, index = d, i
    if dmax > tol:
        return simplify(pts[:index + 1], tol)[:-1] + simplify(pts[index:], tol)
    return [pts[0], pts[-1]]

sys.setrecursionlimit(20000)

src, dst, tol = sys.argv[1], sys.argv[2], float(sys.argv[3])
data = json.load(open(src, encoding="utf-8"))

lines = []
for feature in data["features"]:
    geom = feature["geometry"]
    parts = [geom["coordinates"]] if geom["type"] == "LineString" else geom["coordinates"]
    for part in parts:
        pts = [(float(a), float(b)) for a, b in part]
        # Una polilinea che scavalca l'antimeridiano va spezzata: in
        # equirettangolare un salto di 360 gradi disegnerebbe una riga
        # orizzontale che attraversa tutta la mappa.
        chunk = [pts[0]]
        for prev, cur in zip(pts, pts[1:]):
            if abs(cur[0] - prev[0]) > 180.0:
                if len(chunk) > 1:
                    lines.append(chunk)
                chunk = [cur]
            else:
                chunk.append(cur)
        if len(chunk) > 1:
            lines.append(chunk)

out, total = [], 0
for pts in lines:
    s = simplify(pts, tol)
    if len(s) >= 2:
        out.append(s)
        total += len(s)

with open(dst, "wb") as f:
    f.write(struct.pack("<I", len(out)))
    for pts in out:
        f.write(struct.pack("<I", len(pts)))
        for lon, lat in pts:
            f.write(struct.pack("<hh", round(lon * 100), round(lat * 100)))

print(f"{len(out)} polilinee, {total} punti, {4 + sum(4 + 4*len(p) for p in out)} byte")
