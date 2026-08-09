// SPDX-License-Identifier: GPL-3.0-or-later
// Catena DSP completa: decimazione, spettro e canale end-to-end (RNF-07).

#include "dsp/ChannelProcessor.h"
#include "dsp/CtcssDetector.h"
#include "dsp/NoiseBlanker.h"
#include "dsp/RdsDecoder.h"
#include "dsp/DecimatorChain.h"
#include "dsp/FmIfNoiseReducer.h"
#include "dsp/SpectrumAnalyzer.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

std::vector<Complex> makeAmTone(double carrierHz,
                                double audioHz,
                                double modulationIndex,
                                double sampleRate,
                                std::size_t n,
                                float amplitude = 0.5f)
{
    std::vector<Complex> out(n);
    double carrierPhase = 0.0;
    const double carrierStep = kTwoPi * carrierHz / sampleRate;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double envelope = 1.0 + modulationIndex * std::sin(kTwoPi * audioHz * t);
        out[i] = Complex(amplitude * static_cast<float>(envelope * std::cos(carrierPhase)),
                         amplitude * static_cast<float>(envelope * std::sin(carrierPhase)));
        carrierPhase += carrierStep;
    }
    return out;
}

std::vector<Complex> makeDsbTone(double carrierHz,
                                 double audioHz,
                                 double sampleRate,
                                 std::size_t n,
                                 float amplitude = 0.4f)
{
    std::vector<Complex> out(n);
    const double carrierStep = kTwoPi * carrierHz / sampleRate;
    const double audioStep = kTwoPi * audioHz / sampleRate;
    for (std::size_t i = 0; i < n; ++i) {
        const double carrierPhase = carrierStep * static_cast<double>(i);
        const float audio = static_cast<float>(std::cos(audioStep * static_cast<double>(i)));
        out[i] = Complex(amplitude * audio * static_cast<float>(std::cos(carrierPhase)),
                         amplitude * audio * static_cast<float>(std::sin(carrierPhase)));
    }
    return out;
}

std::vector<Complex> makeFmTone(double carrierHz,
                                double audioHz,
                                double deviationHz,
                                double sampleRate,
                                std::size_t n,
                                float amplitude = 0.8f)
{
    std::vector<Complex> out(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double frequency = carrierHz
            + deviationHz * std::sin(kTwoPi * audioHz * t);
        phase += kTwoPi * frequency / sampleRate;
        out[i] = Complex(amplitude * static_cast<float>(std::cos(phase)),
                         amplitude * static_cast<float>(std::sin(phase)));
    }
    return out;
}

