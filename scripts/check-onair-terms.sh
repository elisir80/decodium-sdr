#!/bin/sh
set -eu

prefix=${1:?usage: check-onair-terms.sh STAGING macos-or-linux-or-windows}
platform=${2:?usage: check-onair-terms.sh STAGING macos-or-linux-or-windows}
case "$platform" in
    macos) base="$prefix/decodium-sdr.app/Contents/Resources" ;;
    linux|windows) base="$prefix/share/decodium-sdr" ;;
    *) printf 'Unsupported platform: %s\n' "$platform" >&2; exit 2 ;;
esac

for file in OnAir/ADDITIONAL-TERMS.md OnAir/NOTICE.md OnAir/THIRD_PARTY.md licenses/LICENSE licenses/THIRD_PARTY_LICENSES; do
    if [ ! -s "$base/$file" ]; then
        printf 'Missing or empty OnAir attribution file: %s\n' "$base/$file" >&2
        exit 1
    fi
done
printf 'OnAir attribution files present in %s\n' "$base"
