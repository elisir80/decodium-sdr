// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/RdsDecoder.h"
#include "dsp/FirDesign.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <numeric>
#include <string>

namespace dsdr::dsp {

namespace {
constexpr double kRdsSubcarrierHz = 57'000.0;
constexpr double kRdsReferenceHz = 63'500.0;
constexpr double kRdsBitRate = 1187.5;
constexpr double kRdsTargetRate = 5'000.0;
constexpr std::size_t kRdsWorkBufferSamples = 65'536;
constexpr int kClockInterpPhaseCount = 128;
constexpr int kClockInterpTapCount = 8;
constexpr std::uint16_t kSyndromeA = 0b1111011000;
constexpr std::uint16_t kSyndromeB = 0b1111010100;
constexpr std::uint16_t kSyndromeC = 0b1001011100;
constexpr std::uint16_t kSyndromeCp = 0b1111001100;
constexpr std::uint16_t kSyndromeD = 0b1001011000;

constexpr std::uint16_t kOffsetA = 0b0011111100;
constexpr std::uint16_t kOffsetB = 0b0110011000;
constexpr std::uint16_t kOffsetC = 0b0101101000;
constexpr std::uint16_t kOffsetCp = 0b1101010000;
constexpr std::uint16_t kOffsetD = 0b0110110100;

constexpr std::uint16_t kLfsrPoly = 0b0110111001;
constexpr std::uint16_t kInputPoly = 0b1100011011;

} // namespace

RdsDecoder::BlockType RdsDecoder::nextBlockType(BlockType type) noexcept
{
    switch (type) {
    case BlockType::A:  return BlockType::B;
    case BlockType::B:  return BlockType::C;
    case BlockType::C:
    case BlockType::Cp: return BlockType::D;
    case BlockType::D:  return BlockType::A;
    }
    return BlockType::A;
}

bool RdsDecoder::configure(double sampleRate)
{
    if (sampleRate < 120'000.0)
        return false;

    m_sampleRate = sampleRate;
    m_subcarrierOmega = kTwoPi * kRdsSubcarrierHz / sampleRate;
    m_referenceOmega = kTwoPi * kRdsReferenceHz / sampleRate;
    // Match SDR++'s rational RDS resampler: power-of-two predecimation,
    // followed by an exact integer interpolation/decimation pair to 5 kHz.
    // The latter matters because 256 kHz / 51 is 5019.6078 Hz, which moves
    // the 1187.5 baud clock enough to make a marginal RDS stream drift.
    const int power = std::clamp(static_cast<int>(std::floor(
        std::log2(sampleRate / kRdsTargetRate))), 0, 8);
    const int predecimation = 1 << power;
    if (!m_rdsDecimator.configure(sampleRate, predecimation, 2500.0))
        return false;
    if (!m_referenceDecimator.configure(sampleRate, predecimation, 2500.0))
        return false;

    const double predecimatedRate = m_rdsDecimator.outputRate();
    const int inputRate = std::max(1, static_cast<int>(std::lround(predecimatedRate)));
    const int outputRate = static_cast<int>(std::lround(kRdsTargetRate));
    const int divisor = std::gcd(inputRate, outputRate);
    const int interpolation = outputRate / divisor;
    m_rdsResampleDecimation = inputRate / divisor;
    if (!m_rdsInterpolator.configure(predecimatedRate, interpolation, 2375.0))
        return false;
    m_rdsSampleRate = predecimatedRate * interpolation
        / static_cast<double>(m_rdsResampleDecimation);
    m_symbolPeriod = m_rdsSampleRate / kRdsBitRate;
    m_clockNominalOmega = m_symbolPeriod;
    m_clockOmega = m_clockNominalOmega;

    // A narrow BPSK Costas loop removes residual phase/frequency error left
    // by the 57 kHz translator. The differential decoder below still removes
    // the 180-degree ambiguity of the loop.
    constexpr double kCostasBandwidth = 0.005;
    constexpr double kCostasDamping = 0.707;
    const double costasDenominator = 1.0
        + 2.0 * kCostasDamping * kCostasBandwidth
        + kCostasBandwidth * kCostasBandwidth;
    m_costasAlpha = 4.0 * kCostasDamping * kCostasBandwidth
        / costasDenominator;
    m_costasBeta = 4.0 * kCostasBandwidth * kCostasBandwidth
        / costasDenominator;
    // SDR++ leaves the first Costas loop at its default +/- pi capture range.
    m_costasMaxFrequency = kPi;

    // SDR++ uses a second, slow Costas loop at half the 2.375 kHz RDS
    // channel bandwidth before converting the recovered stream to real.
    // This removes the residual symbol-rate rotation and leaves a clean eye
    // for the Mueller-Muller clock recovery.
    constexpr double kClockCostasBandwidth = 0.01;
    constexpr double kClockCostasDamping = 0.707;
    const double clockCostasDenominator = 1.0
        + 2.0 * kClockCostasDamping * kClockCostasBandwidth
        + kClockCostasBandwidth * kClockCostasBandwidth;
    m_clockCostasAlpha = 4.0 * kClockCostasDamping * kClockCostasBandwidth
        / clockCostasDenominator;
    m_clockCostasBeta = 4.0 * kClockCostasBandwidth * kClockCostasBandwidth
        / clockCostasDenominator;
    m_clockCostasFrequency = kTwoPi * (kRdsBitRate / m_rdsSampleRate);
    m_clockCostasMinFrequency = m_clockCostasFrequency * 0.9;
    m_clockCostasMaxFrequency = m_clockCostasFrequency * 1.1;

    // The resampler output is now exactly 5 kHz, matching SDR++ and the RDS
    // demodulator's 0..2375 Hz complex branch.
    m_clockOmegaGain = 1.0e-6;
    m_clockMuGain = 0.01;
    m_rdsMixed.assign(kRdsWorkBufferSamples, Complex(0.0f, 0.0f));
    m_referenceMixed.assign(kRdsWorkBufferSamples, Complex(0.0f, 0.0f));
    m_rdsDecimated.assign(kRdsWorkBufferSamples, Complex(0.0f, 0.0f));
    m_referenceDecimated.assign(kRdsWorkBufferSamples, Complex(0.0f, 0.0f));
    m_rdsInterpolated.assign(kRdsWorkBufferSamples, Complex(0.0f, 0.0f));
    // Select the positive-frequency half of the translated real RDS signal.
    // `designBandpass` follows the normal e^(+j*omega*n) tap convention used
    // by ComplexFir, so no additional conjugation is needed here.
    std::vector<Complex> rdsTaps = designBandpass(
        0.0, 2375.0, m_rdsSampleRate, 255, kaiserBeta(70.0));
    m_rdsAnalyticFilter.setTaps(rdsTaps);

    // SDR++ uses the same 128-phase, 8-tap windowed-sinc interpolator for
    // its Mueller--Muller clock recovery.  Keep the phase bank in the
    // decoder so the live path never has to interpolate linearly between
    // samples whose eye opening is already only a few samples wide.
    const int tapCount = kClockInterpPhaseCount * kClockInterpTapCount;
    const double omega = kPi / static_cast<double>(kClockInterpPhaseCount);
    m_clockInterpTaps.assign(
        static_cast<std::size_t>(kClockInterpPhaseCount * kClockInterpTapCount),
        0.0f);
    std::vector<float> prototype(static_cast<std::size_t>(tapCount), 0.0f);
    const double half = static_cast<double>(tapCount) * 0.5;
    // SDR++ passes the phase count as the windowed-sinc normalization, so
    // the polyphase branches retain unit gain instead of each carrying only
    // 1/128 of the interpolator output.
    const double correction = 1.0;
    for (int i = 0; i < tapCount; ++i) {
        const double t = static_cast<double>(i) - half + 0.5;
        const double n = t - half;
        const double windowPhase = kTwoPi * n / static_cast<double>(tapCount);
        const double window = 0.355768
            - 0.487396 * std::cos(windowPhase)
            + 0.144232 * std::cos(2.0 * windowPhase)
            - 0.012604 * std::cos(3.0 * windowPhase);
        const double sinc = std::abs(t * omega) < 1.0e-12
            ? 1.0
            : std::sin(t * omega) / (t * omega);
        prototype[static_cast<std::size_t>(i)] = static_cast<float>(
            sinc * window * correction);
    }
    for (int i = 0; i < tapCount; ++i) {
        const int phase = (kClockInterpPhaseCount - 1)
            - (i % kClockInterpPhaseCount);
        const int tap = i / kClockInterpPhaseCount;
        m_clockInterpTaps[static_cast<std::size_t>(phase * kClockInterpTapCount + tap)]
            = prototype[static_cast<std::size_t>(i)];
    }
    m_clockBuffer.reserve(kRdsWorkBufferSamples + kClockInterpTapCount);
    reset();
    return true;
}

void RdsDecoder::reset() noexcept
{
    m_rdsDecimator.reset();
    m_referenceDecimator.reset();
    m_rdsInterpolator.reset();
    m_rdsAnalyticFilter.reset();
    m_subcarrierPhase = 0.0;
    m_referencePhase = 0.0;
    m_costasPhase = 0.0;
    m_costasFrequency = 0.0;
    m_clockCostasPhase = 0.0;
    m_clockCostasFrequency = m_rdsSampleRate > 0.0
        ? kTwoPi * (kRdsBitRate / m_rdsSampleRate) : 0.0;
    m_rdsAgcLevel = 0.0f;
    m_rdsResamplePhase = 0;
    m_subcarrierPower = 0.0;
    m_referencePower = 0.0;
    m_subcarrierToReferenceDb = -200.0f;
    m_clockPosition = 0.0;
    m_clockPhase = 0.0;
    m_clockOmega = m_clockNominalOmega;
    m_clockErrorAverage = 0.0;
    m_clockSymbolCount = 0;
    m_clockLocked = false;
    m_clockAcquiring = true;
    m_previousSymbol = Complex(0.0f, 0.0f);
    m_havePreviousSymbol = false;
    m_clockP0 = Complex(0.0f, 0.0f);
    m_clockP1 = Complex(0.0f, 0.0f);
    m_clockP2 = Complex(0.0f, 0.0f);
    m_clockC0 = Complex(0.0f, 0.0f);
    m_clockC1 = Complex(0.0f, 0.0f);
    m_clockC2 = Complex(0.0f, 0.0f);
    m_havePreviousClockSymbol = false;
    m_clockAccumulator = Complex(0.0f, 0.0f);
    m_clockSamples = 0;
    m_clockBuffer.clear();
    m_clockBuffer.assign(kClockInterpTapCount - 1, Complex(0.0f, 0.0f));
    m_shiftRegister = 0;
    m_skipBits = 0;
    m_bitPosition = 0;
    m_syncScore = 0;
    m_blockLocked = false;
    m_expectedBlock = BlockType::A;
    m_lastBlockType = BlockType::A;
    m_haveLastBlockType = false;
    m_groupStage = 0;
    m_groupA = 0;
    m_groupB = 0;
    m_groupC = 0;
    m_groupD = 0;
    m_groupCPrime = false;
    m_synced = false;
    m_bitsSinceValid = 0;
    m_bitsSinceGroup = 0;
    m_candidatePi = 0;
    m_validGroupStreak = 0;
    clearDecodedFields();
    m_validBlocks = 0;
}

void RdsDecoder::clearDecodedFields() noexcept
{
    m_piCode = 0;
    m_countryCode = 0;
    m_programCoverage = 0;
    m_programReferenceNumber = 0;
    m_programType = 0;
    m_trafficProgram = false;
    m_trafficAnnouncement = false;
    m_music = false;
    m_programService.fill(' ');
    m_radioText.fill(' ');
    m_alternateFrequencies.fill(0.0);
    m_alternateFrequencyCount = 0;
    m_radioTextAB = false;
}

void RdsDecoder::process(const float *mpx, std::size_t count) noexcept
{
    if (!mpx || count == 0 || m_sampleRate <= 0.0 || m_rdsMixed.empty())
        return;

    std::size_t offset = 0;
    while (offset < count) {
        const std::size_t chunk = std::min(m_rdsMixed.size(), count - offset);
        for (std::size_t i = 0; i < chunk; ++i) {
            const Complex oscillator(static_cast<float>(std::cos(m_subcarrierPhase)),
                                     static_cast<float>(-std::sin(m_subcarrierPhase)));
            m_rdsMixed[i] = Complex(mpx[offset + i], 0.0f) * oscillator;

            const Complex referenceOscillator(
                static_cast<float>(std::cos(m_referencePhase)),
                static_cast<float>(-std::sin(m_referencePhase)));
            m_referenceMixed[i] = Complex(mpx[offset + i], 0.0f) * referenceOscillator;

            m_subcarrierPhase += m_subcarrierOmega;
            if (m_subcarrierPhase > kPi)
                m_subcarrierPhase -= kTwoPi;
            m_referencePhase += m_referenceOmega;
            if (m_referencePhase > kPi)
                m_referencePhase -= kTwoPi;
        }

        const std::size_t decimated = m_rdsDecimator.process(
            m_rdsMixed.data(), chunk, m_rdsDecimated.data());
        const std::size_t referenceDecimated = m_referenceDecimator.process(
            m_referenceMixed.data(), chunk, m_referenceDecimated.data());
        const std::size_t powerSamples = std::min(decimated, referenceDecimated);
        for (std::size_t i = 0; i < powerSamples; ++i) {
            constexpr double kPowerSmoothing = 0.001;
            const double subcarrierPower = magnitudeSquared(m_rdsDecimated[i]);
            const double referencePower = magnitudeSquared(m_referenceDecimated[i]);
            m_subcarrierPower += (subcarrierPower - m_subcarrierPower) * kPowerSmoothing;
            m_referencePower += (referencePower - m_referencePower) * kPowerSmoothing;
        }
        if (m_referencePower > 1.0e-20) {
            m_subcarrierToReferenceDb = static_cast<float>(10.0 * std::log10(
                std::max(m_subcarrierPower, 1.0e-20) / m_referencePower));
        }
        const std::size_t interpolated = m_rdsInterpolator.process(
            m_rdsDecimated.data(), decimated, m_rdsInterpolated.data());
        for (std::size_t i = 0; i < interpolated; ++i) {
            if (m_rdsResamplePhase == 0) {
                processRdsSample(m_rdsInterpolated[i]);
            }
            m_rdsResamplePhase = (m_rdsResamplePhase + 1)
                % m_rdsResampleDecimation;
        }
        offset += chunk;
    }
}

void RdsDecoder::processRdsSample(Complex sample) noexcept
{
    const float magnitude = std::sqrt(magnitudeSquared(sample));
    m_rdsAgcLevel += (magnitude - m_rdsAgcLevel) * 0.01f;
    const float gain = std::clamp(0.5f / std::max(m_rdsAgcLevel, 1.0e-6f),
                                  0.1f, 100.0f);
    sample *= gain;

    const Complex oscillator(static_cast<float>(std::cos(m_costasPhase)),
                             static_cast<float>(-std::sin(m_costasPhase)));
    const Complex rotated = sample * oscillator;
    const float error = std::clamp(rotated.real() * rotated.imag(),
                                   -1.0f, 1.0f);
    m_costasFrequency += m_costasBeta * static_cast<double>(error);
    m_costasFrequency = std::clamp(m_costasFrequency,
                                   -m_costasMaxFrequency, m_costasMaxFrequency);
    m_costasPhase += m_costasFrequency + m_costasAlpha * error;
    if (m_costasPhase > kPi)
        m_costasPhase -= kTwoPi;
    else if (m_costasPhase < -kPi)
        m_costasPhase += kTwoPi;

    const Complex clockOscillator(
        static_cast<float>(std::cos(m_clockCostasPhase)),
        static_cast<float>(-std::sin(m_clockCostasPhase)));
    const Complex clockRotated = rotated * clockOscillator;
    const float clockError = std::clamp(
        clockRotated.real() * clockRotated.imag(), -1.0f, 1.0f);
    m_clockCostasFrequency += m_clockCostasBeta
        * static_cast<double>(clockError);
    m_clockCostasFrequency = std::clamp(m_clockCostasFrequency,
                                        m_clockCostasMinFrequency,
                                        m_clockCostasMaxFrequency);
    m_clockCostasPhase += m_clockCostasFrequency
        + m_clockCostasAlpha * static_cast<double>(clockError);
    if (m_clockCostasPhase > kPi)
        m_clockCostasPhase -= kTwoPi;
    else if (m_clockCostasPhase < -kPi)
        m_clockCostasPhase += kTwoPi;

    processClockSample(rotated);
}

void RdsDecoder::processClockSample(Complex sample) noexcept
{
    // The stream has already been resampled to exactly 5 kHz.  A bounded
    // integrate-and-dump clock is deliberately used here: it is phase-stable
    // for the 4.210526-sample RDS symbol period and avoids making the first
    // valid group depend on a long interpolation-buffer transient.
    m_clockAccumulator += sample;
    ++m_clockSamples;
    m_clockPhase += 1.0;
    if (m_clockPhase < m_clockOmega)
        return;

    const Complex symbol = m_clockAccumulator
        / static_cast<float>(std::max(1, m_clockSamples));
    m_clockAccumulator = Complex(0.0f, 0.0f);
    m_clockSamples = 0;
    m_clockPhase -= m_clockOmega;
    ++m_clockSymbolCount;
    if (m_clockSymbolCount >= 64)
        m_clockLocked = true;
    processSymbol(symbol);
}

void RdsDecoder::processSymbol(Complex symbol) noexcept
{
    if (!m_havePreviousSymbol) {
        m_previousSymbol = symbol;
        m_havePreviousSymbol = true;
        return;
    }

    // Differential BPSK removes the unknown absolute phase of the suppressed
    // 57 kHz carrier. A phase reversal is a one, no reversal is a zero.
    const Complex differential = std::conj(m_previousSymbol) * symbol;
    const std::uint8_t bit = differential.real() < 0.0f ? 1 : 0;
    m_previousSymbol = symbol;
    processBit(bit);
}

void RdsDecoder::processBit(std::uint8_t bit) noexcept
{
    m_shiftRegister = ((m_shiftRegister << 1) & 0x03ffffffu) | (bit & 1u);
    ++m_bitPosition;
    if (++m_bitsSinceGroup > 26u * 12u) {
        // A lock which did not produce a complete group is stale.  Restart
        // block acquisition as well as the public RDS lock.
        dropBlockSync();
        m_bitsSinceGroup = 0;
    }
    if (m_skipBits > 0) {
        --m_skipBits;
        return;
    }

    ++m_bitsSinceValid;
    if (m_bitsSinceValid > 26u * 12u)
        m_synced = false;

    BlockType type = BlockType::A;
    const std::uint16_t value = syndrome(m_shiftRegister);
    const bool knownType = blockTypeForSyndrome(value, type);
    if (knownType) {
        m_bitsSinceValid = 0;
        ++m_validBlocks;
    }

    // SDR++ keeps a small confidence score instead of requiring three
    // perfectly clean, consecutively spaced CRC words.  A real broadcast can
    // lose a complete block to multipath or a single bad symbol; discarding
    // the entire acquisition on that event prevents PI/PS from ever being
    // reconstructed. Unknown words are assigned the next expected type while
    // the score is still positive, exactly as a tolerant streaming decoder.
    m_syncScore = std::clamp(m_syncScore + (knownType ? 1 : -1), 0, 4);
    if (m_syncScore == 0) {
        if (m_blockLocked || m_synced)
            dropBlockSync();
        return;
    }

    m_blockLocked = true;
    const BlockType acceptedType = knownType ? type : m_expectedBlock;
    if (!acceptBlock(m_shiftRegister, acceptedType)) {
        m_groupStage = 0;
        return;
    }
    m_lastBlockType = acceptedType;
    m_haveLastBlockType = true;
    m_expectedBlock = nextBlockType(acceptedType);
    // Dopo un blocco valido il prossimo blocco termina 26 bit più avanti.
    // Durante questi bit il registro continua a scorrere, ma non cerchiamo
    // falsi sincronismi dentro il payload già noto.
    m_skipBits = 25;
}

void RdsDecoder::dropBlockSync() noexcept
{
    m_blockLocked = false;
    m_expectedBlock = BlockType::A;
    m_lastBlockType = BlockType::A;
    m_haveLastBlockType = false;
    m_syncScore = 0;
    m_skipBits = 0;
    m_synced = false;
    m_groupStage = 0;
    m_candidatePi = 0;
    m_validGroupStreak = 0;
    clearDecodedFields();
}

std::uint16_t RdsDecoder::syndrome(std::uint32_t block) noexcept
{
    std::uint16_t syn = 0;
    for (int i = 25; i >= 0; --i) {
        const std::uint8_t output = (syn >> 9) & 1u;
        syn = static_cast<std::uint16_t>((syn << 1) & 0x03ffu);
        syn ^= static_cast<std::uint16_t>(kLfsrPoly * output);
        syn ^= static_cast<std::uint16_t>(kInputPoly * ((block >> i) & 1u));
    }
    return syn;
}

bool RdsDecoder::blockTypeForSyndrome(std::uint16_t value, BlockType &type) noexcept
{
    switch (value) {
    case kSyndromeA:  type = BlockType::A;  return true;
    case kSyndromeB:  type = BlockType::B;  return true;
    case kSyndromeC:  type = BlockType::C;  return true;
    case kSyndromeCp: type = BlockType::Cp; return true;
    case kSyndromeD:  type = BlockType::D;  return true;
    default: return false;
    }
}

std::uint16_t RdsDecoder::offsetFor(BlockType type) noexcept
{
    switch (type) {
    case BlockType::A:  return kOffsetA;
    case BlockType::B:  return kOffsetB;
    case BlockType::C:  return kOffsetC;
    case BlockType::Cp: return kOffsetCp;
    case BlockType::D:  return kOffsetD;
    }
    return 0;
}

char RdsDecoder::printable(std::uint16_t value) noexcept
{
    return value >= 0x20 && value <= 0x7e ? static_cast<char>(value) : ' ';
}

const char *RdsDecoder::programTypeLabel(std::uint8_t type, RdsRegion region) noexcept
{
    // PTY europea RDS (IEC 62106), 0..31, e tabella RBDS nordamericana.
    static constexpr const char *european[] = {
        "None", "News", "Current Affairs", "Information", "Sport",
        "Education", "Drama", "Culture", "Science", "Varied",
        "Pop Music", "Rock Music", "Easy Listening", "Light Classical",
        "Serious Classical", "Other Music", "Weather", "Finance",
        "Children", "Social Affairs", "Religion", "Phone In", "Travel",
        "Leisure", "Jazz Music", "Country Music", "National Music",
        "Oldies Music", "Folk Music", "Documentary", "Alarm Test", "Alarm",
    };
    static constexpr const char *northAmerican[] = {
        "None", "News", "Information", "Sports", "Talk", "Rock",
        "Classic Rock", "Adult Hits", "Soft Rock", "Top 40", "Country",
        "Oldies", "Soft", "Nostalgia", "Jazz", "Classical", "Rhythm & Blues",
        "Soft R&B", "Language", "Religious Music", "Religious Talk",
        "Personality", "Public", "College", "Spanish Talk", "Spanish Music",
        "Hip Hop", "Weather", "Emergency Test", "Emergency", "Traffic",
        "Reserved",
    };
    const auto &labels = region == RdsRegion::NorthAmerica ? northAmerican : european;
    return type < std::size(labels) ? labels[type] : "Unknown";
}

std::string RdsDecoder::base26Callsign(std::uint16_t pi)
{
    // RBDS PI -> callsign conversion used by SDR++ for the normal US ranges.
    const bool westOfMississippi = pi >= 21672;
    std::string result(1, westOfMississippi ? 'W' : 'K');
    int value = static_cast<int>(pi) - (westOfMississippi ? 21672 : 4096);
    std::string rest;
    while (value > 0) {
        rest.push_back(static_cast<char>('A' + (value % 26)));
        value /= 26;
    }
    while (rest.size() < 3)
        rest.push_back('A');
    std::reverse(rest.begin(), rest.end());
    result += rest;
    return result;
}

std::string RdsDecoder::callsignForPi(std::uint16_t pi)
{
    // The AFXY and AXYZ forms encode the same base-26 call sign with a
    // shortened PI field; ordinary PI allocations use the direct mapping.
    if ((pi >> 8) == 0xAF)
        return base26Callsign(static_cast<std::uint16_t>((pi & 0xffu) << 8));
    if ((pi >> 12) == 0x0A)
        return base26Callsign(static_cast<std::uint16_t>(
            (((pi >> 8) & 0x0fu) << 12) | (pi & 0xffu)));
    if (pi >= 0x1000 && pi <= 0x994f
        && (pi & 0xffu) != 0 && ((pi >> 8) & 0x0fu) != 0)
        return base26Callsign(pi);
    return "Not Assigned";
}

bool RdsDecoder::acceptBlock(std::uint32_t block, BlockType type) noexcept
{
    // RDS uses a shortened BCH code.  Following SDR++, try to correct an
    // error in the 16 data bits before allowing a block into a group.  Check
    // bit errors do not affect the decoded payload and are already handled by
    // the offset correction below.
    std::uint32_t corrected = block ^ offsetFor(type);
    std::uint16_t residual = syndrome(corrected);
    if (residual != 0) {
        bool errorFound = false;
        for (int i = 15; i >= 0; --i) {
            errorFound = errorFound || !(residual & 0x001fu);
            const std::uint8_t output = (residual >> 9) & 1u;
            if (errorFound && output)
                corrected ^= static_cast<std::uint32_t>(1u << (i + 10));
            residual = static_cast<std::uint16_t>((residual << 1) & 0x03ffu);
            residual ^= static_cast<std::uint16_t>(kLfsrPoly * output
                                                    * !errorFound);
        }
        if (residual & 0x001fu)
            return false;
    }

    // La sequenza A-B-C(D)/D identifica un gruppo. Blocchi isolati non
    // aggiornano la UI, evitando PS e RadioText composti da dati disallineati.
    switch (type) {
    case BlockType::A:
        m_groupA = corrected;
        m_groupStage = 1;
        break;
    case BlockType::B:
        if (m_groupStage != 1) {
            m_groupStage = 0;
            break;
        }
        m_groupB = corrected;
        m_groupStage = 2;
        break;
    case BlockType::C:
    case BlockType::Cp:
        if (m_groupStage != 2) {
            m_groupStage = 0;
            break;
        }
        m_groupC = corrected;
        m_groupCPrime = type == BlockType::Cp;
        m_groupStage = 3;
        break;
    case BlockType::D:
        if (m_groupStage != 3) {
            m_groupStage = 0;
            break;
        }
        m_groupD = corrected;
        decodeGroup();
        m_groupStage = 0;
        m_bitsSinceGroup = 0;

        // Require two complete groups carrying the same non-reserved PI.  A
        // single group can still be a statistically valid CRC coincidence in
        // noise; consecutive groups make the green RDS indicator meaningful.
        const std::uint16_t pi = static_cast<std::uint16_t>(
            (m_groupA >> 10) & 0xffffu);
        if (pi == 0 || pi == 0xffff) {
            m_synced = false;
            m_candidatePi = 0;
            m_validGroupStreak = 0;
            clearDecodedFields();
        } else if (pi == m_candidatePi) {
            ++m_validGroupStreak;
            m_synced = m_validGroupStreak >= 2;
        } else {
            m_candidatePi = pi;
            m_validGroupStreak = 1;
            m_synced = false;
        }
        break;
    }
    return true;
}

void RdsDecoder::decodeGroup() noexcept
{
    const std::uint16_t blockA = static_cast<std::uint16_t>((m_groupA >> 10) & 0xffffu);
    const std::uint16_t blockB = static_cast<std::uint16_t>((m_groupB >> 10) & 0xffffu);
    const std::uint16_t blockC = static_cast<std::uint16_t>((m_groupC >> 10) & 0xffffu);
    const std::uint16_t blockD = static_cast<std::uint16_t>((m_groupD >> 10) & 0xffffu);

    m_piCode = blockA;
    m_countryCode = static_cast<std::uint8_t>((blockA >> 12) & 0x0f);
    m_programCoverage = static_cast<std::uint8_t>((blockA >> 8) & 0x0f);
    m_programReferenceNumber = static_cast<std::uint8_t>(blockA & 0xff);
    const int groupType = (blockB >> 12) & 0x0f;
    const bool versionB = (blockB >> 11) & 1;

    if (groupType == 0) {
        m_trafficProgram = (blockB >> 10) & 1;
        m_programType = static_cast<std::uint8_t>((blockB >> 5) & 0x1f);
        m_trafficAnnouncement = (blockB >> 4) & 1;
        m_music = (blockB >> 3) & 1;

        // In group 0A, C carries two eight-bit AF codes. Codes 1..204 map to
        // 87.6..107.9 MHz; other encodings are deliberately ignored until
        // the regional method is identified instead of showing bogus MHz.
        if (!versionB && !m_groupCPrime) {
            for (int shift : {8, 0}) {
                const int code = (blockC >> shift) & 0xff;
                if (code < 1 || code > 204)
                    continue;
                const double frequency = 87.5 + static_cast<double>(code) * 0.1;
                const bool duplicate = std::any_of(
                    m_alternateFrequencies.begin(),
                    m_alternateFrequencies.begin() + m_alternateFrequencyCount,
                    [frequency](double value) { return std::abs(value - frequency) < 0.001; });
                if (!duplicate && m_alternateFrequencyCount < m_alternateFrequencies.size())
                    m_alternateFrequencies[m_alternateFrequencyCount++] = frequency;
            }
        }

        const int address = blockB & 0x03;
        const int position = address * 2;
        if (position + 1 < static_cast<int>(m_programService.size())) {
            m_programService[static_cast<std::size_t>(position)] = printable(blockD >> 8);
            m_programService[static_cast<std::size_t>(position + 1)] = printable(blockD & 0xff);
        }
        return;
    }

    if (groupType != 2)
        return;

    const bool textAB = (blockB >> 4) & 1;
    if (textAB != m_radioTextAB) {
        m_radioText.fill(' ');
        m_radioTextAB = textAB;
    }

    const int address = blockB & (versionB ? 0x0f : 0x0f);
    if (!versionB) {
        const int position = address * 4;
        if (position + 3 < static_cast<int>(m_radioText.size())) {
            m_radioText[static_cast<std::size_t>(position)] = printable(blockC >> 8);
            m_radioText[static_cast<std::size_t>(position + 1)] = printable(blockC & 0xff);
            m_radioText[static_cast<std::size_t>(position + 2)] = printable(blockD >> 8);
            m_radioText[static_cast<std::size_t>(position + 3)] = printable(blockD & 0xff);
        }
    } else {
        const int position = address * 2;
        if (position + 1 < static_cast<int>(m_radioText.size())) {
            m_radioText[static_cast<std::size_t>(position)] = printable(blockD >> 8);
            m_radioText[static_cast<std::size_t>(position + 1)] = printable(blockD & 0xff);
        }
    }
}

std::string RdsDecoder::programService() const
{
    std::string result(m_programService.begin(), m_programService.end());
    while (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result;
}

std::string RdsDecoder::programTypeName() const
{
    return programTypeLabel(m_programType, m_region);
}

std::string RdsDecoder::programCoverageName() const
{
    static constexpr const char *labels[] = {
        "Local", "International", "National", "Supra-National",
        "Regional 1", "Regional 2", "Regional 3", "Regional 4",
        "Regional 5", "Regional 6", "Regional 7", "Regional 8",
        "Regional 9", "Regional 10", "Regional 11", "Regional 12",
    };
    return m_programCoverage < std::size(labels)
        ? labels[m_programCoverage] : "Unknown";
}

std::string RdsDecoder::callsign() const
{
    return m_region == RdsRegion::NorthAmerica
        ? callsignForPi(m_piCode) : std::string();
}

std::string RdsDecoder::alternateFrequencies() const
{
    std::string result;
    for (std::size_t i = 0; i < m_alternateFrequencyCount; ++i) {
        if (!result.empty())
            result += ", ";
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%.1f MHz", m_alternateFrequencies[i]);
        result += buffer;
    }
    return result;
}

std::string RdsDecoder::radioText() const
{
    std::string result(m_radioText.begin(), m_radioText.end());
    while (!result.empty() && (result.back() == ' ' || result.back() == '\r'))
        result.pop_back();
    return result;
}

} // namespace dsdr::dsp