std::vector<Complex> makeStereoFmTone(double carrierHz,
                                      double sampleRate,
                                      std::size_t n,
                                      float amplitude = 0.8f)
{
    std::vector<Complex> out(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double left = std::sin(kTwoPi * 800.0 * t);
        const double right = 0.8 * std::sin(kTwoPi * 1800.0 * t);
        const double mpx = 0.45 * (left + right)
            + 0.10 * std::cos(kTwoPi * 19000.0 * t)
            + 0.45 * (left - right) * std::cos(kTwoPi * 38000.0 * t);
        phase += kTwoPi * (carrierHz + 50000.0 * mpx) / sampleRate;
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

std::uint16_t rdsSyndrome(std::uint32_t block)
{
    constexpr std::uint16_t lfsrPoly = 0b0110111001;
    constexpr std::uint16_t inputPoly = 0b1100011011;
    std::uint16_t syndrome = 0;
    for (int i = 25; i >= 0; --i) {
        const std::uint8_t output = (syndrome >> 9) & 1u;
        syndrome = static_cast<std::uint16_t>((syndrome << 1) & 0x03ffu);
        syndrome ^= static_cast<std::uint16_t>(lfsrPoly * output);
        syndrome ^= static_cast<std::uint16_t>(inputPoly * ((block >> i) & 1u));
    }
    return syndrome;
}

std::uint32_t makeRdsBlock(std::uint16_t data, std::uint16_t offset)
{
    const std::uint32_t dataPart = static_cast<std::uint32_t>(data) << 10;
    for (std::uint32_t check = 0; check < 1024; ++check) {
        const std::uint32_t block = dataPart | check;
        if (rdsSyndrome(block ^ offset) == 0)
            return block;
    }
    return 0;
}

void appendRdsBlock(std::vector<std::uint8_t> &bits, std::uint32_t block)
{
    for (int i = 25; i >= 0; --i)
        bits.push_back(static_cast<std::uint8_t>((block >> i) & 1u));
}

std::vector<float> makeRdsMpx(const std::vector<std::uint8_t> &bits,
                              double sampleRate)
{
    constexpr double subcarrierHz = 57'000.0;
    constexpr double bitRate = 1187.5;
    const double samplesPerSymbol = sampleRate / bitRate;
    const std::size_t samples = static_cast<std::size_t>(
        std::ceil(static_cast<double>(bits.size()) * samplesPerSymbol)) + 256;
    std::vector<float> out(samples, 0.0f);

    double bpskPhase = 0.0;
    for (std::size_t symbol = 0; symbol < bits.size(); ++symbol) {
        if (bits[symbol])
            bpskPhase += kPi;
        const std::size_t begin = static_cast<std::size_t>(
            std::floor(static_cast<double>(symbol) * samplesPerSymbol));
        const std::size_t end = std::min(samples, static_cast<std::size_t>(
            std::floor(static_cast<double>(symbol + 1) * samplesPerSymbol)));
        for (std::size_t i = begin; i < end; ++i) {
            const double phase = kTwoPi * subcarrierHz * static_cast<double>(i)
                / sampleRate + bpskPhase;
            out[i] = static_cast<float>(std::cos(phase));
        }
    }
    return out;
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
    void channelConfiguresEveryMode();
    void channelEveryModeProducesFiniteStereoAudio();
    void channelSupportsFmDeemphasisVariants();
    void ctcssDetectsRequestedTone();
    void noiseBlankerRejectsImpulse();
    void fmIfNoiseReductionSuppressesHighFrequencyNoise();
    void rdsDecodesProgramService();
    void channelDemodulatesUsbTone();
    void channelDemodulatesLsbAndDigitalModes();
    void channelDemodulatesAmAndSam();
    void channelDemodulatesDsbTone();
    void channelDemodulatesNfmTone();
    void channelSupportsNfmLowPassSwitch();
    void channelAppliesPowerSquelchToSsb();
    void channelAppliesCtcssGateToNfm();
    void channelSupportsCtcssDecodeOnly();
    void channelDemodulatesWideFmTone();
    void channelDemodulatesWideFmStereo();
    void channelSupportsWideFmLowPassSwitch();
    void channelKeepsCwPitchInBothDirections();
    void channelDemodulatesIqMonitor();
    void channelIqStereoMonitorPreservesQ();
    void channelRejectsOppositeSideband();
    void channelSurvivesSettingsChangeMidStream();
    void squelchSilencesWeakSignalsAndOpensOnStrongOnes();
    void channelSeparatesSignalNoiseAndAudioMeters();
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

void TestDspChain::channelConfiguresEveryMode()
{
    const std::vector<DemodMode> modes = {
        DemodMode::Usb, DemodMode::Lsb, DemodMode::Cw, DemodMode::Cwr,
        DemodMode::Am, DemodMode::Sam, DemodMode::Fm, DemodMode::Nfm,
        DemodMode::DigU, DemodMode::DigL, DemodMode::Iq, DemodMode::Dsb,
    };

    for (const DemodMode mode : modes) {
        ChannelProcessor channel;
        ChannelSettings settings;
        settings.mode = mode;
        settings.filterLowHz = (mode == DemodMode::Fm) ? -90000 : 300;
        settings.filterHighHz = (mode == DemodMode::Fm) ? 90000 : 2700;
        if (mode == DemodMode::Dsb) {
            settings.filterLowHz = -2300;
            settings.filterHighHz = 2300;
        }
        settings.agcMode = AgcMode::Off;
        channel.applySettings(settings);

        QVERIFY2(channel.configure(3200000.0, 48000.0),
                 qPrintable(QStringLiteral("configure fallita per %1")
                                .arg(demodModeName(mode))));
        const int expectedDecimation = mode == DemodMode::Fm ? 13 : 67;
        QCOMPARE(channel.decimation(), expectedDecimation);
        QVERIFY2(std::abs(channel.channelRate() - 3200000.0 / expectedDecimation) < 1e-6,
                 qPrintable(QStringLiteral("frequenza canale errata per %1")
                                .arg(demodModeName(mode))));
    }
}

void TestDspChain::channelEveryModeProducesFiniteStereoAudio()
{
    constexpr double kDeviceRate = 192000.0;
    const std::vector<Complex> input = makeTone(21000.0, kDeviceRate, 48000, 0.15f);
    const std::vector<DemodMode> modes = {
        DemodMode::Usb, DemodMode::Lsb, DemodMode::Cw, DemodMode::Cwr,
        DemodMode::Am, DemodMode::Sam, DemodMode::Fm, DemodMode::Nfm,
        DemodMode::DigU, DemodMode::DigL, DemodMode::Iq, DemodMode::Dsb,
    };

    for (const DemodMode mode : modes) {
        ChannelProcessor channel;
        QVERIFY2(channel.configure(kDeviceRate, 48000.0),
                 qPrintable(QStringLiteral("configure fallita per %1")
                                .arg(demodModeName(mode))));

        ChannelSettings settings;
        settings.offsetHz = 20000.0;
        settings.mode = mode;
        settings.filterLowHz = (mode == DemodMode::Fm) ? -90000 :
            (mode == DemodMode::Nfm ? -6000 : 300);
        settings.filterHighHz = (mode == DemodMode::Fm) ? 90000 :
            (mode == DemodMode::Nfm ? 6000 : 2700);
        if (mode == DemodMode::Am || mode == DemodMode::Sam) {
            settings.filterLowHz = -4000;
            settings.filterHighHz = 4000;
        }
        if (mode == DemodMode::Cw || mode == DemodMode::Cwr) {
            settings.filterLowHz = -250;
            settings.filterHighHz = 250;
        }
        if (mode == DemodMode::Iq) {
            settings.filterLowHz = -6000;
            settings.filterHighHz = 6000;
        }
        if (mode == DemodMode::Dsb) {
            settings.filterLowHz = -2300;
            settings.filterHighHz = 2300;
        }
        settings.agcMode = AgcMode::Off;
        settings.fmDeemphasisUs = (mode == DemodMode::Nfm) ? 75.0
            : (mode == DemodMode::Fm ? 50.0 : 0.0);
        settings.volume = 1.0f;
        channel.applySettings(settings);

        std::vector<float> audio(channel.maxAudioFrames(input.size()) * 2);
        const std::size_t produced = channel.processStereo(input.data(), input.size(),
                                                           audio.data());
        QVERIFY2(produced > 0,
                 qPrintable(QStringLiteral("nessun audio per %1")
                                .arg(demodModeName(mode))));
        for (std::size_t i = 0; i < produced * 2; ++i)
            QVERIFY2(std::isfinite(audio[i]),
                     qPrintable(QStringLiteral("NaN/Inf per %1")
                                    .arg(demodModeName(mode))));
    }
}

void TestDspChain::channelSupportsFmDeemphasisVariants()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr std::size_t kSamples = 48000;
    const std::vector<Complex> input = makeFmTone(0.0, 1000.0, 75000.0,
                                                   kDeviceRate, kSamples, 0.7f);

    for (const double deemphasisUs : {0.0, 22.0, 50.0, 75.0}) {
        ChannelProcessor channel;
        QVERIFY(channel.configure(kDeviceRate, 48000.0));

        ChannelSettings settings;
        settings.mode = DemodMode::Fm;
        settings.filterLowHz = -90000;
        settings.filterHighHz = 90000;
        settings.agcMode = AgcMode::Off;
        settings.fmStereo = false;
        settings.fmDeemphasisUs = deemphasisUs;
        settings.volume = 1.0f;
        channel.applySettings(settings);

        std::vector<float> audio(channel.maxAudioFrames(input.size()) * 2);
        const std::size_t produced = channel.processStereo(input.data(), input.size(),
                                                           audio.data());
        QVERIFY2(produced > 0,
                 qPrintable(QStringLiteral("nessun audio con de-enfasi %1 us")
                                .arg(deemphasisUs)));
        for (std::size_t i = 0; i < produced * 2; ++i)
            QVERIFY2(std::isfinite(audio[i]),
                     qPrintable(QStringLiteral("NaN/Inf con de-enfasi %1 us")
                                    .arg(deemphasisUs)));
    }
}

void TestDspChain::ctcssDetectsRequestedTone()
{
    constexpr double kRate = 48000.0;
    constexpr double kTone = 100.0;
    constexpr std::size_t kSamples = 8192;

    std::vector<float> tone(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i)
        tone[i] = 0.2f * static_cast<float>(
            std::sin(kTwoPi * kTone * static_cast<double>(i) / kRate));

    CtcssDetector detector;
    QVERIFY(detector.configure(kRate, kTone));
    detector.process(tone.data(), tone.size());
    QVERIFY2(detector.detected(),
             qPrintable(QStringLiteral("CTCSS non rilevato, livello %1 dB")
                            .arg(detector.levelDb())));

    detector.reset();
    for (std::size_t i = 0; i < kSamples; ++i)
        tone[i] = 0.2f * static_cast<float>(
            std::sin(kTwoPi * 1500.0 * static_cast<double>(i) / kRate));
    detector.process(tone.data(), tone.size());
    QVERIFY2(!detector.detected(),
             qPrintable(QStringLiteral("falso CTCSS su tono audio, livello %1 dB")
                            .arg(detector.levelDb())));
}

void TestDspChain::noiseBlankerRejectsImpulse()
{
    // Il blanker non è più del canale: vive nel motore e lavora a banda piena
    // prima della decimazione (SPEC-003 §4). La sua API è cambiata di
    // conseguenza — soglia in multipli della mediana invece che in dB, e
    // impulso ricucito per interpolazione invece che tenuto fermo — ma la
    // promessa verificata qui è la stessa: dopo, al posto dell'impulso c'è il
    // segnale che c'era intorno.
    NoiseBlanker blanker;
    blanker.configure(48000.0);
    blanker.setThreshold(4.0);

    std::vector<Complex> samples(256, Complex(0.2f, 0.0f));
    samples[180] = Complex(10.0f, 0.0f);
    QVERIFY(blanker.process(samples.data(), samples.size()) > 0);

    QVERIFY2(std::abs(samples[180].real() - 0.2f) < 0.05f,
             qPrintable(QStringLiteral("impulso non soppresso: %1")
                            .arg(samples[180].real())));
}

void TestDspChain::fmIfNoiseReductionSuppressesHighFrequencyNoise()
{
    constexpr double kRate = 48'000.0;
    constexpr double kTone = 12'000.0;
    std::vector<Complex> samples(4096);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double phase = kTwoPi * kTone * static_cast<double>(i) / kRate;
        samples[i] = Complex(static_cast<float>(std::cos(phase)),
                             static_cast<float>(std::sin(phase)));
    }

    FmIfNoiseReducer reducer;
    QVERIFY(reducer.configure(kRate));
    reducer.setPreset(0); // Voice
    reducer.process(samples.data(), samples.size());

    double power = 0.0;
    for (std::size_t i = 1024; i < samples.size(); ++i)
        power += magnitudeSquared(samples[i]);
    const double rmsAfter = std::sqrt(power / static_cast<double>(samples.size() - 1024));
    QVERIFY2(rmsAfter < 0.8,
             qPrintable(QStringLiteral("rumore IF non ridotto: RMS %1").arg(rmsAfter)));
}

void TestDspChain::rdsDecodesProgramService()
{
    constexpr double kRate = 240'000.0;
    constexpr std::uint16_t kPi = 0x1234;
    const std::uint16_t offsets[] = {
        0b0011111100, // A
        0b0110011000, // B
        0b0101101000, // C
        0b0110110100, // D
    };

    std::vector<std::uint8_t> bits{0}; // simbolo di preambolo per il differenziale
    const char ps[] = {'T', 'E', 'S', 'T', 'F', 'M', ' ', ' '};
    // Un demodulatore reale può acquisire il sincronismo a metà del primo
    // gruppo; ripetere il ciclo è il comportamento normale dell'emittente e
    // verifica che il decoder converga senza richiedere un allineamento
    // iniziale artificiale.
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (int address = 0; address < 4; ++address) {
            appendRdsBlock(bits, makeRdsBlock(kPi, offsets[0]));
            const std::uint16_t blockB = static_cast<std::uint16_t>(
                (10 << 5) | (address & 0x03)); // PTY 10 = Pop Music
            appendRdsBlock(bits, makeRdsBlock(blockB, offsets[1]));
            appendRdsBlock(bits, makeRdsBlock(102 << 8, offsets[2])); // 97.7 MHz AF
            const std::uint16_t blockD = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(ps[address * 2]) << 8)
                | static_cast<std::uint8_t>(ps[address * 2 + 1]));
            appendRdsBlock(bits, makeRdsBlock(blockD, offsets[3]));
        }
    }

    const std::vector<float> mpx = makeRdsMpx(bits, kRate);
    RdsDecoder decoder;
    QVERIFY(decoder.configure(kRate));
    decoder.process(mpx.data(), mpx.size());

    QVERIFY2(decoder.synced(), "RDS non sincronizzato sul segnale sintetico");
    QCOMPARE(decoder.piCode(), kPi);
    QCOMPARE(decoder.countryCode(), static_cast<std::uint8_t>(1));
    QCOMPARE(decoder.programCoverage(), static_cast<std::uint8_t>(2));
    QCOMPARE(decoder.programReferenceNumber(), static_cast<std::uint8_t>(0x34));
    QCOMPARE(decoder.programType(), static_cast<std::uint8_t>(10));
    QCOMPARE(QString::fromStdString(decoder.programTypeName()), QStringLiteral("Pop Music"));
    QCOMPARE(QString::fromStdString(decoder.alternateFrequencies()),
             QStringLiteral("97.7 MHz"));
    QCOMPARE(QString::fromStdString(decoder.programService()), QStringLiteral("TESTFM"));

    decoder.setRegion(RdsRegion::NorthAmerica);
    QCOMPARE(QString::fromStdString(decoder.programTypeName()), QStringLiteral("Country"));
    QVERIFY(!decoder.callsign().empty());
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

