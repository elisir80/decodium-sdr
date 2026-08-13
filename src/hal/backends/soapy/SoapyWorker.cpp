// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/soapy/SoapyWorker.h"

#include "dsp/SpscRing.h"
#include "hal/HalLog.h"

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Errors.hpp>
#include <SoapySDR/Formats.hpp>

#include <algorithm>
#include <cmath>

namespace dsdr::hal::soapy {

namespace {

/// Campioni letti per chiamata. A 2,4 MS/s sono ~7 ms: abbastanza per non
/// pagare il costo della chiamata a ogni manciata di campioni, abbastanza poco
/// da reagire in fretta a un comando.
constexpr std::size_t kReadFrames = 16384;

/// Timeout di readStream. Determina anche la latenza con cui il ciclo si
/// accorge di una richiesta di arresto.
constexpr long kReadTimeoutUs = 200000;

QString describeError(int code)
{
    return QString::fromUtf8(SoapySDR::errToStr(code));
}

} // namespace

SoapyWorker::SoapyWorker(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    m_buffer.resize(kReadFrames * 2);
}

SoapyWorker::~SoapyWorker() = default;

void SoapyWorker::requestFrequency(qint64 hz)
{
    m_pendingFrequency.store(hz, std::memory_order_release);
}

void SoapyWorker::requestSampleRate(double rate)
{
    m_pendingSampleRate.store(rate, std::memory_order_release);
}

void SoapyWorker::requestGain(double db)
{
    if (std::isnan(db) || db < 0.0) {
        m_gainAuto.store(true, std::memory_order_release);
    } else {
        m_gainAuto.store(false, std::memory_order_release);
        m_pendingGain.store(db, std::memory_order_release);
    }
    // Il comando deve essere applicato una sola volta.  Lasciare il solo
    // booleano m_gainAuto come condizione faceva riscrivere il gain a ogni
    // readStream(): il driver Soapy veniva bloccato, il ring IQ si svuotava e
    // l'audio entrava in underrun.
    m_gainCommandPending.store(true, std::memory_order_release);
}

void SoapyWorker::requestAntenna(int index)
{
    m_pendingAntenna.store(index, std::memory_order_release);
}

void SoapyWorker::requestStop()
{
    m_running.store(false, std::memory_order_release);
}

SoapyDeviceProfile SoapyWorker::readProfile(SoapySDR::Device *device,
                                            const QString &deviceArgs) const
{
    SoapyDeviceProfile profile;
    profile.deviceArgs = deviceArgs;

    const auto info = device->getHardwareInfo();
    const auto readInfo = [&info](const char *key) {
        const auto it = info.find(key);
        return it == info.end() ? QString() : QString::fromStdString(it->second);
    };

    profile.driver = QString::fromStdString(device->getDriverKey());
    profile.hardware = QString::fromStdString(device->getHardwareKey());
    profile.serial = readInfo("serial");
    profile.label = readInfo("label");
    if (profile.label.isEmpty())
        profile.label = profile.hardware.isEmpty() ? profile.driver : profile.hardware;

    profile.rxChannels = static_cast<int>(device->getNumChannels(SOAPY_SDR_RX));
    profile.txChannels = static_cast<int>(device->getNumChannels(SOAPY_SDR_TX));
    profile.fullDuplex = profile.rxChannels > 0 && profile.txChannels > 0;

    if (profile.rxChannels <= 0)
        return profile;

    for (double rate : device->listSampleRates(SOAPY_SDR_RX, 0))
        profile.sampleRates.append(rate);

    // Alcuni driver non elencano rate discreti ma dichiarano un intervallo
    // continuo: si scelgono valori tondi e usuali dentro quell'intervallo,
    // perché una tendina con un continuo non è utilizzabile.
    if (profile.sampleRates.isEmpty()) {
        for (const auto &range : device->getSampleRateRange(SOAPY_SDR_RX, 0)) {
            for (double candidate : {250000.0, 1024000.0, 2048000.0, 2400000.0,
                                     4000000.0, 8000000.0, 10000000.0, 20000000.0}) {
                if (candidate >= range.minimum() && candidate <= range.maximum()
                    && !profile.sampleRates.contains(candidate)) {
                    profile.sampleRates.append(candidate);
                }
            }
        }
    }

    try {
        profile.preferredSampleRate = device->getSampleRate(SOAPY_SDR_RX, 0);
    } catch (...) {
        profile.preferredSampleRate = 0.0;
    }

    const auto freqRanges = device->getFrequencyRange(SOAPY_SDR_RX, 0);
    if (!freqRanges.empty()) {
        double minimum = freqRanges.front().minimum();
        double maximum = freqRanges.front().maximum();
        for (const auto &range : freqRanges) {
            minimum = std::min(minimum, range.minimum());
            maximum = std::max(maximum, range.maximum());
        }
        profile.minFrequencyHz = static_cast<qint64>(minimum);
        profile.maxFrequencyHz = static_cast<qint64>(maximum);
    }

    try {
        profile.hasAgc = device->hasGainMode(SOAPY_SDR_RX, 0);
        const auto gainRange = device->getGainRange(SOAPY_SDR_RX, 0);
        profile.minGainDb = gainRange.minimum();
        profile.maxGainDb = gainRange.maximum();
    } catch (...) {
        // Un driver che non espone il guadagno non è un errore: significa
        // solo che l'utente non potrà regolarlo.
    }

    try {
        for (const auto &antenna : device->listAntennas(SOAPY_SDR_RX, 0))
            profile.antennas.append(QString::fromStdString(antenna));
        profile.currentAntenna = QString::fromStdString(device->getAntenna(SOAPY_SDR_RX, 0));
    } catch (...) {
    }

    return profile;
}

void SoapyWorker::applyPendingCommands(SoapySDR::Device *device)
{
    // SoapySDR non è thread-safe: i comandi si applicano qui, fra una lettura
    // e l'altra, mai dal thread che li ha richiesti.
    const qint64 frequency = m_pendingFrequency.exchange(-1, std::memory_order_acq_rel);
    if (frequency > 0) {
        try {
            device->setFrequency(SOAPY_SDR_RX, 0, static_cast<double>(frequency));
            qCInfo(dsdrHal) << "soapy: frequenza applicata" << frequency;
        } catch (const std::exception &e) {
            emit failed(tr("Sintonia rifiutata dal device: %1").arg(QString::fromUtf8(e.what())),
                        false);
        }
    }

    const double rate = m_pendingSampleRate.exchange(-1.0, std::memory_order_acq_rel);
    if (rate > 0.0) {
        try {
            device->setSampleRate(SOAPY_SDR_RX, 0, rate);
        } catch (const std::exception &e) {
            emit failed(tr("Frequenza di campionamento rifiutata: %1")
                            .arg(QString::fromUtf8(e.what())),
                        false);
        }
    }

    const int antenna = m_pendingAntenna.exchange(-1, std::memory_order_acq_rel);
    if (antenna >= 0 && antenna < m_antennas.size()) {
        try {
            device->setAntenna(SOAPY_SDR_RX, 0, m_antennas.at(antenna).toStdString());
        } catch (const std::exception &e) {
            emit failed(tr("Antenna rifiutata: %1").arg(QString::fromUtf8(e.what())), false);
        }
    }

    if (!m_gainCommandPending.exchange(false, std::memory_order_acq_rel))
        return;

    if (m_gainAuto.load(std::memory_order_acquire)) {
        try {
            // AUTO is deliberately client-controlled.  Several Soapy RTL-SDR
            // drivers combine tuner AGC and the RTL2832 AGC too aggressively,
            // producing clipped CF32 samples even when the audio still sounds
            // plausible.  Start at a conservative real gain and let the core
            // overload guard move it in discrete requests.
            if (device->hasGainMode(SOAPY_SDR_RX, 0))
                device->setGainMode(SOAPY_SDR_RX, 0, false);
            device->setGain(SOAPY_SDR_RX, 0, m_safeAutoGainDb);
            qCInfo(dsdrHal) << "soapy: AUTO prudente" << m_safeAutoGainDb
                            << "dB, AGC hardware off";
        } catch (const std::exception &e) {
            emit failed(tr("AUTO gain rifiutato: %1").arg(QString::fromUtf8(e.what())), false);
        }
    } else {
        const double gain = m_pendingGain.exchange(-1.0, std::memory_order_acq_rel);
        if (gain >= 0.0) {
            try {
                if (device->hasGainMode(SOAPY_SDR_RX, 0))
                    device->setGainMode(SOAPY_SDR_RX, 0, false);
                device->setGain(SOAPY_SDR_RX, 0, gain);
            } catch (const std::exception &e) {
                emit failed(tr("Guadagno rifiutato: %1").arg(QString::fromUtf8(e.what())), false);
            }
        }
    }
}

void SoapyWorker::openAndRun(const QString &deviceArgs, qint64 frequencyHz, double sampleRate)
{
    SoapySDR::Device *device = nullptr;

    qCInfo(dsdrHal) << "soapy: apertura" << deviceArgs
                    << "center" << frequencyHz << "sample rate richiesto" << sampleRate;

    // Nessuna eccezione attraversa il seam della HAL (§4.1): SoapySDR ne lancia
    // per qualunque cosa, e qui è dove si fermano.
    try {
        device = SoapySDR::Device::make(deviceArgs.toStdString());
    } catch (const std::exception &e) {
        emit failed(tr("Apertura del device fallita: %1").arg(QString::fromUtf8(e.what())), true);
        emit finished();
        return;
    }

    if (!device) {
        emit failed(tr("Il driver non ha restituito alcun device."), true);
        emit finished();
        return;
    }

    try {
        if (sampleRate > 0.0)
            device->setSampleRate(SOAPY_SDR_RX, 0, sampleRate);
        if (frequencyHz > 0)
            device->setFrequency(SOAPY_SDR_RX, 0, static_cast<double>(frequencyHz));
        if (device->hasGainMode(SOAPY_SDR_RX, 0))
            device->setGainMode(SOAPY_SDR_RX, 0, true);
    } catch (const std::exception &e) {
        qCWarning(dsdrHal) << "soapy: impostazioni iniziali parziali:" << e.what();
    }

    const SoapyDeviceProfile profile = readProfile(device, deviceArgs);
    if (!profile.isValid()) {
        emit failed(tr("Il device non offre canali in ricezione."), true);
        SoapySDR::Device::unmake(device);
        emit finished();
        return;
    }
    // L'elenco antenne serve al ciclo per tradurre gli indici richiesti.
    m_antennas = profile.antennas;
    m_safeAutoGainDb = safeAutoGainDb(profile);
    qCInfo(dsdrHal) << "soapy: profilo" << profile.driver << profile.hardware
                    << "label" << profile.label
                    << "sample rates" << profile.sampleRates
                    << "rate effettivo" << profile.preferredSampleRate
                    << "frequenza" << profile.minFrequencyHz << profile.maxFrequencyHz
                    << "gain" << profile.minGainDb << profile.maxGainDb
                    << "AGC hardware" << profile.hasAgc;
    // Applica il comando iniziale prima di entrare nel ciclo readStream().
    // In questo modo il primo blocco non può essere acquisito con l'AGC
    // hardware ancora attivo.
    applyPendingCommands(device);
    emit opened(profile);

    runLoop(device);

    SoapySDR::Device::unmake(device);
    emit finished();
}

void SoapyWorker::runLoop(SoapySDR::Device *device)
{
    SoapySDR::Stream *stream = nullptr;

    // CF32 è float interleaved: esattamente il formato del nostro ring, quindi
    // dalla scheda al DSP non c'è alcuna conversione.
    try {
        stream = device->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32, {0});
        device->activateStream(stream);
    } catch (const std::exception &e) {
        emit failed(tr("Avvio dello stream fallito: %1").arg(QString::fromUtf8(e.what())), true);
        if (stream)
            device->closeStream(stream);
        return;
    }

