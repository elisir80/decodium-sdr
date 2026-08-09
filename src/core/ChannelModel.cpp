// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ChannelModel.h"

#include <algorithm>

namespace dsdr::core {

namespace {
/// Palette dei canali: tinte del tema DECODIUM, sufficientemente distanti fra
/// loro da restare distinguibili anche sovrapposte al waterfall.
const QColor kChannelColors[] = {
    QColor(0x4F, 0xC3, 0xF7), // azzurro
    QColor(0xFF, 0xB3, 0x4D), // ambra
    QColor(0x81, 0xC7, 0x84), // verde
    QColor(0xE5, 0x73, 0x73), // rosso
    QColor(0xBA, 0x86, 0xFC), // viola
    QColor(0x4D, 0xD0, 0xE1), // ciano
};
constexpr int kColorCount = static_cast<int>(std::size(kChannelColors));
} // namespace

ChannelModel::ChannelModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChannelModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant ChannelModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return QVariant();

    const ChannelEntry &entry = m_entries[static_cast<std::size_t>(index.row())];

    switch (role) {
    case ChannelIdRole:    return entry.id;
    case FrequencyRole:    return entry.frequencyHz;
    case OffsetRole:       return entry.settings.offsetHz;
    case ModeRole:         return static_cast<int>(entry.settings.mode);
    case ModeNameRole:     return demodModeName(entry.settings.mode);
    case FilterLowRole:    return entry.settings.filterLowHz;
    case FilterHighRole:   return entry.settings.filterHighHz;
    case AgcModeRole:      return static_cast<int>(entry.settings.agcMode);
    case AgcThresholdRole: return entry.settings.agcThresholdDb;
    case AgcAttackRole:    return entry.settings.agcAttackMs;
    case AgcDecayRole:     return entry.settings.agcDecayMs;
    case AmCarrierAgcRole: return entry.settings.amCarrierAgc;
    case VolumeRole:       return entry.settings.volume;
    case MutedRole:        return entry.settings.muted;
    case AudioHighPassEnabledRole: return entry.settings.audioHighPassEnabled;
    case AudioHighPassHzRole: return entry.settings.audioHighPassHz;
    case FmStereoRole:     return entry.settings.fmStereo;
    case FmAudioLowPassRole: return entry.settings.fmAudioLowPass;
    case FmDeemphasisRole: return entry.settings.fmDeemphasisUs;
    case FmRdsRole:        return entry.settings.fmRds;
    case RdsAutomaticAfRole: return entry.settings.rdsAutomaticAf;
    case RdsRegionRole:    return static_cast<int>(entry.settings.rdsRegion);
    case RdsSyncedRole:    return entry.rdsSynced;
    case RdsPiRole:        return entry.rdsPi;
    case RdsCountryCodeRole: return entry.rdsCountryCode;
    case RdsProgramCoverageRole: return entry.rdsProgramCoverage;
    case RdsReferenceNumberRole: return entry.rdsReferenceNumber;
    case RdsCallsignRole: return entry.rdsCallsign;
    case RdsProgramTypeRole: return entry.rdsProgramType;
    case RdsAlternateFrequenciesRole: return entry.rdsAlternateFrequencies;
    case RdsProgramServiceRole: return entry.rdsProgramService;
    case RdsRadioTextRole: return entry.rdsRadioText;
    case SquelchEnabledRole:   return entry.settings.squelchEnabled;
    case SquelchThresholdRole: return entry.settings.squelchThresholdDb;
    case CtcssEnabledRole:     return entry.settings.ctcssEnabled;
    case CtcssDecodeOnlyRole:  return entry.settings.ctcssDecodeOnly;
    case CtcssToneRole:        return entry.settings.ctcssToneHz;
    case NoiseBlankerEnabledRole:   return entry.settings.noiseBlankerEnabled;
    case NoiseBlankerThresholdRole: return entry.settings.noiseBlankerThresholdDb;
    case FmIfNoiseReductionEnabledRole: return entry.settings.fmIfNoiseReductionEnabled;
    case FmIfNoiseReductionPresetRole: return entry.settings.fmIfNoiseReductionPreset;
    case SignalDbRole:     return entry.signalDb;
    case NoiseFloorDbRole: return entry.noiseFloorDb;
    case SnrDbRole:       return entry.snrDb;
    case AudioLevelDbRole: return entry.audioLevelDb;
    case AgcGainDbRole:    return entry.agcGainDb;
    case ColorRole:        return entry.color;
    case LabelRole:        return entry.label;
    default:               return QVariant();
    }
}