void TestDspChain::channelDemodulatesLsbAndDigitalModes()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kAudioTone = 1000.0;

    for (const DemodMode mode : {DemodMode::Lsb, DemodMode::DigU, DemodMode::DigL}) {
        const bool lower = mode == DemodMode::Lsb || mode == DemodMode::DigL;
        const double rfTone = kChannelOffset + (lower ? -kAudioTone : kAudioTone);
        const std::vector<Complex> input = makeTone(rfTone, kDeviceRate, 96000, 0.2f);

        ChannelProcessor channel;
        QVERIFY2(channel.configure(kDeviceRate, 48000.0),
                 qPrintable(QStringLiteral("configure fallita per %1")
                                .arg(demodModeName(mode))));

        ChannelSettings settings;
        settings.offsetHz = kChannelOffset;
        settings.mode = mode;
        settings.filterLowHz = 300;
        settings.filterHighHz = 2700;
        settings.agcMode = AgcMode::Off;
        settings.volume = 1.0f;
        channel.applySettings(settings);

        std::vector<float> audio(channel.maxAudioFrames(input.size()));
        const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
        QVERIFY(produced > 1000);

        const std::size_t skip = 2000;
        const double wanted = goertzel(audio.data() + skip, produced - skip,
                                       kAudioTone, 48000.0);
        const double other = goertzel(audio.data() + skip, produced - skip,
                                      1800.0, 48000.0);
        QVERIFY2(wanted > 0.02,
                 qPrintable(QStringLiteral("%1: tono assente (%2)")
                                .arg(demodModeName(mode)).arg(wanted)));
        QVERIFY2(wanted > other * 30.0,
                 qPrintable(QStringLiteral("%1: atteso=%2 spurio=%3")
                                .arg(demodModeName(mode)).arg(wanted).arg(other)));
    }
}

