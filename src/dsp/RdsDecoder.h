// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — decoder RDS minimale per il multiplex Wide-FM.
//
// Il decoder lavora sul multiplex dopo il discriminatore FM: trasla il
// sottoportante a 57 kHz, recupera i simboli BPSK a 1187.5 bit/s e verifica i
// blocchi RDS con il codice CRC-10. La logica è autonoma e non dipende da Qt,
// così può essere testata anche senza la UI.
#pragma once

#include "dsp/DspTypes.h"
#include "dsp/DecimatorChain.h"
#include "dsp/ComplexFir.h"
#include "dsp/InterpolatorChain.h"
#include "common/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dsdr::dsp {

class RdsDecoder
{
public:
    bool configure(double sampleRate);
    void reset() noexcept;

    /// Processa il multiplex Wide-FM, che contiene anche il ramo RDS a 57 kHz.
    void process(const float *mpx, std::size_t count) noexcept;

    bool configured() const noexcept { return m_sampleRate > 0.0; }
    bool timingLocked() const noexcept { return m_clockLocked; }
    double clockSamplesPerSymbol() const noexcept { return m_clockOmega; }
    bool blockLocked() const noexcept { return m_blockLocked; }
    int syncScore() const noexcept { return m_syncScore; }
    int validGroupStreak() const noexcept { return m_validGroupStreak; }
    /// Rapporto fra energia nella banda RDS (57 kHz) e una banda di
    /// riferimento adiacente (63.5 kHz), misurato prima dell'AGC RDS.
    /// Valori prossimi a 0 dB indicano che non emerge alcuna sottoportante
    /// RDS dal rumore del multiplex.
    float subcarrierToReferenceDb() const noexcept { return m_subcarrierToReferenceDb; }
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
    static BlockType nextBlockType(BlockType type) noexcept;
    static char printable(std::uint16_t value) noexcept;
    static const char *programTypeLabel(std::uint8_t type, RdsRegion region) noexcept;
    static std::string callsignForPi(std::uint16_t pi);
    static std::string base26Callsign(std::uint16_t pi);

    void processRdsSample(Complex sample) noexcept;
    void processClockSample(Complex sample) noexcept;
    void processSymbol(Complex symbol) noexcept;
    void processBit(std::uint8_t bit) noexcept;
    void dropBlockSync() noexcept;
    bool acceptBlock(std::uint32_t block, BlockType type) noexcept;
    void decodeGroup() noexcept;
    void clearDecodedFields() noexcept;

    double m_sampleRate = 0.0;
    double m_subcarrierOmega = 0.0;
    double m_subcarrierPhase = 0.0;
    double m_referenceOmega = 0.0;
    double m_referencePhase = 0.0;
    double m_rdsSampleRate = 0.0;
    double m_symbolPeriod = 0.0;
    double m_costasPhase = 0.0;
    double m_costasFrequency = 0.0;
    double m_costasAlpha = 0.0;
    double m_costasBeta = 0.0;
    double m_costasMaxFrequency = 0.0;
    double m_clockCostasPhase = 0.0;
    double m_clockCostasFrequency = 0.0;
    double m_clockCostasAlpha = 0.0;
    double m_clockCostasBeta = 0.0;
    double m_clockCostasMinFrequency = 0.0;
    double m_clockCostasMaxFrequency = 0.0;
    float m_rdsAgcLevel = 0.0f;
    double m_clockPosition = 0.0;
    double m_clockOmega = 0.0;
    double m_clockNominalOmega = 0.0;
    double m_clockOmegaGain = 0.0;
    double m_clockMuGain = 0.0;
    double m_clockErrorAverage = 0.0;
    std::uint64_t m_clockSymbolCount = 0;
    bool m_clockLocked = false;
    bool m_clockAcquiring = true;
    Complex m_previousSymbol{0.0f, 0.0f};
    bool m_havePreviousSymbol = false;
    Complex m_clockP0{0.0f, 0.0f};
    Complex m_clockP1{0.0f, 0.0f};
    Complex m_clockP2{0.0f, 0.0f};
    Complex m_clockC0{0.0f, 0.0f};
    Complex m_clockC1{0.0f, 0.0f};
    Complex m_clockC2{0.0f, 0.0f};
    bool m_havePreviousClockSymbol = false;
    Complex m_clockAccumulator{0.0f, 0.0f};
    int m_clockSamples = 0;

    DecimatorChain m_rdsDecimator;
    DecimatorChain m_referenceDecimator;
    InterpolatorChain m_rdsInterpolator;
    ComplexFir m_rdsAnalyticFilter;
    std::vector<Complex> m_rdsMixed;
    std::vector<Complex> m_referenceMixed;
    std::vector<Complex> m_rdsDecimated;
    std::vector<Complex> m_referenceDecimated;
    std::vector<Complex> m_rdsInterpolated;
    std::vector<Complex> m_clockBuffer;
    std::vector<float> m_clockInterpTaps;
    double m_clockPhase = 0.0;
    int m_rdsResamplePhase = 0;
    int m_rdsResampleDecimation = 1;
    double m_subcarrierPower = 0.0;
    double m_referencePower = 0.0;
    float m_subcarrierToReferenceDb = -200.0f;

    std::uint32_t m_shiftRegister = 0;
    int m_skipBits = 0;
    std::uint64_t m_bitPosition = 0;
    int m_syncScore = 0;
    bool m_blockLocked = false;
    BlockType m_expectedBlock = BlockType::A;
    BlockType m_lastBlockType = BlockType::A;
    bool m_haveLastBlockType = false;
    int m_groupStage = 0;
    std::uint32_t m_groupA = 0;
    std::uint32_t m_groupB = 0;
    std::uint32_t m_groupC = 0;
    std::uint32_t m_groupD = 0;
    bool m_groupCPrime = false;
    bool m_synced = false;
    std::uint32_t m_bitsSinceValid = 0;
    std::uint32_t m_bitsSinceGroup = 0;
    std::uint16_t m_candidatePi = 0;
    int m_validGroupStreak = 0;
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