    m_running.store(true, std::memory_order_release);
    m_clock.start();
    if (m_ring)
        m_ring->clear();

    qCInfo(dsdrHal) << "soapy: stream attivo, formato CF32, blocco" << kReadFrames;

    int consecutiveErrors = 0;
    quint64 totalFrames = 0;
    quint64 totalDropped = 0;
    qint64 lastStatsNs = 0;

    while (m_running.load(std::memory_order_acquire)) {
        applyPendingCommands(device);

        void *buffers[1] = {m_buffer.data()};
        int flags = 0;
        long long timeNs = 0;

        const int read = device->readStream(stream, buffers, kReadFrames, flags, timeNs,
                                            kReadTimeoutUs);

        if (read == SOAPY_SDR_TIMEOUT)
            continue;   // silenzio momentaneo: non è un errore

        if (read == SOAPY_SDR_OVERFLOW) {
            // Il device ha perso campioni perché non li abbiamo letti in tempo.
            totalDropped += kReadFrames;
            emit samplesProduced(0, static_cast<quint32>(kReadFrames), 0);
            continue;
        }

        if (read < 0) {
            if (++consecutiveErrors > 10) {
                emit failed(tr("Lettura interrotta: %1").arg(describeError(read)), true);
                break;
            }
            continue;
        }

        consecutiveErrors = 0;
        if (read == 0)
            continue;

        const std::size_t floats = static_cast<std::size_t>(read) * 2;
        const std::size_t written = m_ring ? m_ring->write(m_buffer.data(), floats) : 0;
        const std::size_t writtenFrames = written / 2;
        totalFrames += writtenFrames;
        totalDropped += static_cast<std::size_t>(read) - writtenFrames;

        const qint64 nowNs = m_clock.nsecsElapsed();
        if (nowNs - lastStatsNs >= 1'000'000'000) {
            const double seconds = static_cast<double>(nowNs - lastStatsNs) / 1e9;
            qCDebug(dsdrHal) << "soapy: flusso" << (totalFrames / seconds) << "IQ/s"
                             << "scarti" << totalDropped
                             << "ring" << (m_ring ? m_ring->available() : 0) << "float";
            totalFrames = 0;
            totalDropped = 0;
            lastStatsNs = nowNs;
        }

        emit samplesProduced(static_cast<quint32>(writtenFrames),
                             static_cast<quint32>(static_cast<std::size_t>(read) - writtenFrames),
                             static_cast<quint64>(m_clock.nsecsElapsed()));
    }

    try {
        device->deactivateStream(stream);
        device->closeStream(stream);
    } catch (...) {
        // In chiusura un errore del driver non ha più nessuno a cui importare.
    }
}

} // namespace dsdr::hal::soapy
