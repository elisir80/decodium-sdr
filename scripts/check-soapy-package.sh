#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Verifica che i moduli Soapy richiesti siano davvero nel pacchetto, non solo
# nell'ambiente che lo ha compilato. L'argomento può essere uno staging tree,
# un AppDir estratto o direttamente un bundle .app macOS.

set -euo pipefail

ROOT="${1:-}"
shift || true
if [ -z "$ROOT" ] || [ ! -d "$ROOT" ]; then
    echo "uso: $0 <pacchetto-o-bundle> <modulo> [modulo ...]" >&2
    exit 2
fi
if [ "$#" -eq 0 ]; then
    echo "ERRORE: indicare almeno un modulo SoapySDR atteso" >&2
    exit 2
fi

if [ -d "$ROOT/Contents/lib/SoapySDR/modules0.8" ]; then
    MODULE_DIR="$ROOT/Contents/lib/SoapySDR/modules0.8"
elif [ -d "$ROOT/Contents/PlugIns/SoapySDR/modules0.8" ]; then
    # Layout accettato per bundle più vecchi, prima del root runtime Soapy.
    MODULE_DIR="$ROOT/Contents/PlugIns/SoapySDR/modules0.8"
else
    MODULE_DIR=$(find "$ROOT" -type d -path '*/SoapySDR/modules0.8' -print -quit)
fi

if [ -z "${MODULE_DIR:-}" ] || [ ! -d "$MODULE_DIR" ]; then
    echo "ERRORE: directory dei moduli SoapySDR assente da $ROOT" >&2
    exit 1
fi

for module in "$@"; do
    found=""
    for suffix in .so .dll .dylib; do
        candidate="$MODULE_DIR/$module$suffix"
        if [ -f "$candidate" ]; then
            found="$candidate"
            break
        fi
    done
    if [ -z "$found" ]; then
        echo "ERRORE: modulo SoapySDR assente: $module ($MODULE_DIR)" >&2
        exit 1
    fi
    echo "SoapySDR presente: $found"
done
