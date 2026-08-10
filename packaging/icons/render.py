#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Genera i formati dell'icona a partire da `it.decodium.sdr.svg`.

I file prodotti sono **versionati**: la CI non deve saperli costruire. Ci
abbiamo già provato — generare l'icona al volo dipendeva da ImageMagick, che
sul runner Linux non c'è, e il ripiego lasciava un PNG di zero byte che
linuxdeploy rifiutava dopo aver fatto tutto il resto del lavoro. Questo script
si esegue a mano quando l'SVG cambia, e il risultato entra in un commit.

    python packaging/icons/render.py

Serve `rsvg-convert` (pacchetto librsvg) per rasterizzare. L'impacchettamento
in `.ico` e `.icns` è scritto qui perché ImageMagick e `iconutil` non sono
disponibili ovunque: entrambi i formati sono, in fondo, un indice più i PNG.
"""

import io
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SVG = os.path.join(HERE, "it.decodium.sdr.svg")

# Windows si ferma a 256; le due misure dispari (24 e 48) servono alle viste
# dell'esplora risorse, che altrimenti riscalano il 32 e lo sgranano.
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

# I quattro tipi @1x più i loro @2x: è ciò che macOS si aspetta di trovare, e
# un .icns incompleto fa comparire l'icona generica in qualche vista.
ICNS_TYPES = [
    (b"icp4", 16), (b"icp5", 32), (b"ic07", 128), (b"ic08", 256),
    (b"ic09", 512), (b"ic11", 32), (b"ic12", 64), (b"ic13", 512),
    (b"ic14", 1024), (b"ic10", 1024),
]


def rasterize(size):
    """PNG quadrato di `size` pixel, in memoria."""
    return subprocess.run(
        ["rsvg-convert", "-w", str(size), "-h", str(size), SVG],
        check=True, stdout=subprocess.PIPE).stdout


def write_ico(path, pngs):
    out = io.BytesIO()
    out.write(struct.pack("<HHH", 0, 1, len(pngs)))
    offset = 6 + 16 * len(pngs)
    for size, blob in pngs:
        # 256 si codifica come 0: il campo è un solo byte.
        dim = 0 if size >= 256 else size
        out.write(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32,
                              len(blob), offset))
        offset += len(blob)
    for _, blob in pngs:
        out.write(blob)
    with open(path, "wb") as f:
        f.write(out.getvalue())
    return len(out.getvalue())


def write_icns(path, cache):
    body = b""
    for icns_type, size in ICNS_TYPES:
        blob = cache[size]
        body += icns_type + struct.pack(">I", len(blob) + 8) + blob
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", len(body) + 8) + body)
    return len(body) + 8


def main():
    try:
        subprocess.run(["rsvg-convert", "--version"], check=True,
                       stdout=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        sys.stderr.write("serve rsvg-convert (pacchetto librsvg)\n")
        return 1

    wanted = sorted(set(ICO_SIZES + [s for _, s in ICNS_TYPES] + [256, 512]))
    cache = {size: rasterize(size) for size in wanted}

    # Il 256 è l'icona del desktop Linux, ed è il file che il workflow di
    # release copia nell'AppDir: il nome non va cambiato senza cambiare anche
    # `.github/workflows/release.yml`.
    for name, size in (("it.decodium.sdr.png", 256),
                       ("it.decodium.sdr-512.png", 512),
                       ("it.decodium.sdr-1024.png", 1024)):
        with open(os.path.join(HERE, name), "wb") as f:
            f.write(cache[size])
        print("%-28s %6d byte" % (name, len(cache[size])))

    ico = os.path.join(HERE, os.pardir, "windows", "decodium-sdr.ico")
    size = write_ico(ico, [(s, cache[s]) for s in ICO_SIZES])
    print("%-28s %6d byte (%d misure)" % ("windows/decodium-sdr.ico", size,
                                          len(ICO_SIZES)))

    icns = os.path.join(HERE, os.pardir, "macos", "decodium-sdr.icns")
    os.makedirs(os.path.dirname(icns), exist_ok=True)
    size = write_icns(icns, cache)
    print("%-28s %6d byte (%d tipi)" % ("macos/decodium-sdr.icns", size,
                                        len(ICNS_TYPES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
