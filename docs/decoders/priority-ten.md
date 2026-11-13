# First ten priority receive modes

The user chose radio-oriented priorities, including the ADS-B, APRS and
POCSAG work already in progress, rather than the first ten television entries
in OnAir's generated list. This is a working implementation set; it does not
replace the full OnAir-parity goal.

1. ADS-B
2. APRS (AFSK 1200)
3. POCSAG
4. AIS
5. ACARS
6. VDL Mode 2
7. NAVTEX
8. DSC (digital selective calling)
9. VOR
10. SELCAL

The user explicitly excluded modes already available in Decodium 4 from this
first group. CW, RTTY, SSTV, FT8 and FT4 are therefore excluded. Radiofax is
also excluded: the Decodium 4 source already has a radiofax receive path.
The choice remains subject to an integration audit: do not count a feature as
new if further evidence shows it already exists in Decodium 4.

For each, a functioning result means an optional receive path integrated with
the application, explicit configuration and status, bounded asynchronous
processing and safe lifetime, plus A/B tests with the same input covering
valid and negative cases, discontinuity and shutdown. Document restrictions
and distinguish synthetic or recorded-input evidence from live RF, and
platform-specific build evidence from actually tested platforms. Do not count
a standalone experiment or a menu entry as a completed decoder. Run application
tests with `DSDR_TEST_SILENT_AUDIO=1`; keep generated files on Projects.

After these ten have been delivered and tested, pause the existing goal at the
user's request; the remaining OnAir modes remain outstanding.

## Evidence ledger (2026-11-13)

The repeatable sanitizer script and Linux CI job also include the APRS AFSK
decoder A/B for valid and bad FCS, streaming and reset. The expanded script
passed locally on macOS ARM64; remote Linux CI has not yet run.

The standalone POCSAG IQ A/B suite passed under ASan/UBSan on macOS ARM64,
covering 512/1200/2400 baud, tuning and noise variations, chunk sizes, rates
through 2 MHz, wrong-baud rejection and reset/recovery. These are synthetic
signals and do not establish performance with physical receivers.
This IQ suite is now part of the repeatable sanitizer script; the expanded
script passed locally. The Linux CI execution remains to be verified remotely.
The same script now also includes the POCSAG audio A/B matrix across three
baud rates, DC offsets, noise, polarity and chunk sizes; all checks passed
locally under ASan/UBSan on macOS ARM64.

The ACARS standalone direct-versus-worker synthetic IQ A/B passed under
ASan/UBSan on macOS ARM64 at 0 and 23 kHz offsets, alongside corrupt-CRC,
parity/layout and silence negative controls. This does not exercise the UI or
prove physical receiver performance.
The repeatable sanitizer script and Linux CI job now include that ACARS test;
the expanded script passed locally on macOS, while remote CI remains unrun.

The VOR identifier comparison against the OnAir generator passed 90
synthetic cases under ASan/UBSan on macOS ARM64, including present and absent
identifiers, voice and noise. This does not establish real-world identifier
accuracy or navigation suitability.
Three additional 60-second synthetic voice/no-identifier controls passed
under the same sanitizers without producing even a transient identifier.

The conventional VOR radial A/B against the local OnAir generator also passed
all 36 synthetic radial/noise/voice combinations under ASan/UBSan on macOS
ARM64. This is not a Doppler VOR, navigation, physical-receiver or UI test.

The existing SELCAL same-sample OnAir A/B matrix also passed under ASan and
UBSan on macOS ARM64, including complete calls and negative noise, incomplete,
single-tone, three-tone, short-tone and overlap cases. This is synthetic audio
at protocol level, not live RF or a full-app test.

The DSC format A/B comparison against the local OnAir reference passed under
ASan/UBSan on macOS ARM64 for the synthetic all-ships, individual, group,
area, distress acknowledgement and relay cases. This is a protocol-level
comparison, not a test of physical reception or the application interface.
The DSC production receiver/worker audio A/B against OnAir also passed under
ASan/UBSan on macOS ARM64; the tested signals were synthetic, and the QML
interface and physical RF path were outside this sanitizer run.

The NAVTEX protocol A/B comparison against the local OnAir reference passed
with AddressSanitizer and UndefinedBehaviorSanitizer on macOS ARM64: normal
and inverted synthetic input, both undamaged and damaged, produced the same
message. This does not test the application UI or live RF reception.
The NAVTEX audio-front-end A/B also passed with ASan/UBSan and UBSan
halt-on-error on macOS ARM64, including the deterministic signal/no-signal,
noise and interference cases. It remains synthetic, not over-the-air evidence.

The standalone ADS-B CPR A/B test passed under AddressSanitizer and
UndefinedBehaviorSanitizer on macOS ARM64. It exercises even/odd ordering,
the ten-second boundary, invalid CRC, different aircraft addresses and surface
messages. This is a synthetic protocol test, not an RF or UI verification.

AIS and POCSAG standalone A/B tests now have a repeatable ASan/UBSan script
and a Linux CI job. The script passed locally on macOS ARM64, covering clean,
corrupt and truncated POCSAG data and AIS valid/invalid GMSK cases. The new
Linux CI job has not yet run; this does not establish Linux execution or RF.
The sanitizer script defaults UBSan to halt on detection; a deliberately
overflowing local control returned an error, while the AIS, POCSAG and ACARS
checks passed with that setting. User-supplied UBSAN_OPTIONS can override it.
The same sanitizer script passed with an output path containing spaces; an
invalid compiler path returned an error rather than reporting a false success.

The standalone AIS synthetic GMSK/FCS A/B suite also passed under
AddressSanitizer and UndefinedBehaviorSanitizer on macOS ARM64. It compares
valid and corrupt frames across 24 rate, timing and polarity combinations,
and checks chunking and reset. This does not exercise the application UI or
prove behaviour with a physical receiver.

A deterministic APRS position-parser robustness test now mutates and
truncates 100,000 inputs derived from four valid packet formats, checks that
any decoded coordinates remain finite and in range, and verifies the four
originals still decode. It passed both CTest with Qt assertions forced on and
a separate AddressSanitizer/UndefinedBehaviorSanitizer run on macOS ARM64.
This bounded synthetic corpus is not a proof against all malformed inputs.

APRS timestamped position parsing now rejects messages shorter than the
seven-character timestamp before indexing it. The APRS engine test passed
with truncated `/` and `@` reports plus the existing valid-position and
synthetic IQ checks on macOS ARM64; it does not establish RF performance.
An A/B executable comparison with Qt assertions forced on confirmed that the
old parser aborts on the one-character `/` input while the fixed parser exits
normally. The engine regression also checks every incomplete prefix of one
timestamped uncompressed position and one compressed position, then accepts
each complete position.
The muted APRS IQ-file-to-dialog test additionally carries a valid-CRC AX.25
packet whose information field is only `/`. The source remains visible, no
position is displayed, and the same behavior holds after reconnect; all seven
IQ-session cases passed on macOS ARM64 with synthetic signals.
APRS items with a six-character name no longer get mistaken for timestamped
station reports merely because their position starts at the same offset. A/B
tests demonstrate the old parser rejected both uncompressed and compressed
examples while the corrected parser accepts them; the APRS engine suite passed.
The muted synthetic IQ-to-dialog test now uses a six-character item name and
verifies both the name and coordinates again after disconnect/reconnect; the
full APRS IQ-session test passed on macOS ARM64.

The packaging attribution check now rejects unknown platform names instead of
silently treating them as Linux/Windows. Its local A/B fixture passed for the
three supported layouts, missing and empty files, and a misspelled platform;
this checks the gate, not the contents of an actual release artifact.

The VDL2 Rust workspace built offline with a new, empty Cargo home and a new
target directory on macOS ARM64, without fetching crates. Its 23 Rust unit
tests and doctest commands passed. The local toolchain emitted a rust-objcopy
warning about missing libLLVM when stripping test binaries; these results do
not validate distribution packaging or the Windows and Linux toolchains.
After the Rust source warning cleanup, the Release ABI, frame adapter, and
muted synthetic IQ-to-QML enabled/disabled/retuning tests were rebuilt and all
five passed on macOS ARM64. This is not physical-radio or other-platform proof.
The current VDL2-enabled Release app was subsequently rebuilt and launched
offscreen with silent audio, isolated test settings and automatic window close;
its normal shutdown fences passed. The software/offscreen platform reported no
QRhi, so this smoke test does not validate GPU rendering or an installed bundle.

The muted AIS dialog test now feeds a position, a static report for the same
MMSI, and a fresh position: coordinates disappear for the static report and
return only on the following position report. The related engine and dialog
tests passed on macOS ARM64 with synthetic IQ; this is not live-RF evidence.

