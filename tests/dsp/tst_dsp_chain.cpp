// SPDX-License-Identifier: GPL-3.0-or-later
// Catena DSP completa: decimazione, spettro e canale end-to-end (RNF-07).

#include "dsp/ChannelProcessor.h"
#include "dsp/DecimatorChain.h"
#include "dsp/SpectrumAnalyzer.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace dsdr;
using namespace dsdr::dsp;

namespace {

std::vector<Complex> makeTone(double frequencyHz,
                              double sampleRate,
                              std::size_t n,
                              float amplitude = 1.0f)
{
    std::vector<Complex> out(n);
    const double w = kTwoPi * frequencyHz / sampleRate;
    for (std::size_t i = 0; i < n; ++i) {
        const double phase = w * static_cast<double>(i);
        out[i] = Complex(amplitude * static_cast<float>(std::cos(phase)),
                         amplitude * static_cast<float>(std::sin(phase)));
    }
    return out;
}

float rms(const Complex *data, std::size_t n)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        sum += magnitudeSquared(data[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

/// Goertzel: energia di una singola frequenza, senza tirare in ballo una FFT.
double goertzel(const float *audio, std::size_t n, double frequencyHz, double sampleRate)
{
    const double w = kTwoPi * frequencyHz / sampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s0 = audio[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / static_cast<double>(n);
}

} // namespace

class TestDspChain : public QObject
{
    Q_OBJECT

private slots:
    void factorizationPrefersLargeStagesFirst();
    void factorizationHandlesPrimeFactors();
    void decimatorPreservesInBandTone();
    void decimatorRejectsAliasingTone();
    void spectrumPeaksAtExpectedBin();
    void spectrumPlacesDcAtCentre();
    void channelDemodulatesUsbTone();
    void channelRejectsOppositeSideband();
    void channelSurvivesSettingsChangeMidStream();
};

void TestDspChain::factorizationPrefersLargeStagesFirst()
{
    const std::vector<int> f20 = DecimatorChain::factorize(20);
    QCOMPARE(f20, std::vector<int>({5, 4}));

    const std::vector<int> f16 = DecimatorChain::factorize(16);
    QCOMPARE(f16, std::vector<int>({8, 2}));

    QVERIFY(DecimatorChain::factorize(1).empty());
}

void TestDspChain::factorizationHandlesPrimeFactors()
{
    // 26 = 2 × 13: il 13 non è fra i fattori preferiti e resta come stadio unico.
    const std::vector<int> f = DecimatorChain::factorize(26);
    int product = 1;
    for (int v : f)
        product *= v;
    QCOMPARE(product, 26);
}

void TestDspChain::decimatorPreservesInBandTone()
{
    constexpr double kRate = 192000.0;
    DecimatorChain chain;
    QVERIFY(chain.configure(kRate, 4, 8000.0));
    QCOMPARE(chain.outputRate(), 48000.0);

    const std::vector<Complex> input = makeTone(3000.0, kRate, 32768);
    std::vector<Complex> output(chain.maxOutput(input.size()));
    const std::size_t produced = chain.process(input.data(), input.size(), output.data());

    QCOMPARE(produced, input.size() / 4);

    // Ampiezza conservata (scartiamo il transitorio iniziale del filtro).
    const float level = rms(output.data() + 512, produced - 512);
    QVERIFY2(std::abs(level - 1.0f) < 0.05f,
             qPrintable(QStringLiteral("livello dopo decimazione = %1").arg(level)));
}

void TestDspChain::decimatorRejectsAliasingTone()
{
    constexpr double kRate = 192000.0;
    DecimatorChain chain;
    QVERIFY(chain.configure(kRate, 4, 8000.0));

    // 40 kHz è ben oltre la Nyquist di 24 kHz della nuova frequenza: senza il
    // filtro anti-alias ricomparirebbe come segnale fantasma in banda.
    const std::vector<Complex> input = makeTone(40000.0, kRate, 32768);
    std::vector<Complex> output(chain.maxOutput(input.size()));
    const std::size_t produced = chain.process(input.data(), input.size(), output.data());

    const float level = rms(output.data() + 1024, produced - 1024);
    const float attenuationDb = -20.0f * std::log10(std::max(level, 1e-9f));
    QVERIFY2(attenuationDb > 60.0f,
             qPrintable(QStringLiteral("attenuazione alias = %1 dB").arg(attenuationDb)));
}

void TestDspChain::spectrumPeaksAtExpectedBin()
{
    constexpr double kRate = 192000.0;
    constexpr int kFft = 4096;
    constexpr double kTone = 24000.0;

    SpectrumAnalyzer analyzer;
    QVERIFY(analyzer.configure(kFft, kRate));
    analyzer.setAveraging(1.0f); // nessuna media: vogliamo il frame puro

    const std::vector<Complex> input = makeTone(kTone, kRate, kFft * 4);
    QVERIFY(analyzer.push(input.data(), input.size()));

    const std::vector<float> &bins = analyzer.magnitudesDb();
    QCOMPARE(static_cast<int>(bins.size()), kFft);

    const auto peak = std::max_element(bins.begin(), bins.end());
    const int peakIndex = static_cast<int>(std::distance(bins.begin(), peak));

    // Con fftshift il bin 0 è −fs/2, quindi il centro è kFft/2.
    const int expected = kFft / 2 + static_cast<int>(std::lround(kTone / (kRate / kFft)));
    QCOMPARE(peakIndex, expected);
    QVERIFY2(*peak > -3.0f, qPrintable(QStringLiteral("picco = %1 dBFS").arg(*peak)));
}

void TestDspChain::spectrumPlacesDcAtCentre()
{
    constexpr int kFft = 1024;
    SpectrumAnalyzer analyzer;
    QVERIFY(analyzer.configure(kFft, 48000.0));
    analyzer.setAveraging(1.0f);

    std::vector<Complex> dc(kFft * 2, Complex(1.0f, 0.0f));
    QVERIFY(analyzer.push(dc.data(), dc.size()));

    const std::vector<float> &bins = analyzer.magnitudesDb();
    const auto peak = std::max_element(bins.begin(), bins.end());
    QCOMPARE(static_cast<int>(std::distance(bins.begin(), peak)), kFft / 2);
}

void TestDspChain::channelDemodulatesUsbTone()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kAudioTone = 1000.0;

    // Una USB pura: portante a +20 kHz, audio a 1 kHz ⇒ riga a +21 kHz.
    const std::vector<Complex> input =
        makeTone(kChannelOffset + kAudioTone, kDeviceRate, 96000, 0.2f);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Usb;
    settings.filterLowHz = 300;
    settings.filterHighHz = 2700;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 1000);

    // Scartiamo il transitorio dei filtri prima di misurare.
    const std::size_t skip = 2000;
    const double wanted = goertzel(audio.data() + skip, produced - skip, kAudioTone, 48000.0);
    const double other = goertzel(audio.data() + skip, produced - skip, 1800.0, 48000.0);

    QVERIFY2(wanted > 0.02, qPrintable(QStringLiteral("tono demodulato = %1").arg(wanted)));
    QVERIFY2(wanted > other * 50.0,
             qPrintable(QStringLiteral("atteso=%1 spurio=%2").arg(wanted).arg(other)));
}

void TestDspChain::channelRejectsOppositeSideband()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;

