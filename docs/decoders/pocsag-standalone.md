# POCSAG experimental bitstream decoder

This isolated receive-only component is **not connected to the application**. It is optional because nothing in the existing build or UI references it. It accepts already demodulated and clock-recovered hard bits, one byte per bit in transmission order. It performs exact sync acquisition in either polarity, batch/frame addressing, extended BCH(31,21) and even-parity validation with correction of up to two bit errors per 32-bit word, and bit-preserving 7-bit alphanumeric extraction. The caller must select the rate and perform FSK demodulation and clock recovery externally. The rate itself is immaterial once bits have been sliced. No transmitter, numeric decoding, encoding autodetection, or CRC is provided: POCSAG itself has no message CRC. Alpha output can include NUL or control characters and padding; do not interpret the function number as proof of the payload format.

Call `reset()` on retuning, data gaps, or rate changes. `process()` and its callback are synchronous and not thread-safe: use a worker thread and marshal results to the UI. The caller owns the callback context. A page is emitted only on a following valid address or idle word; a trailing or interrupted page is discarded on reset or sync loss. Messages exceeding 7160 payload bits and pages containing an uncorrectable word are discarded. A lost address cannot be reconstructed. Even BCH-valid or corrected data can contain undetected errors or miscorrections beyond the correction capability; corrected bit counts are not a guarantee of authenticity or message integrity. Exact sync and hard decisions sacrifice sensitivity; no soft decisions, sync error tolerance, timing recovery, or noise-adaptive receiver is included.

## Verification

From the repository root, with all output under the project volume:

```sh
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr/build c++ -std=c++17 -Wall -Wextra -Werror -Isrc src/dsp/PocsagDecoder.cpp tests/standalone/pocsag/test.cpp -o build/pocsag-standalone-test
DSDR_TEST_SILENT_AUDIO=1 ./build/pocsag-standalone-test
```

The independent test encoder creates a synthetic, protocol-shaped preamble and batch, a known RIC and text, normal and inverted bit polarity, arbitrary input chunking, and 0/1/2/3-bit impairment comparisons. It checks all 496 two-bit corruption patterns for one codeword, known sync/idle BCH checks, truncated input, and deterministic noise. These are synthetic bitstream tests, not recorded RF, independently captured traffic, a separate implementation comparison, or end-to-end IQ/audio tests. The three-bit rejection is one selected pattern, not a general guarantee. The fixture text is test data, not a claimed received message. There is no evidence of receiver sensitivity, application UI responsiveness, operation with physical hardware, or parity with OnAir.

Reference consulted as documentation only: `/Volumes/Projects/OnAir-reference-3.9.0/docs/PAGER_CCIR.md` and `docs/railway.md` (structure and absence of message CRC); no OnAir source code copied. The POCSAG format is also described by NXP's [PCF5001 product specification](https://kazojc.com/elementy_czynne/IC/PCF5001.pdf). An independently sourced recording with known permitted ground truth, a bit slicer and integration point, and runtime/UI tests are needed before this can be presented as an application receiver.