SELCAL was also exercised against three locally generated speech samples
(English US, English UK and Italian; about 7–10 seconds each) at 8 kHz.
For each sample, the decoder emitted no call during speech and then emitted
exactly the expected call after a one-second silence and a synthetic SELCAL
signal. The optional raw-fixture path is in `test_selcal_audio.cpp`; speech
files are local test artifacts, not distributed. These are synthesized voices,
not recorded airband speech, and do not prove general false-alarm performance.
On macOS this diagnostic can be repeated after building `tst_selcal_audio` by
running `bash tests/standalone/selcal_speech_macos.sh`. It checks that each
generated voice contains audio, keeps temporary files under the project build
directory, deletes them on exit, and performs no speaker playback. It is an
optional macOS diagnostic, not a substitute for portable CI or actual RF.

For conventional VOR, an independent OnAir generator A/B passed 36 combinations
of nine radials including 0 and 359 degrees, two synthetic noise levels and
voice on/off. Separate muted synthetic IQ-file-to-dialog tests passed at 0 and
359 degrees, including disconnect and reconnect. These checks do not establish
Doppler VOR, radio-frequency reception or navigational suitability.
VOR now uses a multistage decimator for input rates above 960 ksample/s and
accepts rates through 2.048 Msample/s. Synthetic 2 and 2.048 Msample/s engine
A/B and muted IQ-to-dialog/reconnect tests passed on macOS ARM64; a tap-only
Release measurement processed three seconds in 54–57 ms on this machine,
with adjacent-channel rejection and recovery. This is not a live RF,
cross-platform or sustained full-app performance guarantee.
The portable CTest high-rate test also checks stale-generation rejection and
recovery after non-finite IQ at both high rates; it passed on macOS ARM64.
The same A/B, adjacent-channel and recovery checks now pass at 960001,
1000000, 1300000 and 1536000 sample/s as well; a muted synthetic
1300000-sample/s IQ-to-dialog test also covers disconnect and reconnect.
At 1.3 and 2.048 Msample/s, irregular 317-sample blocks and a single large
input also produced the same synthetic radial within 0.01 degree.
A separate muted synthetic IQ-file-to-dialog run at 2 Msample/s checked the
radial every five seconds for 60 seconds, observed no new worker or result
drops and recorded 3002 GUI timer ticks on this Mac. This is not RF or
cross-platform evidence.
At 2 Msample/s the muted IQ-to-dialog test additionally tunes outside the
available IQ passband, checks that the previous radial and identifier clear,
and then tunes back and verifies recovery.
VOR's latest-result controller now consumes at most one result per GUI timer
tick. Existing direct-versus-worker, generation-invalidation and high-rate
tests, plus a muted 1.3 Msample/s IQ-to-dialog regression, pass on macOS.
A deterministic worker-barrier A/B also publishes a second result during the
first poll and verifies that it is consumed only on the following poll; three
consecutive executions passed on macOS ARM64. A separately compiled copy of
the previous unbounded loop failed that same test, while the current loop
passed; neither comparison changes the product source or transmits RF.

A separate muted POCSAG app test replayed synthetic IQ at 2 Msample/s for
60 seconds on macOS ARM64: the message counter increased from 5 to 133, the
worker reported no additional dropped blocks, and a GUI timer continued to run.
This does not measure sustained operation on other machines or live RF.
Muted synthetic POCSAG IQ-to-dialog tests also passed at 512 and 1200 baud
with 192 and 200 ksample/s input. For each baud at 192 ksample/s, an A/B
check received no message while configured at 2400 baud and recovered the
expected address and text when the correct rate was restored.
Muted synthetic AIS IQ-to-dialog tests additionally cover class A position
reports of types 1, 2 and 3. Each test injects corrupt-FCS frames before the
valid report and checks MMSI and coordinates through the application on macOS
ARM64; these synthetic cases do not establish over-the-air performance.
The ADS-B controller retains a recent CPR position across identification
messages from the same ICAO but clears it when the displayed aircraft changes
or the position expires. Muted IQ-to-dialog and timestamped synthetic A/B
tests cover same-aircraft identification, another aircraft and expired data.
ADS-B controller polling is limited to 64 messages per GUI update and stale
discard work to 256 queue entries per read. A synthetic 100-frame backlog
test checks that the first update handles 64 and the next handles the remaining
36 without an input-queue drop; it is not an RF throughput benchmark.
After this change, muted ADS-B IQ-to-dialog, window-close, menu-exit and
PTT-refusal/retry regression tests also passed on macOS ARM64.
APRS displays coordinates from uncompressed, unambiguous position reports
in the latest decoded packet. Tests cover north/east, south/west, timestamps,
invalid ranges and malformed input; synthetic muted NFM IQ-to-dialog tests
verify displayed coordinates and clearing on disconnect. Position ambiguity
and live RF remain outside this evidence.
Compressed positions with primary or secondary symbol table identifiers and
alphanumeric overlays are also decoded. The APRS 1.0 reference vector
(49.5, -72.75), numeric and alphabetic overlays, malformed and truncated
inputs, synthetic NFM and muted IQ-to-dialog/reconnect tests passed. The
coordinates are shown; the overlay symbol itself is not rendered. After the
overlay changes, the APRS engine, IQ-session, dialog and full-QML shutdown
regressions passed on macOS ARM64 with audio muted.
NAVTEX synthetic IQ engine A/B now covers both USB and LSB using the positive
audio filter bounds expected by the DSP. The muted LSB IQ-to-dialog case also
passes through disconnect, reconnect and channel reselection on macOS ARM64.
DSC similarly passed muted synthetic IQ engine A/B for USB and LSB, and an
LSB distress report was displayed again after disconnect/reconnect in the
IQ-to-dialog test. These sideband cases do not establish real RF reception.
Boundary checks also cover the north-pole/date-line edge and compressed
base-91 values outside geographic latitude or longitude limits.
An additional muted NFM engine A/B sends a position report, a non-position
status packet and another position report: coordinates clear for the status
packet and reappear only for the subsequent valid position.
Position reports with timestamps also validate day/hour/minute or
hour/minute/second ranges before accepting the coordinates; valid and invalid
timestamp boundaries have dedicated A/B cases.
An additional synthetic IQ-to-dialog case carries a valid-FCS packet with an
ambiguous latitude: the source remains visible while the position is explicitly
unavailable, including after reconnect.
Active APRS object reports with uncompressed, unambiguous positions expose
the object name separately from the sender; deleted objects do not retain a
position. Synthetic NFM engine A/B and muted IQ-to-dialog tests exercise the
active/deleted distinction, object name and disconnect behavior.
Active uncompressed APRS item reports with unambiguous coordinates and
3–9-character names, including embedded spaces, are also supported. Synthetic engine tests cover active and removed items, while a
muted IQ-to-dialog test checks the displayed name and disconnect/reconnect.
Following the item-name change, the five APRS engine, IQ-session and full-QML
shutdown tests and the focused APRS dialog tests passed sequentially on macOS
ARM64 with audio muted.

After those changes, ten selected engine/lifecycle tests covering the ten
priority decoders passed sequentially on macOS ARM64 with silent app audio.
The VDL2 test in that run exercises its app-manager lifecycle; it is not an
additional VDL2 over-the-air decoding test. This is a targeted regression run,
not the full project suite or proof of cross-platform support.

On macOS ARM64 the OnAir attribution component and its missing-file negative
control passed. A separate full temporary install using Qt's CMake deploy was
interrupted after repeated macdeployqt errors opening relative PlugIns paths;
the complete installed app and distribution package therefore remain unverified.
The errors and install attempt are recorded in build/onair-install.log.
The release workflow instead disables the generated deploy and invokes
macdeployqt with an absolute bundle path. A local trial of that route copied
the executable and notices without those relative-path errors, but was stopped
by the test's 180-second timeout while macdeployqt was still processing; its
completion and the final package are not verified by this trial.
The later retry using the release-style absolute-path macdeployqt completed
successfully, with all OnAir notices and the executable present and no reported
deploy errors. It was staged before subsequent APRS changes and the temporary
bundle was removed without a launch or runtime test, so it does not validate a
package built from the current worktree or prove standalone startup.

