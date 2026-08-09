// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — decoder RDS minimale per il multiplex Wide-FM.
//
// Il decoder lavora sul multiplex dopo il discriminatore FM: trasla il
// sottoportante a 57 kHz, recupera i simboli BPSK a 1187.5 bit/s e verifica i
// blocchi RDS con il codice CRC-10. La logica è autonoma e non dipende da Qt,
// così può essere testata anche senza la UI.
#pragma once

#include "dsp/DspTypes.h"
#include "common/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dsdr::dsp {

class RdsDecoder
{
public:
    bool configure(double sampleRate);
    void reset() noexcept;

    /// Processa il multiplex Wide-FM, che contiene anche il ramo RDS a 57 kHz.
    void process(const float *mpx, std::size_t count) noexcept;

    bool configured() const noexcept { return m_sampleRate > 0.0; }
    void setRegion(RdsRegion region) noexcept { m_region = region; }
    RdsRegion region() const noexcept { return m_region; }
    bool synced() const noexcept { return m_synced; }
    std::uint16_t piCode() const noexcept { return m_piCode; }
    std::uint8_t countryCode() const noexcept { return m_countryCode; }
    std::uint8_t programCoverage() const noexcept { return m_programCoverage; }
    std::uint8_t programReferenceNumber() const noexcept { return m_programReferenceNumber; }
    std::uint8_t programType() const noexcept { return m_programType; }
    bool trafficProgram() const noexcept { return m_trafficProgram; }
    bool trafficAnnouncement() const noexcept { return m_trafficAnnouncement; }
    bool music() const noexcept { return m_music; }
    std::string programTypeName() const;
    std::string programCoverageName() const;
    std::string callsign() const;
    std::string alternateFrequencies() const;
    std::string programService() const;
    std::string radioText() const;
    std::uint64_t validBlocks() const noexcept { return m_validBlocks; }

private:
    enum class BlockType {
        A,
        B,
        C,
        Cp,
        D,
    };

    static std::uint16_t syndrome(std::uint32_t block) noexcept;
    static bool blockTypeForSyndrome(std::uint16_t value, BlockType &type) noexcept;
    static std::uint16_t offsetFor(BlockType type) noexcept;
    static char printable(std::uint16_t value) noexcept;
    static const char *programTypeLabel(std::uint8_t type, RdsRegion region) noexcept;
    static std::string callsignForPi(std::uint16_t pi);
    static std::string base26Callsign(std::uint16_t pi);

    void processSymbol(Complex symbol) noexcept;
    void processBit(std::uint8_t bit) noexcept;
    void acceptBlock(std::uint32_t block, BlockType type) noexcept;
    void decodeGroup() noexcept;

    double m_sampleRate = 0.0;
    double m_subcarrierOmega = 0.0;
    double m_subcarrierPhase = 0.0;
    double m_symbolPeriod = 0.0;
    double m_symbolClock = 0.0;
    double m_lowpassAlpha = 0.0;
    std::size_t m_symbolSamples = 0;
    Complex m_lowpass{0.0f, 0.0f};
    Complex m_symbolAccumulator{0.0f, 0.0f};
    Complex m_previousSymbol{0.0f, 0.0f};
    bool m_havePreviousSymbol = false;

    std::uint32_t m_shiftRegister = 0;
    int m_skipBits = 0;
    int m_groupStage = 0;
    std::uint32_t m_groupA = 0;
    std::uint32_t m_groupB = 0;
    std::uint32_t m_groupC = 0;
    std::uint32_t m_groupD = 0;
    bool m_groupCPrime = false;
    bool m_synced = false;
    std::uint32_t m_bitsSinceValid = 0;
    std::uint16_t m_piCode = 0;
    std::uint8_t m_countryCode = 0;
    std::uint8_t m_programCoverage = 0;
    std::uint8_t m_programReferenceNumber = 0;
    std::uint8_t m_programType = 0;
    bool m_trafficProgram = false;
    bool m_trafficAnnouncement = false;
    bool m_music = false;
    std::array<char, 8> m_programService{' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    std::array<char, 64> m_radioText{};
    std::array<double, 8> m_alternateFrequencies{};
    std::size_t m_alternateFrequencyCount = 0;
    bool m_radioTextAB = false;
    std::uint64_t m_validBlocks = 0;
    RdsRegion m_region = RdsRegion::Europe;
};

} // namespace dsdr::dsp