QHash<int, QByteArray> ChannelModel::roleNames() const
{
    return {
        {ChannelIdRole, "channelId"},
        {FrequencyRole, "frequencyHz"},
        {OffsetRole, "offsetHz"},
        {ModeRole, "mode"},
        {ModeNameRole, "modeName"},
        {FilterLowRole, "filterLowHz"},
        {FilterHighRole, "filterHighHz"},
        {AgcModeRole, "agcMode"},
        {AgcThresholdRole, "agcThresholdDb"},
        {AgcAttackRole, "agcAttackMs"},
        {AgcDecayRole, "agcDecayMs"},
        {AmCarrierAgcRole, "amCarrierAgc"},
        {VolumeRole, "volume"},
        {MutedRole, "muted"},
        {AudioHighPassEnabledRole, "audioHighPassEnabled"},
        {AudioHighPassHzRole, "audioHighPassHz"},
        {FmStereoRole, "fmStereo"},
        {FmAudioLowPassRole, "fmAudioLowPass"},
        {FmDeemphasisRole, "fmDeemphasisUs"},
        {FmRdsRole, "fmRds"},
        {RdsAutomaticAfRole, "rdsAutomaticAf"},
        {RdsRegionRole, "rdsRegion"},
        {RdsSyncedRole, "rdsSynced"},
        {RdsPiRole, "rdsPi"},
        {RdsCountryCodeRole, "rdsCountryCode"},
        {RdsProgramCoverageRole, "rdsProgramCoverage"},
        {RdsReferenceNumberRole, "rdsReferenceNumber"},
        {RdsCallsignRole, "rdsCallsign"},
        {RdsProgramTypeRole, "rdsProgramType"},
        {RdsAlternateFrequenciesRole, "rdsAlternateFrequencies"},
        {RdsProgramServiceRole, "rdsProgramService"},
        {RdsRadioTextRole, "rdsRadioText"},
        {SquelchEnabledRole, "squelchEnabled"},
        {SquelchThresholdRole, "squelchThresholdDb"},
        {CtcssEnabledRole, "ctcssEnabled"},
        {CtcssDecodeOnlyRole, "ctcssDecodeOnly"},
        {CtcssToneRole, "ctcssToneHz"},
        {NoiseBlankerEnabledRole, "noiseBlankerEnabled"},
        {NoiseBlankerThresholdRole, "noiseBlankerThresholdDb"},
        {FmIfNoiseReductionEnabledRole, "fmIfNoiseReductionEnabled"},
        {FmIfNoiseReductionPresetRole, "fmIfNoiseReductionPreset"},
        {SignalDbRole, "signalDb"},
        {NoiseFloorDbRole, "noiseFloorDb"},
        {SnrDbRole, "snrDb"},
        {AudioLevelDbRole, "audioLevelDb"},
        {AgcGainDbRole, "agcGainDb"},
        {ColorRole, "channelColor"},
        {LabelRole, "label"},
    };
}

void ChannelModel::setCurrentIndex(int index)
{
    if (index < -1 || index >= rowCount())
        index = rowCount() > 0 ? 0 : -1;
    if (m_currentIndex == index)
        return;
    m_currentIndex = index;
    emit currentIndexChanged();
}

int ChannelModel::append(const ChannelEntry &entry)
{
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_entries.push_back(entry);
    endInsertRows();

    emit countChanged();
    if (m_currentIndex < 0)
        setCurrentIndex(row);
    return row;
}

