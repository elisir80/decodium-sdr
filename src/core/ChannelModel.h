// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — modello dei canali RX esposto a QML.
//
// I "flag VFO" colorati sullo spettro e la channel strip laterale sono due
// viste dello stesso modello: spostare un flag e cambiare la frequenza nella
// strip passano dallo stesso percorso.
#pragma once

#include "dsp/ChannelProcessor.h"

#include <QAbstractListModel>
#include <QColor>

#include <vector>

namespace dsdr::core {

struct ChannelEntry
{
    ChannelId id = kInvalidChannel;
    qint64 frequencyHz = 0;
    dsp::ChannelSettings settings;
    QString label;
    QColor color;
    float signalDb = -140.0f;
    float noiseFloorDb = -140.0f;
    float snrDb = 0.0f;
    float audioLevelDb = -140.0f;
    float agcGainDb = 0.0f;
    bool rdsSynced = false;
    QString rdsPi;
    int rdsCountryCode = -1;
    int rdsProgramCoverage = -1;
    int rdsReferenceNumber = -1;
    QString rdsCallsign;
    QString rdsProgramType;
    QString rdsAlternateFrequencies;
    QString rdsProgramService;
    QString rdsRadioText;
};

class ChannelModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)

public:
    enum Roles {
        ChannelIdRole = Qt::UserRole + 1,
        FrequencyRole,
        OffsetRole,
        ModeRole,
        ModeNameRole,
        FilterLowRole,
        FilterHighRole,
        AgcModeRole,
        AgcThresholdRole,
        AgcAttackRole,
        AgcDecayRole,
        AmCarrierAgcRole,
        VolumeRole,
        MutedRole,
        AudioHighPassEnabledRole,
        AudioHighPassHzRole,
        FmStereoRole,
        FmAudioLowPassRole,
        FmDeemphasisRole,
        FmRdsRole,
        RdsAutomaticAfRole,
        RdsRegionRole,
        RdsSyncedRole,
        RdsPiRole,
        RdsCountryCodeRole,
        RdsProgramCoverageRole,
        RdsReferenceNumberRole,
        RdsCallsignRole,
        RdsProgramTypeRole,
        RdsAlternateFrequenciesRole,
        RdsProgramServiceRole,
        RdsRadioTextRole,
        SquelchEnabledRole,
        SquelchThresholdRole,
        CtcssEnabledRole,
        CtcssDecodeOnlyRole,
        CtcssToneRole,
        NoiseBlankerEnabledRole,
        NoiseBlankerThresholdRole,
        FmIfNoiseReductionEnabledRole,
        FmIfNoiseReductionPresetRole,
        SignalDbRole,
        NoiseFloorDbRole,
        SnrDbRole,
        AudioLevelDbRole,
        AgcGainDbRole,
        ColorRole,
        LabelRole,
    };
    Q_ENUM(Roles)

    explicit ChannelModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);

    // ── API per il SessionManager ────────────────────────────────────────

    int append(const ChannelEntry &entry);
    void removeAt(int row);
    void clear();

    int indexOf(ChannelId id) const;
    const ChannelEntry *at(int row) const;
    ChannelEntry *mutableAt(int row);

    /// Notifica il cambio di un'entry già modificata in place.
    void entryChanged(int row, const QList<int> &roles = {});

    void updateMeters(ChannelId id, float signalDb, float noiseFloorDb,
                      float snrDb, float audioLevelDb, float agcGainDb);
    void updateRds(ChannelId id, bool synced, const QString &pi,
                   int countryCode, int programCoverage, int referenceNumber,
                   const QString &callsign,
                   const QString &programType, const QString &alternateFrequencies,
                   const QString &programService, const QString &radioText);

    /// Colore successivo della palette dei canali (stile slice).
    QColor nextColor() const;

signals:
    void countChanged();
    void currentIndexChanged();

private:
    std::vector<ChannelEntry> m_entries;
    int m_currentIndex = -1;
};

} // namespace dsdr::core