| Mode | Current evidence | Missing before counting as delivered |
| --- | --- | --- |
| ADS-B | App receiver; synthetic and real recorded-IQ comparison; airborne global CPR; synthetic IQ-file-to-dialog A/B verifies valid and bad-CRC frames, source disconnect clears aircraft and reconnect accepts a fresh frame; controller detaches only after shutdown is committed and remains attached if PTT release is refused; receiver is owned by the asynchronous shutdown barrier after DSP/backend fencing; full QML window-close and menu-exit tests with a deliberately held worker verify responsiveness, fake-backend close and delayed exit, including a PTT-off refusal/retry | Other OS/application exit paths and cross-platform verification, broader RF evidence remain; a direct destructor without the normal shutdown protocol can still block |
| APRS | App NFM receiver; synthetic IQ-to-controller A/B, CRC, channel, buffered-loss and muted/zero-volume tests at 192 and 200 ksample/s (including resampling); synthetic IQ-file-to-dialog A/B decodes a valid packet, rejects corrupted FCS, clears the packet on disconnect and decodes again after reconnect and NFM channel reselection; controller detaches after shutdown is committed; full QML window-close, menu-exit and PTT-refusal/retry tests with a held worker confirm the simulated radio closes and the GUI remains responsive while awaiting worker termination | Ordered loss metadata from every backend, other OS exit-path verification, final UI and cross-platform verification, recorded/RF evidence; a direct destructor without normal shutdown can still wait |
| POCSAG | App menu and session wiring; synthetic engine A/B with loss/recovery, source change and identical audio samples; synthetic IQ-file-to-dialog A/B at 192, 200, 1300 ksample/s and 2 Msample/s; disconnect clears history and reconnect restores reception at 192 ksample/s; delayed-worker shutdown; standalone IQ A/B at 200 ksample/s, 960 ksample/s, 1.536 and 2 Msample/s with irregular/whole chunks. On macOS ARM64, a 15-second synthetic 2 Msample/s app playback advanced from 5 to 37 messages without dropped worker blocks | Longer sustained-load, cross-platform and recorded/RF evidence remain; synthetic playback cannot establish performance on other hardware |
| AIS | Integrated app path and dialog; synthetic IQ A/B through engine and IQ-file session, with position and MMSI cleared on disconnect and reception restored on reconnect; asynchronous worker shutdown and APRS regression tests; remodulated documented payload; types 4, 9, 11, 18, 19 and 27 position extraction tested with synthetic valid, unavailable, truncated and bad-FCS frames, and coordinates verified through IQ-file, DSP, controller and QML with bad-FCS negative controls | Other position-bearing message types, cross-platform and live/recorded-IQ verification; one channel at a time, without automatic tuning |
| ACARS | App menu/session and raw-IQ tap; synthetic engine A/B with valid/CRC-invalid frames and gap/recovery; synthetic IQ-file-to-dialog A/B, including stale-history clearing on disconnect and renewed messages after reconnect; delayed-worker shutdown; attribution in About and tested macOS terms install component | Complete package and cross-platform verification; recorded/RF and other message features |
| VDL Mode 2 | Optional isolated OnAir protocol, experimental IQ/filter/worker and Qt presentation; synthetic IQ A/B against OnAir including adjacent-channel and loss cases; optional app menu and asynchronous startup with simulated-radio shutdown and offscreen UI A/B on macOS ARM64; seven Release-build lifecycle tests cover cancellation, source change, stop/restart, shutdown and backend-close retry; Release IQ-file playback delivered 192 consecutive reference-generated frames with zero reported worker/result queue loss and a responsive UI over 25 seconds; disconnect cleared old results and reconnect recovered four frames; UI stop/restart and retune A/B reject a signal 25 kHz away and recover four frames after retuning; output muted in app tests | Experimental build only, fixed single channel; longer synthetic or physical receiver performance, adaptive timing, packaging, RF and cross-platform verification remain |
| NAVTEX | Experimental app menu/dialog and channel selection; OnAir-derived protocol, fixed-centre audio front end and bounded worker; synthetic bit/audio/worker A/B vs OnAir, polarity, interrupted input, stale generation and silence; synthetic audio at offsets −40 to +40 Hz in both polarities matched OnAir; several deterministic uniform-noise levels and a 1500 Hz interfering tone decoded the expected message and matched OnAir, while the sampled noise-only case emitted none; controlled full queue invalidated a partial message and recovered a complete message matching OnAir; muted USB IQ through DSP with loss/recovery; offscreen IQ-file-to-dialog A/B including disconnect clearing old data and recovery after reconnect/channel reselection, plus held-worker asynchronous shutdown; external separate fldigi-derived audit remains unused | Broader noise/interference and frequency-offset characterization, packaging and notices verification, cross-platform and recorded/RF verification |
| DSC | Experimental app menu and dialog; fixed-layout MF/HF distress, all-ships, individual/automatic, group, area, all-ships distress acknowledgement and relay; bounded asynchronous receiver with 48-to-24 kHz filtering; synthetic audio A/B vs OnAir and corrupted ECC bit-level checks; muted USB IQ loss/recovery and offscreen IQ-file-to-dialog enabled/disabled checks for each supported case; synthetic distress call clears on disconnect and decodes again after reconnect/channel reselection; held-worker asynchronous shutdown | Other DSC layouts and VHF, RF and cross-platform verification; unknown-origin real audio fixture is not redistributed |
| VOR | Experimental app menu/session and raw-IQ tap; synthetic engine A/B at 90 degrees, loss/recovery; IQ-file-to-dialog with radial and ident cleared on disconnect and recovered after reconnect, plus delayed-worker shutdown; 1020 Hz ident displayed in UI, cleared on disable and expires after 30 s without a keying tone; OnAir-generated DXB/no-ident tests at 6–14 WPM, with/without synthetic voice, two noise levels and three seeds passed after coherent-window filtering; ten additional letter/digit identifiers at 7/10/14 WPM passed with/without voice; three 60-second synthetic voice/no-ident runs produced no transient IDs; single-character candidates suppressed; stale-generation and asynchronous-worker tests | The fixed threshold may suppress weak ident signals; broader interference and false-positive validation, recorded/RF and cross-platform verification remain; integer rates 96 ksample/s–2.048 Msample/s; not for navigation |
| SELCAL | App menu/session and AM/SSB channel selection; synthetic muted AM IQ file-to-dialog A/B, including disconnect clearing the old code and reconnect with channel reselection; synthetic USB/LSB, loss/recovery and delayed-worker shutdown; worker tests split a call at a gap, a channel change and a forced full queue, reject the mixed halves and recover on the next complete call; bounded worker and noise controls; same-audio OnAir comparison across four calls and three deterministic seeds, including 72 too-short and 72 overlapping-pair negatives rejected by both; expanded 900-case diagnostic through ±5 Hz recovered complete calls while rejecting tested incomplete, noise-only, single-tone and three-tone samples; eight synthetic voice-like harmonic/noise controls produced no calls | The more permissive spectral threshold needs broader false-positive testing with real speech, recorded/RF validation and cross-platform tests |

AIS type 21 aid-to-navigation coordinates are also supported for 272–360-bit
reports. Synthetic GMSK tests cover normal and extended lengths, unavailable
coordinates, truncation and invalid FCS; a separate IQ-file-to-QML test checks
the displayed coordinates and rejects corrupt frames with audio muted. This is
synthetic macOS ARM64 evidence, not a live maritime receiver test.

Additional synthetic macOS UI/session checks cover: AIS status and stale-data
clearing on invalid input and channel changes; ADS-B position and altitude
clearing on disconnect, and CPR generation separation on retune; APRS field
clearing on disconnect; POCSAG and ACARS out-of-band rejection and recovery;
VOR frequency entry and reopen; and DSC, SELCAL, VOR and NAVTEX display
clearing on disconnect. These cases do not establish live RF reliability.

POCSAG's 512 and 1200 baud options have additionally passed the synthetic
192 ksample/s IQ-file-to-dialog A/B, including disabled reception, zero dropped
blocks during the measured interval, disconnect and reconnect. This is macOS
ARM64 evidence and does not establish live RF or cross-platform performance.
At 200 ksample/s, synthetic IQ-file-to-dialog tests also passed at 512 and
1200 baud, comparing disabled and enabled reception with zero reported worker
block drops during the check; these tests do not establish RF performance.

The VOR IQ tap has now also passed the same synthetic enabled/disabled,
carrier-only and discontinuity checks at every accepted input rate from 96 to
384 ksample/s in 48 ksample/s steps, at three synthetic radials. This tests the
tap and worker on macOS ARM64, not live RF, navigation accuracy or other OSes.
At 192 ksample/s a synthetic VOR shifted 30 kHz away from the selected
channel yielded no valid radial; the selected-channel signal then recovered.
This single separation and level does not establish general selectivity.

VDL2 was configured and built from a fresh Release build directory on macOS
ARM64 using the packaged synthetic fixture. All 17 CI-selected lifecycle and
QML/IQ tests passed with audio muted, including sustained and retuning tests.
The newly configured Linux and Windows CI jobs have not yet run.