    // Segnale sulla banda laterale INFERIORE: in USB non deve passare.
    const std::vector<Complex> input =
        makeTone(kChannelOffset - 1000.0, kDeviceRate, 96000, 0.2f);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Usb;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());

    const std::size_t skip = 2000;
    const double leaked = goertzel(audio.data() + skip, produced - skip, 1000.0, 48000.0);
    QVERIFY2(leaked < 0.002,
             qPrintable(QStringLiteral("banda laterale opposta trapelata = %1").arg(leaked)));
}

void TestDspChain::channelSurvivesSettingsChangeMidStream()
{
    constexpr double kDeviceRate = 192000.0;
    const std::vector<Complex> input = makeTone(21000.0, kDeviceRate, 48000, 0.2f);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = 20000.0;
    settings.mode = DemodMode::Usb;
    settings.agcMode = AgcMode::Off;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    channel.process(input.data(), input.size(), audio.data());

    // Cambio di modo e filtro a flusso attivo: nessun crash, catena riavviata
    // in modo pulito, uscita ancora finita.
    settings.mode = DemodMode::Cw;
    settings.filterLowHz = -250;
    settings.filterHighHz = 250;
    settings.cwPitchHz = 700.0;
    channel.applySettings(settings);

    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 0);
    for (std::size_t i = 0; i < produced; ++i)
        QVERIFY(std::isfinite(audio[i]));
}

QTEST_APPLESS_MAIN(TestDspChain)

#include "tst_dsp_chain.moc"