void TestDspChain::channelDemodulatesAmAndSam()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kAudioTone = 1000.0;
    const std::vector<Complex> input = makeAmTone(kChannelOffset, kAudioTone, 0.5,
                                                   kDeviceRate, 192000, 0.4f);

    for (const DemodMode mode : {DemodMode::Am, DemodMode::Sam}) {
        ChannelProcessor channel;
        QVERIFY2(channel.configure(kDeviceRate, 48000.0),
                 qPrintable(QStringLiteral("configure fallita per %1")
                                .arg(demodModeName(mode))));

        ChannelSettings settings;
        settings.offsetHz = kChannelOffset;
        settings.mode = mode;
        settings.filterLowHz = -4000;
        settings.filterHighHz = 4000;
        settings.agcMode = AgcMode::Off;
        settings.amCarrierAgc = mode == DemodMode::Am;
        settings.volume = 1.0f;
        channel.applySettings(settings);

        std::vector<float> audio(channel.maxAudioFrames(input.size()));
        const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
        QVERIFY(produced > 2000);

        const std::size_t skip = mode == DemodMode::Sam ? 6000 : 2000;
        const double wanted = goertzel(audio.data() + skip, produced - skip,
                                       kAudioTone, 48000.0);
        QVERIFY2(wanted > 0.02,
                 qPrintable(QStringLiteral("%1: tono AM assente (%2)")
                                .arg(demodModeName(mode)).arg(wanted)));
    }
}