The optional VDL2 CI selection now also builds and runs the six protocol ABI,
frame, burst, message-model, shutdown and lifecycle test executables. The full
23-test selection passed locally on macOS ARM64 in Release with audio muted;
the changed remote CI configuration has not yet run on any platform.
After adding connected-but-incompatible IQ source checks in the manager and
interface, the full expanded 25-test selection also passed locally under the
same conditions. Remote CI and physical receiver tests remain outstanding.
The local release-workflow changes now request VDL2 in each distribution and
install a matching Rust target on Windows. They have not run remotely; no
published package or cross-platform compatibility is implied by that change.
An independent fresh macOS ARM64 Release build with VDL2 disabled also
compiled and completed an offscreen muted startup/shutdown smoke test. This
does not validate the interactive UI or other operating systems.
An A/B inspection of the two macOS application binaries found Vdl2AppManager
symbols in the enabled build and none in the disabled build. The application
entry point supplies a null manager in the disabled configuration; the menu
test uses an injected null manager, rather than driving the disabled binary.

In particular, a passing synthetic A/B test does not demonstrate real-world RF
reception, full protocol coverage or operation on an untested platform.

The standalone ACARS test previously compiled with assertions disabled in
Release. Its build now explicitly keeps them enabled and rejects configurations
that disable them. The corrected Release test passed locally on macOS ARM64;
earlier Release passes of that particular test were not evidence of its checks.

The NAVTEX audio A/B against OnAir was repeated with four independent,
deterministic noise sequences for each of six noise/interference cases. All
20 signal-present cases decoded the expected text in both implementations;
neither emitted a message in the four noise-plus-interference-only cases. This
is synthetic audio evidence on macOS ARM64, not a live-RF sensitivity claim.
The muted macOS Qt Quick suite also explicitly discovered and passed the APRS,
NAVTEX and VDL2 literal-text UI cases; 178 component checks passed in that run.

APRS conservatively suppresses decoder input already buffered when an IQ-loss
notification is handled, while the main audio path continues. A backend that
reports a loss only after affected samples have already been processed cannot
retroactively invalidate a result already displayed: exact protection requires
loss markers ordered with the IQ data from the backend itself.

VOR now accepts 48 ksample/s multiples through 960 ksample/s. Synthetic tap A/B covers 480, 768 and 960 ksample/s at three radials; DspEngine A/B covers 960 ksample/s and recovery from a gap. A Release tap-only measurement on this Mac processed 3 s of synthetic 960 ksample/s IQ in 62 ms; it is not a full-app or live-RF performance measurement.

A Release 960 ksample/s synthetic IQ-file-to-dialog run on macOS ARM64 also
decoded radial and identifier, stayed responsive, recorded zero VOR worker
block drops and recovered after disconnect/reconnect. It is not a hardware or
cross-platform test, nor a long-duration performance guarantee.

VOR fractional-ratio resampling now accepts integer input rates from 96 to 960 ksample/s. Synthetic tap tests cover 200, 250 and 500 ksample/s at three radials; 200 ksample/s also passed the muted IQ-file-to-dialog A/B including source reconnection and zero reported worker drops on macOS ARM64. This is not RF or cross-platform evidence.
At 200 and 500 ksample/s the tap also rejected the tested 30 kHz-shifted
synthetic signal, recovered the selected signal, and rejected stale input after
a generation change. This does not establish rejection for every interference.
At 200 ksample/s a synthetic non-finite IQ sample invalidated the partial VOR
result; a following valid signal recovered. The current input block after the
bad sample is intentionally discarded.
The same non-finite-sample invalidation and subsequent VOR recovery were also
tested through DspEngine at 200 ksample/s, with muted synthetic input; this does
not establish how other DSP branches handle the same malformed sample.

AIS also passed synthetic IQ-file-to-dialog A/B at 200 ksample/s and the
192 ksample/s baseline, including bad-FCS rejection, two-channel selection,
disconnect and reconnect on macOS ARM64. The 200 ksample/s stimulus is a
resampled synthetic waveform, not an independent recording or RF evidence.
The symmetric AIS FIR is compared sample by sample against the previous
convolution at 192, 200, 960 and 2400 ksample/s. A muted synthetic 960
ksample/s IQ-file-to-dialog test also passed on macOS ARM64 with no reported
AIS worker block drops during the check; RF performance remains unproven.
In a separate 15-second sustained synthetic 960 ksample/s run on macOS ARM64,
the AIS frame count advanced from 1 to 199 with zero additional worker block
drops and a responsive UI. This short single-host test is not RF, cross-platform
or long-duration proof.

The experimental DSC dialog now shows the decoded category for supported
formats. Synthetic IQ-to-dialog cases verified distress, safety, routine,
urgency and ship's business, including clearing after source disconnect. This
is not safety equipment or live RF evidence.
The DSC dialog also exposes the decoded end-of-sequence symbol as request for
acknowledgement, acknowledgement or other end of call. All three were tested
with synthetic individual-call IQ through the UI on macOS ARM64; this does not
mean the application transmits an acknowledgement.
The acknowledgement-request case also clears the displayed MMSI, category
and sequence on disconnect and restores them on reconnect in the synthetic UI test.
The displayed format, category, sequence and MMSI placeholders were also
checked through disconnect and reconnect for synthetic distress and individual
acknowledgement-request calls on macOS ARM64, with audio muted.
The experimental DSC decoder additionally handles 27-symbol addressed
distress acknowledgements and relays, including an area-addressed relay. A
bitstream A/B against OnAir covered addressed acknowledgement, individual
relay and area relay, corrupted ECC and interrupted-frame recovery; synthetic
IQ-to-QML checks covered one addressed acknowledgement and one area relay on
macOS ARM64. The earlier distress and individual synthetic IQ-to-QML cases
were rerun successfully after extending the parser. The ordinary project test
also checks rejection of an addressed call with valid symbols but a wrong ECC,
rejection of an incomplete call and recovery after reset, without requiring an
external OnAir checkout. The remaining
DSC formats, VHF, real RF and other platforms are not established by these tests.

### Local macOS package smoke (13 November 2026)

A local Apple Silicon install was processed with macdeployqt, the configured
SoapySDR and Hamlib bundle scripts, and an ad-hoc signature. Deep strict
signature verification passed. The installed app started with the Cocoa plugin,
`DSDR_TEST_SILENT_AUDIO=1`, isolated test settings and the demo backend; the
automatic close completed with `test: shutdown fences passed`. The main
executable's direct dependencies contained no `/opt/homebrew` or `/usr/local`
paths. The first offscreen attempt failed because this package contains only
the Cocoa platform plugin; the Cocoa smoke passed. This is a local startup and
shutdown smoke, not a decoder, complete dependency-closure, RF or distribution
verification. The local staging configuration included the RTL-SDR Soapy module;
it does not establish availability of other radio plugins. No Linux, Windows or
Intel macOS package was tested by this check.

An additional `otool` scan of 201 staged Mach-O objects found no load
dependencies under `/opt/homebrew`, `/usr/local` or `/Volumes/Projects` after
excluding each library's own install ID. This checks recorded load paths, not
all dynamically constructed paths or the presence of every optional plugin.
In a second muted Cocoa startup, dyld logged 1,250 loaded-image lines; none
used the local Homebrew, `/usr/local` or Projects paths outside the staged app.
Only the modules exercised by this demo startup are covered.

### ACARS external recorded-audio A/B (13 November 2026)

The four-channel `test.wav` published by jiskra/acarsdec (SHA-256
`752acabede3f96e8f137567e8218fe3cd9aab61130b9198d0cbce6cf2cf53b80`)
was decoded locally by a separately compiled acarsdec with libsndfile: seven
messages, distributed 2/2/2/1 across its four channels. Its command returned
255 on file completion despite printing those messages. Each 12.5 kHz channel
was then independently linearly interpolated and remodulated as an artificial
1 MHz AM IQ carrier, fed to `AcarsChannelRx`, and yielded seven valid blocks
with matching registrations, labels and message content across the same
channels. No audio was played. This compares the core receiver, not the GUI,
radio hardware, a native IQ recording or other signal conditions. The external
recording and temporary probe live under `build/tmp/real-acars` and are not
redistributed; its off-air provenance has not been independently established.
`tests/standalone/acars/test_recorded.cpp` makes the core comparison repeatable
with that external file and libsndfile: it asserts the seven reference
identifiers, equality at two chunk sizes and no messages for four silent
controls. On this Mac all four channels passed with assertions enabled. This
test must be supplied the exact file identified by the hash above; it is not a
default CI test and does not replace the independent acarsdec comparison.
The same recorded test also sends each remodulated channel through the actual
asynchronous `AcarsReceiver`: its messages matched the direct decoder, with no
reported dropped blocks, dropped messages or worker errors in this run.
An optional `recordedSourceToDialog` integration case also passed on macOS
ARM64 with the first recorded channel remodulated into a 1 MHz IQ WAV: the
real IQ-file backend, DSP, controller and offscreen QML dialog displayed the
reference `.F-GTAE` address and `#DFB00000` text. The existing synthetic UI
A/B test also passed after this change. `DSDR_TEST_SILENT_AUDIO=1` forced
physical output gain to zero. This does not test a live receiver or platforms
other than the host.
To reproduce the external UI fixture without distributing the source audio:
`python3 scripts/prepare-acars-recording.py /path/to/test.wav /new/path/acars.wav`.
The converter verifies the source hash and refuses existing output files. The
new fixture passed the same muted UI test; wrong input and overwrite attempts
were rejected in local negative checks.