void ChannelModel::removeAt(int row)
{
    if (row < 0 || row >= rowCount())
        return;

    beginRemoveRows(QModelIndex(), row, row);
    m_entries.erase(m_entries.begin() + row);
    endRemoveRows();

    emit countChanged();

    if (m_entries.empty()) {
        m_currentIndex = -1;
        emit currentIndexChanged();
    } else if (m_currentIndex >= rowCount()) {
        m_currentIndex = rowCount() - 1;
        emit currentIndexChanged();
    }
}

void ChannelModel::clear()
{
    if (m_entries.empty())
        return;
    beginResetModel();
    m_entries.clear();
    m_currentIndex = -1;
    endResetModel();
    emit countChanged();
    emit currentIndexChanged();
}

int ChannelModel::indexOf(ChannelId id) const
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [id](const ChannelEntry &e) { return e.id == id; });
    return it == m_entries.end() ? -1 : static_cast<int>(std::distance(m_entries.begin(), it));
}

const ChannelEntry *ChannelModel::at(int row) const
{
    if (row < 0 || row >= rowCount())
        return nullptr;
    return &m_entries[static_cast<std::size_t>(row)];
}

ChannelEntry *ChannelModel::mutableAt(int row)
{
    if (row < 0 || row >= rowCount())
        return nullptr;
    return &m_entries[static_cast<std::size_t>(row)];
}

void ChannelModel::entryChanged(int row, const QList<int> &roles)
{
    if (row < 0 || row >= rowCount())
        return;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, roles);
}

void ChannelModel::updateMeters(ChannelId id, float signalDb, float noiseFloorDb,
                                float snrDb, float audioLevelDb, float agcGainDb)
{
    const int row = indexOf(id);
    if (row < 0)
        return;

    ChannelEntry &entry = m_entries[static_cast<std::size_t>(row)];
    entry.signalDb = signalDb;
    entry.noiseFloorDb = noiseFloorDb;
    entry.snrDb = snrDb;
    entry.audioLevelDb = audioLevelDb;
    entry.agcGainDb = agcGainDb;
    entryChanged(row, {SignalDbRole, NoiseFloorDbRole, SnrDbRole,
                       AudioLevelDbRole, AgcGainDbRole});
}

void ChannelModel::updateRds(ChannelId id, bool synced, const QString &pi,
                             int countryCode, int programCoverage, int referenceNumber,
                             const QString &callsign,
                             const QString &programType, const QString &alternateFrequencies,
                             const QString &programService, const QString &radioText)
{
    const int row = indexOf(id);
    if (row < 0)
        return;

    ChannelEntry &entry = m_entries[static_cast<std::size_t>(row)];
    if (entry.rdsSynced == synced && entry.rdsPi == pi
        && entry.rdsCountryCode == countryCode
        && entry.rdsProgramCoverage == programCoverage
        && entry.rdsReferenceNumber == referenceNumber
        && entry.rdsCallsign == callsign
        && entry.rdsProgramType == programType
        && entry.rdsAlternateFrequencies == alternateFrequencies
        && entry.rdsProgramService == programService
        && entry.rdsRadioText == radioText)
        return;

    entry.rdsSynced = synced;
    entry.rdsPi = pi;
    entry.rdsCountryCode = countryCode;
    entry.rdsProgramCoverage = programCoverage;
    entry.rdsReferenceNumber = referenceNumber;
    entry.rdsCallsign = callsign;
    entry.rdsProgramType = programType;
    entry.rdsAlternateFrequencies = alternateFrequencies;
    entry.rdsProgramService = programService;
    entry.rdsRadioText = radioText;
    entryChanged(row, {RdsSyncedRole, RdsPiRole, RdsCountryCodeRole,
                       RdsProgramCoverageRole, RdsReferenceNumberRole,
                       RdsCallsignRole, RdsProgramTypeRole,
                       RdsAlternateFrequenciesRole, RdsProgramServiceRole,
                       RdsRadioTextRole});
}

QColor ChannelModel::nextColor() const
{
    return kChannelColors[rowCount() % kColorCount];
}

} // namespace dsdr::core