void TestDspChain::channelDemodulatesDsbTone()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kAudioTone = 1000.0;
    const std::vector<Complex> input = makeDsbTone(kChannelOffset, kAudioTone,
                                                   kDeviceRate, 192000);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));
    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Dsb;
    settings.filterLowHz = -2300;
    settings.filterHighHz = 2300;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 2000);
    const std::size_t skip = 2000;
    const double wanted = goertzel(audio.data() + skip, produced - skip,
                                   kAudioTone, 48000.0);
    QVERIFY2(wanted > 0.02,
             qPrintable(QStringLiteral("DSB: tono assente (%1)").arg(wanted)));
}

void TestDspChain::channelDemodulatesNfmTone()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kAudioTone = 1000.0;
    const std::vector<Complex> input = makeFmTone(kChannelOffset, kAudioTone, 2500.0,
                                                   kDeviceRate, 192000);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Nfm;
    settings.filterLowHz = -6000;
    settings.filterHighHz = 6000;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 2000);

    const std::size_t skip = 2000;
    const double wanted = goertzel(audio.data() + skip, produced - skip,
                                   kAudioTone, 48000.0);
    const double other = goertzel(audio.data() + skip, produced - skip,
                                  3000.0, 48000.0);
    QVERIFY2(wanted > 0.05, qPrintable(QStringLiteral("NFM: tono assente (%1").arg(wanted)));
    QVERIFY2(wanted > other * 10.0,
             qPrintable(QStringLiteral("NFM: atteso=%1 spurio=%2").arg(wanted).arg(other)));
}

void TestDspChain::channelSupportsNfmLowPassSwitch()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kAudioTone = 5000.0;
    const std::vector<Complex> input = makeFmTone(kChannelOffset, kAudioTone, 2500.0,
                                                   kDeviceRate, 192000);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Nfm;
    settings.filterLowHz = -6000;
    settings.filterHighHz = 6000;
    settings.agcMode = AgcMode::Off;
    settings.fmDeemphasisUs = 0.0;
    settings.volume = 1.0f;

    settings.fmAudioLowPass = true;
    channel.applySettings(settings);
    std::vector<float> filtered(channel.maxAudioFrames(input.size()));
    const std::size_t filteredFrames = channel.process(input.data(), input.size(),
                                                       filtered.data());

    settings.fmAudioLowPass = false;
    channel.applySettings(settings);
    std::vector<float> unfiltered(channel.maxAudioFrames(input.size()));
    const std::size_t unfilteredFrames = channel.process(input.data(), input.size(),
                                                         unfiltered.data());

    const std::size_t skip = 16000;
    const double filteredTone = goertzel(filtered.data() + skip,
                                         filteredFrames - skip, kAudioTone, 48000.0);
    const double unfilteredTone = goertzel(unfiltered.data() + skip,
                                           unfilteredFrames - skip, kAudioTone, 48000.0);
    QVERIFY2(unfilteredTone > 0.05,
             qPrintable(QStringLiteral("NFM low-pass test: tono bypass assente (%1)")
                            .arg(unfilteredTone)));
    QVERIFY2(filteredTone < unfilteredTone * 0.5,
             qPrintable(QStringLiteral("NFM low-pass non attenua: on=%1 off=%2")
                            .arg(filteredTone).arg(unfilteredTone)));
}

