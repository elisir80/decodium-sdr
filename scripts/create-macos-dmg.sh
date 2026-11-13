#!/usr/bin/env bash
set -euo pipefail

source_dir=$1
output=$2
attempt=1
while :; do
  log=$(mktemp)
  if hdiutil create -volname 'DECODIUM SDR' -srcfolder "$source_dir" \
      -ov -format UDZO "$output" 2>"$log"; then
    cat "$log" >&2
    rm -f "$log"
    test -s "$output"
    exit 0
  fi
  cat "$log" >&2
  if ! grep -qi 'Resource busy' "$log" || [ "$attempt" -ge 4 ]; then
    rm -f "$log"
    exit 1
  fi
  rm -f "$log" "$output"
  echo "hdiutil busy; retry $attempt/3" >&2
  attempt=$((attempt + 1))
  sleep "${DSDR_DMG_RETRY_DELAY:-10}"
done