### NAVTEX external over-the-air audio probe (13 November 2026)

The PhosphorTEX public WebSDR 518 kHz five-minute recording (source:
`https://fractaldoctor.github.io/PhosphorTEX/tests.html`, 8 kHz mono PCM16,
SHA-256 `a0b59dc6425625e5930bd768a56fc2c47e8863ee4139724966e92276a9ddd49d`)
was resampled offline to 24 kHz and fed to `NavtexAudioDecoder`; no audio was
played. On macOS ARM64 it emitted two complete messages, SA97 and SA51. SA97
contains the same identifiable NCC-Hamburg warning described by the source,
but the decoded body includes extraneous characters. The source reports five
messages for its own decoder; this is an external published reference, not an
independent decoder executed locally. Two versus five is a concrete gap, not a
pass or a proven exact sensitivity comparison. The fixture is external and is
not redistributed. The core audio probe bypasses the application's IQ and UI
path; further A/B diagnosis is required before claiming reliable NAVTEX RX.

The locally compiled, separate `fventuri/navtex` decoder (fldigi-derived)
was then run on the same recording resampled to 24 kHz PCM16, centred at
1000 Hz. It produced five framed headers: SA97, SA77, SA56, SA51 and SE89.
The current Decodium audio decoder emitted only SA97 and SA51, with extra
characters in both bodies. This local A/B establishes a reproducible reception
gap on this recording; it does not isolate whether the cause is the slicer,
clock recovery, framing or FEC. Both runs were offline and silent.
An additional local same-float-audio A/B ran the unmodified OnAir FSK slicer
into both OnAir and Decodium protocol decoders. Both emitted only SA97 and
SA51, the same headers as the Decodium audio decoder. This excludes a simple
regression from the port on this sample but does not isolate a shared slicer or
protocol weakness; the independent fldigi-derived decoder remains the five-
header comparison. No audio device was used.
An offline clock-loop gain sweep (0.05, 0.15, 0.35 baseline, 0.65 and 0.9)
did not yield a safe single adjustment: 0.65 recovered SA56 but lost SA51,
and 0.9 emitted no messages. These are diagnostic temporary builds, not
production changes; no false-positive or broader reception claim follows.
Relaxing the experimental phasing-lock control-word threshold from 12 to 11,
10 or 8 in separate temporary protocol builds still produced only SA97 and
SA51 on the same recording. The result does not prove that locking is correct,
but does not support adopting that threshold change alone.

An instrumented *temporary copy* of the independent fldigi-derived reference
captured its 32,345 timing-recovered bit decisions for the same audio. Feeding
their hard signs to both polarities of the Decodium protocol decoder produced
only SA97 and SA51, whereas the original reference produced all five headers.
This narrows the discrepancy to protocol/framing/FEC processing or the loss of
soft-confidence information in this hard-sign experiment; it does not rule
out additional demodulator limitations. Production code was not changed.

The independent decoder's raw bit accumulator values were also retained and
scaled to preserve relative confidence instead of reducing them to hard signs.
The Decodium protocol still emitted only SA97 and SA51. Diagnostic character
output is readable for those messages but becomes garbled between them,
pointing toward framing/parity/FEC recovery. This does not establish a unique
root cause; no production change was made from these probes.

A targeted fix now defers reinitialising each protocol decoder until the bit
following a complete NNNN message, so the next phasing sequence obtains fresh
word alignment and DX/RX parity. In the external recording test, the original
implementation decoded two of five expected headers while the changed code
recovered SA97, SA77, SA56, SA51 and SE89 at both 137- and 4096-sample
chunk sizes; silence emitted no message. The independent fldigi-derived output
agreed on the five headers. Existing synthetic protocol, audio and worker A/B
suites passed, including their noise/invalid and discontinuity controls. The
optional external fixture test is `tests/standalone/navtex/recording_ab.cpp`;
its 24 kHz float input derives from the hashed external recording above. This
is one recorded audio example and not live RF or cross-platform validation.
After rebuilding with the fix, the muted macOS ARM64 `tst_navtex_engine` and
`tst_navtex_session` integration tests also passed sequentially (2/2). These
exercise synthetic input, the app session and QML, including shutdown and
recovery; they do not replay the external recording through the UI.

A synthetic protocol check now covers consecutive independently phased
broadcasts in both polarities and two message lengths. Both the fixed and
pre-fix implementations pass those clean vectors, so this check guards basic
continuity but does not independently reproduce the recorded-signal defect;
the external recording remains the demonstrated failing-before/passing-after
regression case.

A second public WebSDR recording, `navtex_518khz_8min.wav` from the same
PhosphorTEX page (SHA-256
`f305313b15f84b550d23a19301c6320e54399dbab3549f7b798f95d56f38cc06`),
was compared offline after 24 kHz resampling. The independent fldigi-derived
reference output nine headers (PA19, PA40, PA42, PA43, PA49, PA54, PA56,
PA57, PJ77); both pre-fix and fixed Decodium emitted the same eight, missing
PA40. The fixed decoder's PA56 body was visibly garbled even though its error
counter was zero. Thus the fix did not regress header coverage on this sample,
but NAVTEX remains incomplete: missing-frame recovery and error/confidence
reporting need improvement. No audio was played; this is not live RF or UI
coverage.

The NAVTEX dialog now calls the counter “unrecognised characters” and states
that zero does not guarantee error-free text. This reflects the current
implementation, which counts explicit `*` characters and cannot detect all
incorrect valid characters. The focused offscreen QML test verifies the new
labels and literal text rendering on macOS ARM64 with silent audio. It does not
measure or improve decoding accuracy; PA40 and the garbled PA56 remain open.

For the eight-minute recording, feeding the independent reference's captured
soft timing decisions to the fixed Decodium protocol recovered all nine
headers, including PA40. Thus on this particular input the missing PA40 is
introduced before the protocol decoder by the current audio front end/timing
path. Four isolated smoothing-coefficient trials (0.03, 0.06, 0.25, 0.5 versus
0.12 baseline) each still yielded the same eight headers; none was adopted.
This is a differential experiment, not a proof of one exact DSP defect.

After the phasing-reset fix, a new timing-loop sweep found that gain 0.15
recovered all five headers in the first recording and all nine in the second;
it also restored the PA56 `NETHERLANDS COASTGUARD` and `FOGHORN INOPERATIVE`
text. The previous gain 0.35 produced eight headers and failed the PA56 body
assertion in the same recorded test. Gain 0.15 is now used in the audio
front end. Both recordings passed at two chunk sizes with silence controls,
and the synthetic OnAir A/B audio suite passed its offsets, both polarities,
noise and interference cases. These two captures do not establish general RF
sensitivity or error-free text, and other platforms remain unverified.

With the 0.15 timing gain included, the synthetic asynchronous worker A/B
passed again (including stale-generation and full-queue recovery), and the
macOS ARM64 session/engine integration tests passed sequentially (2/2) with
silent audio. These integration cases use synthetic signals, not the external
recordings.

With the timing and phasing fixes, the optional `recordedIqToDialog` test
passed on macOS ARM64 in 64 seconds with forced silent output: a 70-second
portion of the five-minute WebSDR audio was resampled to 192 kHz and placed in
the real component of an artificial IQ WAV (Q=0), then replayed through the
actual IQ-file backend, USB DSP, asynchronous receiver, controller and QML
NAVTEX dialog. The test observed SA97 and `NCC-HAMBURG`. This verifies one
message through that software path; it is not native IQ, an RF test, or a
full five-message UI replay. The external source and generated IQ remain in
`build/tmp/real-navtex`, not in the repository.

`python3 scripts/prepare-navtex-recording.py /path/to/navtex_518khz_5min.wav
/new/path/navtex.wav` reproduces the IQ fixture for the optional dialog test,
using ffmpeg without audio playback. It checks the source hash and refuses to
overwrite existing WAV or metadata. On macOS ARM64 its generated WAV matched
the previously successful UI fixture byte for byte; wrong input and overwrite
negative checks passed. The capture itself remains external.

