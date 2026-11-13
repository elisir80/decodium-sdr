# SELCAL comparison against OnAir

Run `sh tests/standalone/selcal_onair/run.sh` from the project root. Set
`ONAIR_ROOT` to a checkout of OnAir 3.9.0 if it is not at the default path.
The test builds into `build/selcal-onair-ab` and deliberately links a silent
audio sink that cannot open a device. Both implementations receive identical
synthetic 8 kHz samples: four different calls, each complete, incomplete and
noise-only, single-tone, three-tone, short-tone and overlapping-pair negatives, with offsets −1.5/0/+1.5 Hz,
two noise levels and three deterministic seeds.

This establishes agreement for the tested synthetic cases only; it does not
establish performance at larger frequency offsets, real-radio performance, or
platform support.

`sh tests/standalone/selcal_onair/run.sh --wide` runs a stricter diagnostic
matrix through ±5 Hz and uniform-noise amplitude 0.25, requiring Decodium to
decode all complete cases and reject the negative cases. The reference result
is logged, but is not required to match on complete cases: after a sensitivity
change Decodium decoded 24 cases that OnAir missed in the original 540-case
matrix. The expanded negative matrix includes single- and three-tone signals.
This does not establish a false-alarm rate on real speech. No sound device is opened.
