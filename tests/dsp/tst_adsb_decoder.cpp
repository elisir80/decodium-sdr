// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/AdsbDecoder.h"
#include <QTest>
#include <vector>
using namespace dsdr::dsp;

namespace {
using Frame = AdsbDecoder::Frame;
// Publicly documented Mode S extended squitter example, including parity.
const Frame known = {0x8d,0x40,0x62,0x1d,0x58,0xc3,0x82,0xd6,0x90,0xc8,0xac,0x28,0x63,0xa7};
std::vector<Complex> waveform(const Frame &f) {
    std::vector<Complex> s(240, Complex(0.01f, 0));
    for (int i : {0, 2, 7, 9}) s[i] = Complex(0.8f, 0.2f);
    for (std::size_t b = 0; b < 112; ++b)
        s[16 + 2*b + ((f[b/8] >> (7-b%8)) & 1 ? 0 : 1)] = Complex(0.8f, 0.2f);
    return s;
}
void capture(void *p, const Frame &f) { static_cast<std::vector<Frame> *>(p)->push_back(f); }
}
class TestAdsb : public QObject {
    Q_OBJECT
private slots:
    void crc() {
        QVERIFY(AdsbDecoder::validFrame(known));
        auto bad = known; bad[6] ^= 4;
        QVERIFY(!AdsbDecoder::validFrame(bad));
        bad = known; bad[13] ^= 1;
        QVERIFY(!AdsbDecoder::validFrame(bad));
        bad = known; bad[0] = 0x88; // unsupported DF even if parity happened to match
        QVERIFY(!AdsbDecoder::validFrame(bad));
    }
    void streamingAB() {
        std::vector<Frame> got;
        AdsbDecoder d(capture, &got);
        auto good = waveform(known);
        auto badFrame = known; badFrame[6] ^= 4;
        auto bad = waveform(badFrame);
        d.process(bad.data(), bad.size());
        QCOMPARE(got.size(), std::size_t(0));
        d.process(good.data(), 71);
        d.process(good.data()+71, good.size()-71);
        QCOMPARE(got.size(), std::size_t(1));
        QVERIFY(got[0] == known);
        d.process(good.data(), good.size());
        QCOMPARE(got.size(), std::size_t(2));
    }
    void preambleAndReset() {
        std::vector<Frame> got;
        AdsbDecoder d(capture, &got);
        auto good = waveform(known);
        auto bad = good; bad[7] = Complex(0.01f, 0);
        d.process(bad.data(), bad.size());
        QCOMPARE(got.size(), std::size_t(0));
        d.reset();
        d.process(good.data(), 120);
        d.reset();
        d.process(good.data()+120, 120);
        QCOMPARE(got.size(), std::size_t(0));
        d.reset(); d.process(good.data(), good.size());
        QCOMPARE(got.size(), std::size_t(1));
    }
    void marginalAndNoiseAB() {
        std::vector<Frame> got;
        AdsbDecoder d(capture, &got);
        auto impaired = waveform(known);
        // Weaker preamble contrast and incomplete symbol separation.
        for (int i : {1,3,4,5,6,8,10,11,12,13,14,15}) impaired[i] = Complex(0.65f, 0);
        for (std::size_t bit=0; bit<112; ++bit)
            impaired[16+2*bit+((known[bit/8]>>(7-bit%8))&1 ? 1 : 0)] = Complex(0.75f, 0);
        // A/B against the previous preamble criterion on this identical IQ:
        // the old 2:1 threshold rejects the frame, the current decoder below
        // must recover it and still validate the complete CRC.
        const float high = std::norm(impaired[0]);
        const float low = std::norm(impaired[1]);
        QVERIFY(!(high > low * 2.0f));
        d.process(impaired.data(), impaired.size());
        QCOMPARE(got.size(), std::size_t(1));
        QVERIFY(got[0] == known);
        got.clear(); d.reset();
        auto corrupted = known; corrupted[6] ^= 4;
        auto invalid = waveform(corrupted);
        d.process(invalid.data(), invalid.size());
        QVERIFY(got.empty());
        d.reset();
        std::vector<Complex> noise(2000000);
        std::uint32_t rng=0x48ac1234;
        for (auto &sample : noise) {
            rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5;
            const float i=float(rng & 0xffff)/32768.0f-1.0f;
            rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5;
            sample=Complex(i,float(rng & 0xffff)/32768.0f-1.0f);
        }
        d.process(noise.data(), noise.size());
        QVERIFY(got.empty());
    }
};
QTEST_GUILESS_MAIN(TestAdsb)
#include "tst_adsb_decoder.moc"