The two optional recorded NAVTEX audio tests also passed under AddressSanitizer
and UndefinedBehaviorSanitizer on macOS ARM64, at both chunk sizes with
silence controls. LeakSanitizer is not supported by this host's ASan runtime;
the first invocation reported that limitation and was rerun with leak
detection disabled. This is not a leak test or a cross-platform result.

An offline normalized-whitespace text comparison against the local independent
reference found that recovering all headers does not mean byte-perfect bodies:
one of five bodies matched exactly on the first recording and eight of nine
on the second. The remaining bodies had short inserted characters or one
substitution; sequence-similarity scores on this specific material were at
least 0.9939, but are not a measured on-air error rate. Reference output is
itself a decoder output, not a verified human transcript. This reinforces the
UI warning that zero unrecognised characters does not ensure correct text.

A localized comparison of the remaining recorded NAVTEX discrepancies found
short insertions near line transitions in SA97, SA77, SA51 and SE89, and a
single punctuation substitution in PA42 versus the independent decoder.
Another gain of 0.05 had the same exact-match counts as 0.15; gain 0.5 was
worse on the second recording (seven headers and no exact bodies). These are
observations against a decoder reference, not proof of transmitted text; no
heuristic character deletion or additional tuning was applied.

An isolated diagnostic build counted conflicting valid FEC copies per decoder
polarity only while processing each body. On these two captures, all bodies
that differed from the independent output had at least one conflict; PA54 also
had one conflict despite matching. This suggests a useful warning, not a
verified error count: the sample is small and an undetected matching error in
both copies is possible. No production quality metric was added. An initial
experiment mixed class definitions and crashed; the corrected diagnostic
rebuilt both decoder and audio front end against the same temporary header and
completed. Production binaries were not involved.

In an isolated diagnostic build, clean synthetic messages produced zero
valid-copy disagreements in both polarities. A controlled two-bit swap in one
FEC copy produced one disagreement while the recovered text remained correct.
This confirms that a disagreement is not itself an error; it does not establish
whether a zero count is a reliable quality indicator on real signals. The
instrumentation is experimental and remains outside production.

### POCSAG external audio checks (13 November 2026)

The public `rrruark/POCSAG-Decoder` sample `pager_addr4.wav` (SHA-256
`cb4c19e17fb8a58f57d09133fc2d7180b4b275fbf63171339f17f1823672cfbd`)
was read silently at 48 kHz by the standalone POCSAG audio decoder. At 1200
baud it yielded the sample's documented address and complete test text; at
512 and 2400 baud it emitted nothing. Seven other supplied address samples
also produced one message each at 1200 and none at the two other rates; their
full texts were not independently verified. This is audio-core evidence on
externally supplied examples, not independent execution of the reference,
GUI/IQ-path validation, verified off-air provenance or live radio coverage.
The external files are kept only in `build/tmp/pocsag-reference`.

`tests/standalone/pocsag/test_recorded.cpp` provides an optional reproducible
core test using the hash-identified external `pager_addr4.wav` and libsndfile.
It verifies the documented address and message with two input chunk sizes and
rejects silence and both other supported baud rates. It passed on macOS ARM64;
it is not included in the default CI because the external recording is not
redistributed. The test opens no audio device.

The optional `recordedIqToDialog` POCSAG integration case passed on macOS ARM64
with silent audio: the external sample was interpolated and frequency-modulated
into a repeated artificial 192 kHz IQ recording, then passed through the real
IQ-file backend, DSP, worker, controller and QML dialog. The documented address
and beginning of the documented text were verified in the model and rendered
UI. This does not establish native-IQ, live RF or cross-platform performance;
the generated fixture is kept under `build/tmp/real-pocsag`.

`python3 scripts/prepare-pocsag-recording.py /path/to/pager_addr4.wav
/new/path/pocsag.wav` reproduces the artificial IQ fixture from the external
hash-verified sample without overwriting existing files. The generated file
matched the original fixture byte for byte and passed the muted QML integration
test again. Wrong-source and existing-output negative checks passed locally.

### APRS external audio A/B (13 November 2026)

The `AFSK_1200_baud.wav` fixture distributed by `ktauchathuranga/aprs-rx`
(SHA-256 `afd53b2c01227778aaca404cb8ba317302bf04b172e76a33359f15a2c13d56ab`)
was decoded silently by both locally compiled `aprs-rx` and Decodium's AFSK
core at 44.1 kHz. Both decoded one frame; AX.25 address and payload agreed.
`tests/standalone/aprs/test_recorded.cpp` verifies the complete expected frame
at 137- and 4096-sample chunk sizes and rejects silence; both passed on macOS
ARM64. The audio file remains external in `build/tmp/aprs-rx-reference`; its
off-air origin is not established. This is not a live radio or GUI-path test.

The optional APRS `recordedIqToDialog` integration case passed on macOS ARM64
with audio forced silent. The external AFSK audio was resampled and
frequency-modulated into a repeated artificial 192 kHz IQ WAV; the IQ-file
backend, NFM DSP, worker, controller and QML dialog recovered the reference
source, comment and coordinates. It verifies one externally supplied frame,
not live RF, native IQ, other operating systems or broad signal conditions.

`python3 scripts/prepare-aprs-recording.py /path/to/AFSK_1200_baud.wav
/new/path/aprs.wav` reproduces the optional IQ-to-dialog fixture using ffmpeg
and the documented hash. The generated IQ matched the earlier successful
fixture byte for byte and passed the muted macOS QML integration case again;
wrong-source and no-overwrite negative checks passed. The external recording
is not redistributed.

The optional external APRS and POCSAG audio-core regression tests also passed
on macOS ARM64 under AddressSanitizer and UndefinedBehaviorSanitizer, including
their two chunk sizes and silence/wrong-baud controls. Leak detection was
disabled because it is unsupported by this host's sanitizer runtime; this is
not leak verification, real-RF testing or evidence for other platforms.

### AIS external discriminator-audio probe (13 November 2026)

The public `long-beach-160-messages.wav` supplied by `tpainter/pyAIS`
(SHA-256 `76db3dce818e4ec7be8c11b9d8baf93b80c03b94cfccfad52707d1098461d331`)
is mono 48 kHz discriminator audio, not IQ. Offline zero-order upsampling to
96 kHz and artificial 2400 Hz-deviation FM remodulation fed both the Decodium
AIS core and a separately compiled `ibelinp/libaisdemod` single-channel
receiver. Decodium returned 104 CRC-valid frames; the reference returned 26
frames, all of which matched Decodium payloads as a multiset. Inspection of
the reference's frame parser shows that it emits only byte-aligned payloads;
a rerun explicitly asserted this for every callback. Thus no reference frames
were omitted by the original comparison. It did not independently validate
the 78 other Decodium frames. Different filtering and timing make these counts non-equivalent. No
audio was played, and there was no live RF, native IQ or UI validation.
The external fixture is kept only in `build/tmp/ais-py-reference`.

The AIS A/B diagnostic was extended to compare unique payloads as well as
counts: all 104 Decodium payloads on the artificially remodulated sample were
distinct; all 26 reference payloads were distinct and included among them.
Thus the count difference is not explained by identical payload duplicates in
this run. The remaining 78 are still unverified by that reference, and CRC
validity alone is not independent ground truth. No vessel data was printed.

The AIS remodulation comparison was repeated at artificial deviations of
1200, 2400, 4800 and 9600 Hz. The independent single-channel receiver returned
the same 26 distinct payloads at each setting, all included in Decodium's
103–104 distinct CRC-valid results. This rules out that deviation choice alone
explains the count gap in this setup; it does not validate the unmatched frames
or establish performance on native IQ.

An exploratory check of several additional pyAIS `.raw` files interpreted as
interleaved unsigned 8-bit IQ (as suggested by comments in its script) yielded
zero frames in both Decodium and the independent receiver after 2x sample
repetition. Byte inspection suggests this interpretation may be wrong (the
raw files lack self-describing format metadata), so this is inconclusive and
is not counted as a receiver failure, native-IQ validation or an A/B pass.

The previously inconclusive 2018 raw AIS sample (SHA-256
`9a37012be1ccb53bf5bfecc7ff6cff5077fdc2e0027febed091dbedf2c5fbd62`)
was reinterpreted as 48 kHz signed 16-bit mono discriminator audio, then
artificially FM-remodulated and duplicated to 96 kHz for the independent
receiver. Decodium and libaisdemod each emitted five distinct CRC-valid
payloads, and all five matched. This is evidence for that interpretation, not
authoritative metadata or proof that the raw file is native IQ. No audio was
played; the file remains external and only aggregate counts were logged.

`tests/standalone/ais/test_recorded_ab.cpp` makes the 26-frame subset A/B
repeatable with externally supplied libaisdemod, libsndfile and the hash-named
Long Beach recording. It checks every reference payload as a multiset, prints
counts only, and passed locally; missing arguments and a wrong-rate fixture
were rejected. It does not prove the remaining 78 frames, and is not a default
CI test because the external reference and recording are not bundled.