void TestDspChain::channelSeparatesSignalNoiseAndAudioMeters()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr std::size_t kBlock = 4096;

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Nfm;
    settings.filterLowHz = -6000;
    settings.filterHighHz = 6000;
    settings.agcMode = AgcMode::Off;
    settings.fmDeemphasisUs = 0.0;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(kBlock));
    const std::vector<Complex> weak = makeTone(kChannelOffset, kDeviceRate,
                                               kBlock, 0.01f);
    for (int i = 0; i < 6; ++i)
        channel.process(weak.data(), weak.size(), audio.data());
    const float weakLevel = channel.signalLevelDb();
    const float measuredNoise = channel.noiseFloorDb();

    const std::vector<Complex> strong = makeFmTone(kChannelOffset, 1000.0,
                                                   2500.0, kDeviceRate, kBlock,
                                                   0.25f);
    for (int i = 0; i < 8; ++i)
        channel.process(strong.data(), strong.size(), audio.data());

    QVERIFY2(channel.signalLevelDb() > weakLevel + 10.0f,
             qPrintable(QStringLiteral("S-meter non separato: debole=%1 forte=%2")
                            .arg(weakLevel).arg(channel.signalLevelDb())));
    QVERIFY2(channel.noiseFloorDb() < channel.signalLevelDb() - 8.0f,
             qPrintable(QStringLiteral("fondo rumore=%1 segnale=%2")
                            .arg(channel.noiseFloorDb()).arg(channel.signalLevelDb())));
    QVERIFY2(channel.snrDb() > 8.0f,
             qPrintable(QStringLiteral("SNR insufficiente: %1 dB").arg(channel.snrDb())));
    QVERIFY2(channel.noiseFloorDb() >= measuredNoise - 8.0f,
             qPrintable(QStringLiteral("fondo rumore instabile: prima=%1 dopo=%2")
                            .arg(measuredNoise).arg(channel.noiseFloorDb())));
    QVERIFY2(channel.audioLevelDb() > -100.0f,
             qPrintable(QStringLiteral("meter audio fermo a fondo scala: %1 dBFS")
                            .arg(channel.audioLevelDb())));
}

void TestDspChain::channelAppliesPowerSquelchToSsb()
{
    constexpr double kRate = 192000.0;
    constexpr double kOffset = 20000.0;
    const std::vector<Complex> input = makeTone(kOffset + 1000.0, kRate,
                                                 192000, 0.2f);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kRate, 48000.0));
    ChannelSettings settings;
    settings.offsetHz = kOffset;
    settings.mode = DemodMode::Usb;
    settings.filterLowHz = 300;
    settings.filterHighHz = 2700;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    settings.squelchEnabled = true;
    settings.squelchThresholdDb = -5.0;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 1000);
    double closedPower = 0.0;
    for (std::size_t i = produced / 2; i < produced; ++i)
        closedPower += static_cast<double>(audio[i]) * audio[i];
    QVERIFY2(closedPower < 1e-6,
             qPrintable(QStringLiteral("squelch SSB aperto: potenza=%1")
                            .arg(closedPower)));

    settings.squelchThresholdDb = -60.0;
    channel.applySettings(settings);
    produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 1000);
    const double wanted = goertzel(audio.data() + produced / 2,
                                   produced - produced / 2, 1000.0, 48000.0);
    QVERIFY2(wanted > 0.02,
             qPrintable(QStringLiteral("squelch SSB non riaperto: %1").arg(wanted)));
}

void TestDspChain::channelAppliesCtcssGateToNfm()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    const std::vector<Complex> input = makeFmTone(kChannelOffset, 100.0,
                                                  2500.0, kDeviceRate, 192000);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));
    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Nfm;
    settings.filterLowHz = -6000;
    settings.filterHighHz = 6000;
    settings.agcMode = AgcMode::Off;
    settings.fmDeemphasisUs = 0.0;
    settings.ctcssEnabled = true;
    settings.ctcssToneHz = 100.0;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 40000);
    const std::size_t skip = 16000;
    const double tone = goertzel(audio.data() + skip, produced - skip,
                                 100.0, 48000.0);
    QVERIFY2(tone > 0.02,
             qPrintable(QStringLiteral("CTCSS non apre il canale NFM: %1")
                            .arg(tone)));
}

void TestDspChain::channelSupportsCtcssDecodeOnly()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    const std::vector<Complex> input = makeFmTone(kChannelOffset, 100.0,
                                                  2500.0, kDeviceRate, 192000);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));
    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Nfm;
    settings.filterLowHz = -6000;
    settings.filterHighHz = 6000;
    settings.agcMode = AgcMode::Off;
    settings.fmDeemphasisUs = 0.0;
    settings.ctcssEnabled = true;
    settings.ctcssDecodeOnly = true;
    settings.ctcssToneHz = 123.0;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 40000);
    const std::size_t skip = 16000;
    const double tone = goertzel(audio.data() + skip, produced - skip,
                                 100.0, 48000.0);
    QVERIFY2(tone > 0.02,
             qPrintable(QStringLiteral("CTCSS decode-only ha silenziato l'audio: %1")
                            .arg(tone)));
    QVERIFY2(!channel.ctcssDetected(),
             qPrintable(QStringLiteral("CTCSS decode-only ha rilevato il tono errato (%1 dB)")
                            .arg(channel.ctcssLevelDb())));
}

