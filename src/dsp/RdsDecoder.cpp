// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/RdsDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

namespace dsdr::dsp {

namespace {
constexpr double kRdsSubcarrierHz = 57'000.0;
constexpr double kRdsBitRate = 1187.5;
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

bool RdsDecoder::configure(double sampleRate)
{
    if (sampleRate < 120'000.0)
        return false;

    m_sampleRate = sampleRate;
    m_subcarrierOmega = kTwoPi * kRdsSubcarrierHz / sampleRate;
    m_symbolPeriod = sampleRate / kRdsBitRate;
    m_lowpassAlpha = 1.0 - std::exp(-kTwoPi * 3500.0 / sampleRate);
    reset();
    return true;
}

void RdsDecoder::reset() noexcept
{
    m_subcarrierPhase = 0.0;
    m_symbolClock = 0.0;
    m_symbolSamples = 0;
    m_lowpass = Complex(0.0f, 0.0f);
    m_symbolAccumulator = Complex(0.0f, 0.0f);
    m_previousSymbol = Complex(0.0f, 0.0f);
    m_havePreviousSymbol = false;
    m_shiftRegister = 0;
    m_skipBits = 0;
    m_groupStage = 0;
    m_groupA = 0;
    m_groupB = 0;
    m_groupC = 0;
    m_groupD = 0;
    m_groupCPrime = false;
    m_synced = false;
    m_bitsSinceValid = 0;
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
    m_validBlocks = 0;
}

void RdsDecoder::process(const float *mpx, std::size_t count) noexcept
{
    if (!mpx || count == 0 || m_sampleRate <= 0.0)
        return;

    for (std::size_t i = 0; i < count; ++i) {
        const Complex oscillator(static_cast<float>(std::cos(m_subcarrierPhase)),
                                 static_cast<float>(-std::sin(m_subcarrierPhase)));
        const Complex mixed = Complex(mpx[i], 0.0f) * oscillator;
        m_lowpass += static_cast<float>(m_lowpassAlpha) * (mixed - m_lowpass);
        m_symbolAccumulator += m_lowpass;
        ++m_symbolSamples;

        m_subcarrierPhase += m_subcarrierOmega;
        if (m_subcarrierPhase > kPi)
            m_subcarrierPhase -= kTwoPi;

        m_symbolClock += 1.0;
        if (m_symbolClock < m_symbolPeriod)
            continue;

        m_symbolClock -= m_symbolPeriod;
        const float denominator = static_cast<float>(std::max<std::size_t>(1, m_symbolSamples));
        processSymbol(m_symbolAccumulator / denominator);
        m_symbolAccumulator = Complex(0.0f, 0.0f);
        m_symbolSamples = 0;
    }
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
    if (m_skipBits > 0) {
        --m_skipBits;
        return;
    }

    ++m_bitsSinceValid;
    if (m_bitsSinceValid > 26u * 12u)
        m_synced = false;

    BlockType type;
    const std::uint16_t value = syndrome(m_shiftRegister);
    if (!blockTypeForSyndrome(value, type))
        return;

    m_bitsSinceValid = 0;
    m_synced = true;
    ++m_validBlocks;
    acceptBlock(m_shiftRegister, type);
    // Dopo un blocco valido il prossimo blocco termina 26 bit più avanti.
    // Durante questi bit il registro continua a scorrere, ma non cerchiamo
    // falsi sincronismi dentro il payload già noto.
    m_skipBits = 25;
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

void RdsDecoder::acceptBlock(std::uint32_t block, BlockType type) noexcept
{
    const std::uint32_t corrected = block ^ offsetFor(type);

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
        break;
    }
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