The optional AIS `recordedIqToDialog` integration case passed on macOS ARM64
with silent output. A 15-second portion of the external Long Beach discriminator
audio was remodulated to artificial 192 kHz IQ on channel A and passed through
the IQ-file backend, DSP, worker, controller and QML panel; the test verified
that a frame appeared and its MMSI was rendered. It did not compare that
specific displayed frame with the independent receiver, nor validate native IQ,
live RF or another platform. No vessel identifier is printed by the test.

The optional AIS recorded UI test can also accept a local file of identifiers
emitted by the independent libaisdemod receiver on the identical artificial IQ
fixture. On macOS ARM64 at least one identifier received in the controller and
rendered in QML matched that reference. A deliberately nonmatching list made the
test fail as expected. These files remain in `build/tmp/real-ais`, and neither
identifiers nor payloads are printed. This verifies a shared identifier, not
full payload equality at the UI, native IQ, live RF or other operating systems.

`python3 scripts/prepare-ais-recording.py /path/to/long-beach-160-messages.wav
/new/path/ais.wav` regenerates the 15-second artificial IQ UI fixture with
ffmpeg, verifies the documented hash and refuses existing outputs. The generated
WAV matched the prior fixture byte for byte and passed the muted UI/reference-ID
check; wrong-source and no-overwrite controls passed locally. The input and
identifier list remain external. This does not make the sample native IQ.

The optional AIS external A/B harness passed on macOS ARM64 with the Decodium
core and harness compiled using AddressSanitizer and UndefinedBehaviorSanitizer:
all 26 reference payloads matched. The prebuilt independent reference library
was not sanitizer-instrumented, and leak detection was disabled because this
host does not support it. The other 78 payloads remain unverified.

After adding the optional external-fixture cases, the normal synthetic APRS,
POCSAG and AIS session integration suites were run sequentially on macOS ARM64
without the external-fixture environment variables: all three CTest targets
passed with silent output. These results cover the existing synthetic cases;
they do not add cross-platform or RF evidence.

### DSC external sample probe (13 November 2026)

The `test.wav` included in `alemassimo/TAOSW.DSC_Decoder` has SHA-256
`3e170987f154a0769fa22ca94c1fe45e6083de9f78c90c24420055edae5fe2e0`;
its duplicate named SDRuno capture has the same hash. A simple stereo-to-mono
resampling to 24 kHz followed by the fixed-1700-Hz DSC audio core produced no
messages. The exact capture convention, content, expected messages and stereo
channel meaning have not been verified, and no independent reference decoder
was run; this result is inconclusive, not a verified decoder failure. No audio
was played and the external file remains in `build/tmp/dsc-reference`.

Separating the two channels of the external DSC candidate and testing each at
24 kHz still yielded zero supported calls. A coarse spectral probe of the
first ten seconds found prominent energy around 555 and 605 Hz, rather than
establishing a 1700 Hz DSC centre. This is not a protocol identification or a
frequency estimate for all transmissions in the recording. Capture convention
and an independent decoded reference remain unverified; no production changes
were made on that basis.

A later diagnostic shifted channel 0 of the external DSC candidate by +1200 Hz
before 24 kHz resampling, placing a decodable signal near the fixed front-end
centre. OnAir's independent decoder reported five ECC-valid format-120 calls;
the Decodium audio core emitted three format-120 calls, and the three source
identifiers matched the reference as a multiset. No identifiers were logged.
The other two reference calls were not recovered. This is a genuine A/B gap on
this shifted sample, but does not establish why they were lost or validate the
original capture's tuning, complete content, UI, live RF or other platforms.

DSC diagnostic on the frequency-shifted external recording: feeding OnAir's sliced bits into the local protocol decoder yielded 4 calls, while OnAir's protocol decoder yielded 5 and the complete local audio path yielded 3. This isolates at least one additional loss after slicing, but does not establish that the two missing calls have the same cause. The diagnostic uses a translated recording and is not a live-RF test; the ten-decoder objective remains unfinished.

The product decoder now accepts up to four erased phasing symbols, but rejects valid contradictory phasing symbols, conflicting information copies and invalid ECC. With the same translated sample the complete local path recovered 4 of the 5 independently reported calls; feeding the reference's sliced bits into the product recovered all 5. `sh tests/standalone/dsc/run_recorded_ab.sh /path/to/test.wav` verifies the external recording hash and repeats this comparison without printing identifiers. Synthetic A/B also covers four erasures, five erasures, contradictory phasing and random noise. One call remains missing in the complete local audio path; these bounded tests do not establish a general false-positive rate or live-RF performance.

With gentler clock correction, the complete product audio path now matches all 5 of the reference's 5 calls in this translated recording. `run_recorded_ab.sh` checks this result. `run_product_audio_ab.sh` tests the actual product DSP against OnAir with synthetic rate offsets, frequency offsets, noise, inversion, chunk sizes, reset and silence. These tests are limited samples, and the clock adjustment was selected using this recording; independent recordings and live RF remain necessary to assess generalisation.

An optional `tst_dsc_session recordedIqToDialog` test also passed on macOS with `DSDR_TEST_SILENT_AUDIO=1`, comparing a value displayed by the real QML dialog against the independent decoder's identifiers. Its input was generated by `scripts/prepare-dsc-recording.py` from the hash-checked recording as frequency-shifted, artificial IQ. This proves at least one reference call reached the dialog on this path, not all five, native IQ, live RF, other platforms or a general accuracy rate. The test uses an isolated settings directory, a muted channel and the test audio gate; Qt still opened the configured audio device.

The same dialog test was rerun with five fabricated, nonmatching identifiers and failed at the intended identity assertion; rerunning with the reference identifiers passed. This negative control confirms that an arbitrary displayed value is insufficient to pass this particular test. It does not extend the scope of the positive evidence above.

The product-audio A/B suite also covers 12 additional synthetic distress messages with different source identifiers, initial phases and independent deterministic noise seeds; rate, carrier offset and chunk size vary across the cases. Both the local product and OnAir decoded the expected identities in all cases. The expanded suite passed with AddressSanitizer and UndefinedBehaviorSanitizer on macOS ARM64 (leak detection disabled); this is bounded synthetic evidence, not RF or cross-platform validation.

The product-format A/B additionally verifies recovery of four phasing erasures with reversed polarity, and rejects a payload whose two copies were changed consistently while four phasing symbols are erased and the original ECC is retained. This specifically checks that phasing recovery does not bypass the message integrity check in this synthetic case. After the clock adjustment, the muted actual-session IQ-to-dialog regressions for an individual call and an LSB distress call also passed on macOS ARM64; the LSB test additionally disconnects and reconnects the IQ source.

ADS-B type 19 exposes ground speed and ground track for subtypes 1–2, plus vertical rate for subtypes 1–4, for the latest valid frame. A published velocity vector, invalid CRC, absent components, zero velocity, subtype 2 scaling and subtype 3 non-ground-speed handling passed the C++ tests; the QML component test verifies visible values and their removal when unavailable. The displayed values are not a persistent aircraft track. These checks do not establish live RF or full IQ-to-dialog coverage for velocity.

Subsequently, subtypes 3–4 gained indicated/true airspeed and magnetic heading presentation, distinct from ground speed and ground track. Synthetic frame tests cover TAS, IAS, subtype-4 scaling, unavailable speed, heading-valid and heading-unavailable flags; the muted QML component test checks TAS/IAS switching and clearing. Two subtype-3 frames generated by OnAir's encoder with independently computed CRC decoded to the expected IAS/TAS, heading and vertical rate. The TAS frame also passed through synthetic IQ to the QML dialog, and a bad-CRC variant was rejected. An optional independent pyModeS 2.22.0 comparison (`tests/standalone/onair_vertical/velocity_pymodes_ab.py`) checks those two frames and a published ground-velocity frame against the same expected fields as `tst_adsb_message`; pyModeS rounds the ground-speed result to an integer. RF and other-platform verification remain outstanding.

The published type-19 frame was subsequently converted to synthetic 2 Msamples/s IQ and sent through the actual IQ-file source, asynchronous receiver, controller and QML dialog on macOS ARM64 with output muted. The displayed speed, track and vertical rate matched the vector; disconnect cleared them. A second IQ fixture with a corrupted CRC produced no accepted frame. This exercises the application path but is not a live-RF or other-platform test.

On the external `modes1_2msps_cu8.bin` recorded-IQ fixture already used for the demodulator A/B, the product decoded 58 type-19 frames. Product velocity fields agreed with independent pyModeS 2.22.0 for all 58 (speed tolerance below one knot for integer quantisation). This check uses `real_ab`'s optional local binary-frame output plus `recorded_velocity_product.cpp` and `recorded_velocity_pymodes_ab.py`; identifiers are not printed. The fixture is one short recording of one aircraft and does not establish sensitivity, live reception, other operating systems or all velocity subtypes.

