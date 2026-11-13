#!/usr/bin/env bash
# A/B: system DLL is allowed, Hamlib's libusb must be bundled.
set -eu
source_root="$1"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/dsdr-win-package.XXXXXXXX")"
trap 'rm -rf "$test_root"' EXIT
mkdir -p "$test_root/tools" "$test_root/package/bin"
cat > "$test_root/tools/objdump" <<'SCRIPT'
#!/bin/sh
printf ' DLL Name: mscms.dll\n DLL Name: libusb-1.0.dll\n'
SCRIPT
chmod +x "$test_root/tools/objdump"
: > "$test_root/package/bin/decodium-sdr.exe"
if PATH="$test_root/tools:$PATH" bash "$source_root/scripts/check-windows-package.sh" "$test_root/package" > "$test_root/missing.log" 2>&1; then
    echo 'Missing libusb incorrectly accepted' >&2; exit 1
fi
grep -q 'libusb-1.0.dll' "$test_root/missing.log"
if grep -q 'mscms.dll' "$test_root/missing.log"; then
    echo 'Windows system library incorrectly reported missing' >&2; exit 1
fi
: > "$test_root/package/bin/libusb-1.0.dll"
PATH="$test_root/tools:$PATH" bash "$source_root/scripts/check-windows-package.sh" "$test_root/package"
