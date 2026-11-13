#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Optional macOS-only diagnostic; no audio is played and no fixture is shipped.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
binary="${1:-$root/build/bin/tst_selcal_audio}"
mkdir -p "$root/build/tmp"
task_dir="$(mktemp -d "$root/build/tmp/selcal-speech.XXXXXX")"
trap 'rm -rf "$task_dir"' EXIT
export DSDR_TEST_SILENT_AUDIO=1
say -v Samantha -o "$task_dir/us.aiff" 'Mayday, this is test station one two three. Requesting a radio check on this frequency.'
say -v Daniel -o "$task_dir/uk.aiff" 'Speedbird five two six, maintain flight level three five zero. Contact control and good day.'
say -v Alice -o "$task_dir/it.aiff" 'Controllo radio stazione di prova, attendiamo ulteriori istruzioni. Grazie e buona giornata.'
for voice in us uk it; do
    afconvert -f WAVE -d LEI16@8000 -c 1 "$task_dir/$voice.aiff" "$task_dir/$voice.wav"
done
python3 - "$task_dir" <<'PY'
import pathlib, struct, sys, wave
root = pathlib.Path(sys.argv[1])
for name in ("us", "uk", "it"):
    with wave.open(str(root / (name + ".wav")), "rb") as f:
        if (f.getnchannels(), f.getsampwidth(), f.getframerate()) != (1, 2, 8000):
            raise SystemExit("unexpected speech fixture format")
        raw = f.readframes(f.getnframes())
    if len(raw) < 16000 or max(map(abs, struct.unpack("<%dh" % (len(raw)//2), raw))) < 500:
        raise SystemExit("speech fixture empty or nearly silent: " + name)
    (root / (name + ".raw")).write_bytes(raw)
PY
"$binary" "$task_dir/us.raw" "$task_dir/uk.raw" "$task_dir/it.raw"
printf '%s\n' 'SELCAL speech A/B passed: three generated voices'