Three demodulated VOR recordings from https://github.com/martinber/vor-python-decoder/tree/master/samples (filenames 177deg_long_1, 234deg_long_1, 293deg_long_1; SHA-256 respectively `5dd89788e65eb16c0bd679d4afb56085ca6389c26908d37bea0deaaff1995be5`, `a9741d6e8f31f66891ee9b6db846ae721f95d5f38dae4645bbd7fe55b758345a`, `b07b3cc0ac392fe0d1697756e716c92a9cfe950cc59ac2002046879017a668ad`) exposed an unresolved VOR discrepancy. Mono downmix at native gain produced no valid windows for the first two and about 270 degrees for the third. Using channel 0 with gain 10/3/3 respectively produced approximately 155.5/211.5/270.2 degrees against the repository's approximate map bearings 177/234/293. `tests/standalone/vor/recorded_probe.cpp` uses the product audio decoder and no audio device. These recordings share acquisition and beacon provenance, so a roughly 22-degree common difference cannot yet be attributed to the product, beacon type, acquisition filters or reference calibration. No empirical offset has been added. This is a material unresolved risk: VOR remains experimental and unsuitable for navigation.

On the same amplified audio, an independent OnAir VOR receiver (`recorded_onair_probe.cpp`, with stubbed audio output and a synthetic AM carrier) reported 155.61 and 270.61 degrees for the 177 and 293 samples, close to the product's 155.50 and 270.22. OnAir did not report a valid radial for the 234 sample under these probe conditions. This argues against a simple product-only 22-degree correction for those two samples, but does not establish absolute accuracy: both pipelines share the original demodulated capture, and the sample provider explicitly uses an empirically calibrated offset and map bearings. A VOR radial is magnetic while a map-derived bearing may use true north; historical station declination and the exact recording conditions remain to be verified.

The product VOR validity gate now compares signal components with the window RMS and keeps an absolute floor, rather than requiring fixed component amplitudes. The same three mono recordings at native level now produce valid windows, with unchanged radials. Synthetic audio tests cover gains 0.001, 0.01, 0.1 and 1, rejection of missing branches at each gain, silence and twelve deterministic noise-only seeds; the 36-case OnAir generator comparison also passed. The expanded local test passed ASan/UBSan on macOS ARM64 with leak detection disabled. Relative thresholds are heuristics: these limited negative controls do not establish false-positive rates or navigational integrity, and the absolute-bearing discrepancy remains unresolved.

After this change, the muted actual DSP-engine VOR A/B regression also passed at 192, 200, 960, 2000 and 2048 ksample/s on macOS ARM64 (8 QtTest checks including setup and cleanup). This uses synthetic signals and does not establish sustained real-time hardware performance.

`sh tests/standalone/vor/run_recorded_ab.sh /path/to/samples` checks the three recorded-audio hashes and the product's observed radial ranges without gain adjustment; the local positive cases passed, and an incorrect expected angle and empty input were rejected. Additional synthetic negative tests with voice-like harmonics and a 1020-Hz tone at three gains passed. The expected ranges are regression baselines for the product's measurements, not verified reference bearings; these controls do not establish general interference immunity.

The external Signal Identification Wiki ICAO SELCAL demonstration (`https://www.sigidwiki.com/images/9/93/Icao_selcal.mp3`, SHA-256 `4a9d93f70bb04bc78c67ed0e3f17459b8f600fa1cddab0eabc318748d5898835`) documents four repetitions each of AB-CD, EF-GH, JK-LM and PQ-RS. At 8 kHz with chunks of 257 and 4096 samples, the original product and OnAir both emitted zero calls; a product adjustment for unequal pair amplitudes emits exactly the 16 documented calls for both chunk sizes while OnAir emits zero. The independent-reference disagreement is explicit, not a claim of parity. The muted `run_recorded_ab.sh` reproduces the result, with no sample redistributed. The wider 1260-case synthetic OnAir comparison retained all negative controls, with 24 disagreements confined to complete-call cases; synthetic speech and three generated macOS voices remained negative. The lower threshold is a sensitivity/false-positive tradeoff, so broader real speech/interference and live RF validation remain required.

The documented sample's first 14.5 seconds were also translated to artificial AM IQ by `scripts/prepare-selcal-recording.py`; the muted macOS ARM64 IQ-file-to-dialog test showed AB-CD, cleared it on disconnect and recovered it after reconnect. The original synthetic AM IQ session A/B passed again. This is one example of the four codes through the full app, not a native IQ recording or proof of live-radio and cross-platform operation.

An independent off-air VDL2 sample from `airframesio/xng` (`crates/xng-mode-vdl2/tests/data/vdl2_offair_6s.i16`, SHA-256 `c4e8de5e1d805ace3905107bf27ce1e7130f5f95db321c4ff86a16f08669f7fa`) is 50 ksample/s I/Q, explicitly conjugated by its source. The product's experimental window decoder recovered two CRC-valid AVLC frames, identical across 1024/4096-sample chunks; silence and reversing Q produced none. The independent xng off-air test passed, and a direct comparison of its raw AVLC frames found four unique frames, including both product frames. Thus the product missed two of the four independently decoded frames: this is a material VDL2 gap, not parity. `tests/standalone/vdl2/run_recorded_iq.sh` reproduces the current product baseline; the external sample is CC BY-SA and is not redistributed here. This test bypasses the product frontend, worker and UI and does not establish real-time or live hardware performance.

After adding bounded retries at adjacent sample positions when a preamble candidate fails, the same off-air capture produces three CRC-valid frames; all three match distinct raw frames from the four decoded by independent xng. The recorded-IQ test now requires three frames, chunk invariance and negative silence/inverted-IQ controls. The synthetic OnAir framing, mixed-FCS, asynchronous queue and loss/restart comparisons passed, as did the muted synthetic IQ-file-to-QML application test on macOS ARM64. One independent-reference frame remains missing. This is not full parity or evidence for sustained-load performance after adding retries; raw reference and product frame artifacts were removed after comparison.

After the adjacent-sample retry, the muted 48-repeat synthetic IQ-file-to-QML sustained regression passed on macOS ARM64 in 25.2 seconds; its assertions require 192 decoded messages and zero reported worker/result queue drops. This bounded synthetic run is not a worst-case-noise or live RF throughput guarantee. Exploratory variants lowering the acquisition correlation threshold, relaxing the energy check, and resampling this one recording from 50 to 100 ksample/s each still recovered only three frames; those variants were not adopted. The fourth remains unresolved.

The external 50 ksample/s recording was also resampled to 192 ksample/s and fed through the product's actual channel frontend and window decoder. Three CRC-valid frames were observed; this count-only check is now part of `run_recorded_iq.sh`. It does not compare those frontend payloads with the independent reference and does not exercise the asynchronous worker or UI. Relaxing acquisition gates and increasing the number of candidate attempts experimentally did not recover the fourth frame and was not adopted.

The VDL2 preamble search now interpolates symbol positions and normalises differential correlation by pair magnitudes, with a weak-pair guard inspired by the independent `airframesio/xng` receiver (MIT/Apache-2.0). The native 50 ksample/s recording now yields all four reference frames byte-for-byte, consistently across 1024/4096-sample blocks; silence, deterministic noise and inverted IQ yield none. After resampling to 192 ksample/s and passing through the product frontend, five distinct frames are produced: four match xng's four, and the additional one has an independently checked PPP FCS but was not independently decoded by xng. Synthetic OnAir comparisons, corrupted-frame rejection and async gap/reset tests passed. The muted 48-repeat application IQ-to-QML regression still passed in 25.1 s with 192 messages and zero reported queue drops. These bounded samples do not prove real-time live RF, other platforms, or that the extra frame is a valid real-world message.

The 192 ksample/s resampled off-air fixture was subsequently played through the full muted macOS ARM64 app path: five messages reached the QML list, GUI heartbeat continued, and the counters reported no dropped blocks/results or worker errors. A second run with conjugated IQ left the list empty and passed the same responsiveness and queue checks. The test compares counts, not the displayed contents byte-for-byte; it does not independently validate the fifth message or establish live hardware performance.

An additional independent comparison resampled the same source recording to 100 ksample/s for xng, while the product frontend used the 192 ksample/s version. Both produced exactly five unique AVLC frames and all five matched byte-for-byte, including the additional frame. This supersedes the earlier uncertainty about independent decoding of that frame for this particular recording. The application UI test still checks counts rather than displayed contents, and this single capture does not establish live RF performance or behaviour on other platforms.