void TestDspChain::channelDemodulatesWideFmTone()
{
    constexpr double kDeviceRate = 480000.0;
    // L'offset volutamente supera il bordo del filtro Wide-FM: questo
    // intercetta una regressione in cui il cambio modo perde la sintonia e
    // lascia il mixer a 0 Hz.
    constexpr double kChannelOffset = 120000.0;
    constexpr double kAudioTone = 1000.0;
    const std::vector<Complex> input = makeFmTone(kChannelOffset, kAudioTone, 50000.0,
                                                   kDeviceRate, 480000);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Fm;
    settings.filterLowHz = -90000;
    settings.filterHighHz = 90000;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    QCOMPARE(channel.decimation(), 2);
    QCOMPARE(channel.channelRate(), 240000.0);
    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 20000);

    const std::size_t skip = 4000;
    const double wanted = goertzel(audio.data() + skip, produced - skip,
                                   kAudioTone, 48000.0);
    const double other = goertzel(audio.data() + skip, produced - skip,
                                  3000.0, 48000.0);
    QVERIFY2(wanted > 0.05,
             qPrintable(QStringLiteral("Wide-FM: tono assente (%1)").arg(wanted)));
    QVERIFY2(wanted > other * 10.0,
             qPrintable(QStringLiteral("Wide-FM: atteso=%1 spurio=%2").arg(wanted).arg(other)));
}

void TestDspChain::channelDemodulatesWideFmStereo()
{
    constexpr double kDeviceRate = 480000.0;
    constexpr double kChannelOffset = 20000.0;
    const std::vector<Complex> input = makeStereoFmTone(kChannelOffset,
                                                        kDeviceRate, 480000);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Fm;
    settings.filterLowHz = -90000;
    settings.filterHighHz = 90000;
    settings.agcMode = AgcMode::Off;
    settings.fmStereo = true;
    settings.fmDeemphasisUs = 0.0;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()) * 2);
    const std::size_t produced = channel.processStereo(input.data(), input.size(),
                                                       audio.data());
    QVERIFY(produced > 20000);

    const std::size_t skip = 6000;
    std::vector<float> left(produced - skip);
    std::vector<float> right(produced - skip);
    for (std::size_t i = skip; i < produced; ++i) {
        left[i - skip] = audio[i * 2];
        right[i - skip] = audio[i * 2 + 1];
    }

    const double leftWanted = goertzel(left.data(), left.size(), 800.0, 48000.0);
    const double leftLeak = goertzel(left.data(), left.size(), 1800.0, 48000.0);
    const double rightWanted = goertzel(right.data(), right.size(), 1800.0, 48000.0);
    const double rightLeak = goertzel(right.data(), right.size(), 800.0, 48000.0);

    QVERIFY2(leftWanted > 0.03,
             qPrintable(QStringLiteral("stereo L assente = %1").arg(leftWanted)));
    QVERIFY2(rightWanted > 0.02,
             qPrintable(QStringLiteral("stereo R assente = %1").arg(rightWanted)));
    QVERIFY2(leftWanted > leftLeak * 2.0,
             qPrintable(QStringLiteral("separazione L: wanted=%1 leak=%2")
                            .arg(leftWanted).arg(leftLeak)));
    QVERIFY2(rightWanted > rightLeak * 2.0,
             qPrintable(QStringLiteral("separazione R: wanted=%1 leak=%2")
                            .arg(rightWanted).arg(rightLeak)));
}

void TestDspChain::channelSupportsWideFmLowPassSwitch()
{
    constexpr double kDeviceRate = 480000.0;
    constexpr double kOffset = 20000.0;
    const std::vector<Complex> input = makeFmTone(kOffset, 17000.0, 50000.0,
                                                   kDeviceRate, 480000);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));
    ChannelSettings settings;
    settings.offsetHz = kOffset;
    settings.mode = DemodMode::Fm;
    settings.filterLowHz = -90000;
    settings.filterHighHz = 90000;
    settings.agcMode = AgcMode::Off;
    settings.fmStereo = false;
    settings.volume = 1.0f;

    for (const bool lowPass : {true, false}) {
        settings.fmAudioLowPass = lowPass;
        channel.applySettings(settings);
        std::vector<float> audio(channel.maxAudioFrames(input.size()) * 2);
        const std::size_t produced = channel.processStereo(input.data(), input.size(),
                                                           audio.data());
        QVERIFY(produced > 20000);
        for (std::size_t i = 0; i < produced * 2; ++i)
            QVERIFY(std::isfinite(audio[i]));
    }
}

void TestDspChain::channelKeepsCwPitchInBothDirections()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kPitch = 600.0;
    const std::vector<Complex> input = makeTone(kChannelOffset, kDeviceRate, 96000, 0.2f);

    for (const DemodMode mode : {DemodMode::Cw, DemodMode::Cwr}) {
        ChannelProcessor channel;
        QVERIFY(channel.configure(kDeviceRate, 48000.0));

        ChannelSettings settings;
        settings.offsetHz = kChannelOffset;
        settings.mode = mode;
        settings.filterLowHz = -250;
        settings.filterHighHz = 250;
        settings.cwPitchHz = kPitch;
        settings.agcMode = AgcMode::Off;
        settings.volume = 1.0f;
        channel.applySettings(settings);

        std::vector<float> audio(channel.maxAudioFrames(input.size()));
        const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
        QVERIFY(produced > 1000);
        const std::size_t skip = 2000;
        const double wanted = goertzel(audio.data() + skip, produced - skip,
                                       kPitch, 48000.0);
        QVERIFY2(wanted > 0.02,
                 qPrintable(QStringLiteral("%1: pitch CW assente (%2)")
                                .arg(demodModeName(mode)).arg(wanted)));
    }
}

