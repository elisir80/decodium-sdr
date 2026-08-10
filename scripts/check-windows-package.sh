#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Verifica che un pacchetto Windows sia autosufficiente: ogni DLL importata
# dai suoi binari deve stare nel pacchetto o essere parte di Windows.
#
# Serve perché il difetto che presidia non si vede dove nasce. Su una macchina
# che compila il progetto, MSYS2 mette centinaia di DLL nel PATH e il pacchetto
# parte anche quando è incompleto; il messaggio «manca libx264» arriva solo
# all'utente, che è la persona peggio attrezzata per capirlo.
#
# Il modello è un Windows appena installato: le DLL di sistema si riconoscono
# da un elenco esplicito, non guardando in System32 della macchina corrente —
# lì ci sono anche i runtime che qualcun altro ha installato, e fidarsi di
# quelli significa scoprire il problema di nuovo a valle.
#
#   scripts/check-windows-package.sh <directory-del-pacchetto>
#
# La directory è quella che contiene bin/decodium-sdr.exe.

set -uo pipefail

ROOT="${1:-}"
if [ -z "$ROOT" ] || [ ! -d "$ROOT" ]; then
    echo "uso: $0 <directory-del-pacchetto>" >&2
    exit 2
fi
if [ ! -f "$ROOT/bin/decodium-sdr.exe" ]; then
    echo "ERRORE: $ROOT/bin/decodium-sdr.exe non esiste: non è un pacchetto" >&2
    exit 2
fi
if ! command -v objdump >/dev/null 2>&1; then
    echo "ERRORE: serve objdump (pacchetto binutils)" >&2
    exit 2
fi

# DLL presenti su qualunque Windows 10/11. L'elenco è esplicito e va allungato
# a mano: una riga in più qui è una decisione, una risoluzione automatica dal
# System32 locale sarebbe un'illusione.
SYSTEM_DLLS="
advapi32 authz avrt bcrypt cfgmgr32 comctl32 comdlg32 crypt32 cryptsp d2d1
d3d9 d3d11 d3d12 dbghelp dcomp dnsapi dsound dwmapi dwrite dxgi dxva2 evr
gdi32 hid imm32 iphlpapi kernel32 ksuser mf mfplat mfreadwrite mfuuid mpr
msvcrt ncrypt netapi32 normaliz ntdll ole32 oleacc oleaut32 opengl32 pdh
powrprof propsys psapi rpcrt4 secur32 setupapi shcore shell32 shlwapi
synchronization uiautomationcore urlmon user32 userenv usp10 uxtheme version
win32u windowscodecs winhttp wininet winmm winspool wintrust ws2_32 wsock32
wtsapi32
"

# Eccezioni: dipendenze che mancano di proposito, con il motivo. Una voce qui
# è una promessa fatta all'utente, non una riga per far tacere il controllo.
#
#   ftd2xx    — driver D2XX di FTDI, richiesto da colibrinano_lib.dll. Chi ha
#               un ColibriNANO lo ha già installato, perché senza quel driver
#               non funziona nemmeno il software del costruttore. Chi non ce
#               l'ha non carica mai quella libreria: il backend la cerca, non
#               la trova e lo dice.
#   msvcp140  — runtime Visual C++, richiesto dalla stessa libreria, che è
#   vcruntime140  compilata con MSVC e non da noi. Stesso ragionamento: si
#               presenta solo a chi ha la radio, insieme al suo driver.
KNOWN_MISSING="ftd2xx msvcp140 vcruntime140"

lower() { tr 'A-Z' 'a-z'; }

# Tutte le DLL del pacchetto. Windows cerca le dipendenze nella directory
# dell'eseguibile, quindi è bin/ che conta; le altre si elencano lo stesso,
# perché una DLL fuori posto è un difetto diverso ma pur sempre un difetto.
present=$(find "$ROOT" -type f -iname "*.dll" -printf "%f\n" | lower | sort -u)

missing_report=""
exit_code=0

while read -r binary; do
    [ -z "$binary" ] && continue
    while read -r dep; do
        [ -z "$dep" ] && continue
        low=$(printf '%s' "$dep" | lower)
        base="${low%.dll}"

        case "$low" in api-ms-*|ext-ms-*) continue ;; esac
        if printf '%s\n' "$present" | grep -qx "$low"; then continue; fi
        if printf '%s' "$SYSTEM_DLLS" | tr -s ' \n' '\n\n' | grep -qx "$base"; then continue; fi
        if printf '%s' "$KNOWN_MISSING" | tr ' ' '\n' | grep -qx "$base"; then continue; fi

        missing_report="${missing_report}${dep}|${binary#$ROOT/}"$'\n'
        exit_code=1
    done < <(objdump -p "$binary" 2>/dev/null | sed -n 's/.*DLL Name: //p' | tr -d '\r')
done < <(find "$ROOT" -type f \( -iname "*.dll" -o -iname "*.exe" \))

if [ $exit_code -eq 0 ]; then
    echo "pacchetto autosufficiente: nessuna dipendenza mancante"
    exit 0
fi

echo "DIPENDENZE MANCANTI — il pacchetto non parte su un Windows pulito:"
echo
printf '%s' "$missing_report" | sort -u | awk -F'|' '
    { chi[$1] = chi[$1] (chi[$1] ? ", " : "") $2 }
    END { for (d in chi) printf "  %-28s richiesta da: %s\n", d, chi[d] }
' | sort
echo
echo "Se una di queste manca di proposito, va dichiarata in KNOWN_MISSING"
echo "dentro $0, con il motivo — non tolta dal controllo."
exit 1
