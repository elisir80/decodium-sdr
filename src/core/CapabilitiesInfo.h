// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — capability del backend attivo, in forma bindabile da QML.
//
// È l'oggetto da cui la UI si genera (§4.2): `Session.capabilities.canTransmit`
// decide se il PTT esiste, `coherentRx` se compare il pannello QuadBeam.
// CONSTITUTION §7: in QML non deve mai comparire un confronto sul backendId.
#pragma once

#include "hal/BackendCapabilities.h"

#include <QObject>
#include <QVariantList>

namespace dsdr::core {

class CapabilitiesInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int maxRxChannels READ maxRxChannels NOTIFY changed)
    Q_PROPERTY(bool coherentRx READ coherentRx NOTIFY changed)
    Q_PROPERTY(int maxPanadapters READ maxPanadapters NOTIFY changed)
    Q_PROPERTY(bool canTransmit READ canTransmit NOTIFY changed)
    Q_PROPERTY(bool fullDuplex READ fullDuplex NOTIFY changed)
    Q_PROPERTY(bool clientDemod READ clientDemod NOTIFY changed)
    Q_PROPERTY(bool clientModulation READ clientModulation NOTIFY changed)
    Q_PROPERTY(bool clientSpectrum READ clientSpectrum NOTIFY changed)
    Q_PROPERTY(bool clientAgc READ clientAgc NOTIFY changed)
    Q_PROPERTY(QVariantList sampleRates READ sampleRates NOTIFY changed)
    Q_PROPERTY(qint64 minFrequency READ minFrequency NOTIFY changed)
    Q_PROPERTY(qint64 maxFrequency READ maxFrequency NOTIFY changed)
    Q_PROPERTY(bool hasPreamp READ hasPreamp NOTIFY changed)
    Q_PROPERTY(bool hasAttenuator READ hasAttenuator NOTIFY changed)
    Q_PROPERTY(double maxGainReduction READ maxGainReduction NOTIFY changed)
    Q_PROPERTY(bool remoteCapable READ remoteCapable NOTIFY changed)
    Q_PROPERTY(bool supportsRecording READ supportsRecording NOTIFY changed)
    Q_PROPERTY(QStringList nativePanels READ nativePanels NOTIFY changed)
    Q_PROPERTY(bool manualDeviceEntry READ manualDeviceEntry NOTIFY changed)

public:
    explicit CapabilitiesInfo(QObject *parent = nullptr) : QObject(parent) {}

    void setCapabilities(const hal::BackendCapabilities &capabilities)
    {
        m_capabilities = capabilities;
        emit changed();
    }

    const hal::BackendCapabilities &raw() const noexcept { return m_capabilities; }

    int maxRxChannels() const { return m_capabilities.maxRxChannels; }
    bool coherentRx() const { return m_capabilities.coherentRx; }
    int maxPanadapters() const { return m_capabilities.maxPanadapters; }
    bool canTransmit() const { return m_capabilities.canTransmit(); }
    bool fullDuplex() const { return m_capabilities.tx == TxSupport::FullDuplex; }
    bool clientDemod() const { return m_capabilities.demod == DspLocation::Client; }
    bool clientModulation() const { return m_capabilities.modulation == DspLocation::Client; }
    bool clientSpectrum() const { return m_capabilities.spectrum == DspLocation::Client; }
    bool clientAgc() const { return m_capabilities.agc == DspLocation::Client; }
    qint64 minFrequency() const { return m_capabilities.minFrequencyHz; }
    qint64 maxFrequency() const { return m_capabilities.maxFrequencyHz; }
    bool hasPreamp() const { return m_capabilities.hasPreamp; }
    double maxGainReduction() const { return m_capabilities.maxGainReductionDb; }
    bool hasAttenuator() const { return m_capabilities.hasAttenuator; }
    bool remoteCapable() const { return m_capabilities.remoteCapable; }
    bool supportsRecording() const { return m_capabilities.supportsRecording; }
    QStringList nativePanels() const { return m_capabilities.nativePanels; }
    bool manualDeviceEntry() const { return m_capabilities.manualDeviceEntry; }

    QVariantList sampleRates() const
    {
        QVariantList list;
        list.reserve(m_capabilities.sampleRates.size());
        for (double rate : m_capabilities.sampleRates)
            list.append(rate);
        return list;
    }

signals:
    void changed();

private:
    hal::BackendCapabilities m_capabilities;
};

} // namespace dsdr::core