void TestDspChain::channelDemodulatesIqMonitor()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kAudioTone = 1000.0;
    const std::vector<Complex> input = makeTone(kChannelOffset + kAudioTone,
                                                 kDeviceRate, 96000, 0.2f);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Iq;
    settings.filterLowHz = -6000;
    settings.filterHighHz = 6000;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()));
    const std::size_t produced = channel.process(input.data(), input.size(), audio.data());
    QVERIFY(produced > 1000);
    const std::size_t skip = 2000;
    const double wanted = goertzel(audio.data() + skip, produced - skip,
                                   kAudioTone, 48000.0);
    QVERIFY2(wanted > 0.02,
             qPrintable(QStringLiteral("IQ monitor: tono assente (%1").arg(wanted)));
}

void TestDspChain::channelIqStereoMonitorPreservesQ()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    const std::vector<Complex> input = makeTone(kChannelOffset + 1000.0,
                                                 kDeviceRate, 96000, 0.2f);

    ChannelProcessor channel;
    QVERIFY(channel.configure(kDeviceRate, 48000.0));

    ChannelSettings settings;
    settings.offsetHz = kChannelOffset;
    settings.mode = DemodMode::Iq;
    settings.filterLowHz = -6000;
    settings.filterHighHz = 6000;
    settings.agcMode = AgcMode::Off;
    settings.volume = 1.0f;
    channel.applySettings(settings);

    std::vector<float> audio(channel.maxAudioFrames(input.size()) * 2);
    const std::size_t produced = channel.processStereo(input.data(), input.size(),
                                                       audio.data());
    QVERIFY(produced > 1000);
    const std::size_t skip = 2000;
    std::vector<float> left(produced - skip);
    std::vector<float> right(produced - skip);
    for (std::size_t i = skip; i < produced; ++i) {
        left[i - skip] = audio[i * 2];
        right[i - skip] = audio[i * 2 + 1];
    }
    QVERIFY(goertzel(left.data(), left.size(), 1000.0, 48000.0) > 0.02);
    QVERIFY(goertzel(right.data(), right.size(), 1000.0, 48000.0) > 0.02);
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

void TestDspChain::squelchSilencesWeakSignalsAndOpensOnStrongOnes()
{
    constexpr double kDeviceRate = 192000.0;
    constexpr double kChannelOffset = 20000.0;
    constexpr double kAudioTone = 1000.0;

    const auto rms = [](const float *data, std::size_t n) {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            sum += double(data[i]) * data[i];
        return std::sqrt(sum / double(n));
    };

    const auto runWith = [&](float amplitude, bool squelchOn) {
        const std::vector<Complex> input =
            makeTone(kChannelOffset + kAudioTone, kDeviceRate, 192000, amplitude);

        ChannelProcessor channel;
        channel.configure(kDeviceRate, 48000.0);

        ChannelSettings settings;
        settings.offsetHz = kChannelOffset;
        settings.mode = DemodMode::Usb;
        settings.agcMode = AgcMode::Off;   // con l'AGC il debole verrebbe tirato su
        settings.volume = 1.0f;
        settings.squelchEnabled = squelchOn;
        settings.squelchThresholdDb = -40.0;
        channel.applySettings(settings);

        std::vector<float> audio(channel.maxAudioFrames(input.size()));
        const std::size_t produced = channel.process(input.data(), input.size(), audio.data());

        // Si misura sulla coda: l'apertura e la chiusura sono graduali per non
        // farsi sentire come un colpo secco, e all'inizio il guadagno sta
        // ancora salendo.
        const std::size_t skip = produced / 2;
        return rms(audio.data() + skip, produced - skip);
    };

    // Un segnale ben sopra la soglia passa, con o senza squelch.
    const double strongOpen = runWith(0.30f, true);
    const double strongOff = runWith(0.30f, false);
    QVERIFY2(strongOpen > 0.5 * strongOff,
             qPrintable(QStringLiteral("lo squelch strozza un segnale forte: %1 contro %2")
                            .arg(strongOpen).arg(strongOff)));

    // Uno molto sotto la soglia viene tacitato — ed è il punto dell'esercizio.
    const double weakSquelched = runWith(0.0005f, true);
    const double weakOpen = runWith(0.0005f, false);
    QVERIFY2(weakOpen > 0.0, "il segnale debole non arriva nemmeno senza squelch");
    QVERIFY2(weakSquelched < weakOpen * 0.05,
             qPrintable(QStringLiteral("lo squelch non chiude: %1 contro %2 a squelch spento")
                            .arg(weakSquelched).arg(weakOpen)));
}

QTEST_APPLESS_MAIN(TestDspChain)

#include "tst_dsp_chain.moc"
