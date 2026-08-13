// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlsdr/RtlSdrWorker.h"

#include "dsp/SpscRing.h"
#include "hal/HalLog.h"

#include <rtl-sdr.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsdr::hal::rtlsdr {

namespace {

constexpr std::size_t kReadBufferBytes = 1u << 16; // multiplo di 512 richiesto da librtlsdr
constexpr std::size_t kScratchFloats = kReadBufferBytes;

const float *sampleTable()
{
    static const std::vector<float> table = [] {
        std::vector<float> values(256);
        for (int i = 0; i < 256; ++i)
            values[static_cast<std::size_t>(i)] =
                (static_cast<float>(i) - 127.5f) / 127.5f;
        return values;
    }();
    return table.data();
}

QString tunerName(enum rtlsdr_tuner tuner)
{
    switch (tuner) {
    case RTLSDR_TUNER_E4000: return QStringLiteral("Elonics E4000");
    case RTLSDR_TUNER_FC0012: return QStringLiteral("Fitipower FC0012");
    case RTLSDR_TUNER_FC0013: return QStringLiteral("Fitipower FC0013");
    case RTLSDR_TUNER_FC2580: return QStringLiteral("FCI FC2580");
    case RTLSDR_TUNER_R820T: return QStringLiteral("Rafael Micro R820T");
    case RTLSDR_TUNER_R828D: return QStringLiteral("Rafael Micro R828D");
    default: return QStringLiteral("Sconosciuto");
    }
}

QList<double> standardSampleRates()
{
    // Sono rate validi per il demodulatore RTL2832; i valori conservativi fino
    // a 2,4 MS/s evitano gli overrun USB più comuni sulle macchine degli utenti.
    return {250'000.0, 1'024'000.0, 1'536'000.0, 1'792'000.0,
            1'920'000.0, 2'048'000.0, 2'160'000.0, 2'400'000.0,
            2'560'000.0, 2'880'000.0, 3'200'000.0};
}

} // namespace

RtlSdrWorker::RtlSdrWorker(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
    , m_scratch(kScratchFloats)
{
}

RtlSdrWorker::~RtlSdrWorker()
{
    requestStop();
}

void RtlSdrWorker::requestFrequency(qint64 hz)
{
    m_pendingFrequency.store(hz, std::memory_order_release);
}

void RtlSdrWorker::requestSampleRate(double rate)
{
    m_pendingSampleRate.store(rate, std::memory_order_release);
}

void RtlSdrWorker::requestGain(double db)
{
    m_pendingGainTenths.store(db < 0.0 ? -1 : qRound(db * 10.0),
                              std::memory_order_release);
}

void RtlSdrWorker::requestPpm(int ppm)
{
    m_pendingPpm.store(ppm, std::memory_order_release);
}

void RtlSdrWorker::requestBiasTee(bool enabled)
{
    m_pendingBiasTee.store(enabled ? 1 : 0, std::memory_order_release);
}

void RtlSdrWorker::requestDirectSampling(int mode)
{
    m_pendingDirectSampling.store(std::clamp(mode, 0, 2), std::memory_order_release);
}

void RtlSdrWorker::requestOffsetTuning(bool enabled)
{
    m_pendingOffsetTuning.store(enabled ? 1 : 0, std::memory_order_release);
}

void RtlSdrWorker::requestStop()
{
    m_running.store(false, std::memory_order_release);
    if (rtlsdr_dev_t *device = m_device.load(std::memory_order_acquire))
        rtlsdr_cancel_async(device);
}

RtlSdrDeviceProfile RtlSdrWorker::readProfile(rtlsdr_dev_t *device, int deviceIndex,
                                               const QString &serial) const
{
    RtlSdrDeviceProfile profile;
    profile.index = deviceIndex;
    profile.product = QString::fromUtf8(rtlsdr_get_device_name(
        static_cast<uint32_t>(deviceIndex)));
    profile.serial = serial;
    profile.sampleRates = standardSampleRates();
    profile.tuner = tunerName(rtlsdr_get_tuner_type(device));

    const int count = rtlsdr_get_tuner_gains(device, nullptr);
    if (count > 0) {
        profile.gainTenthsDb.resize(count);
        const int received = rtlsdr_get_tuner_gains(device, profile.gainTenthsDb.data());
        if (received < 0)
            profile.gainTenthsDb.clear();
        else if (received < profile.gainTenthsDb.size())
            profile.gainTenthsDb.resize(received);
    }
    std::sort(profile.gainTenthsDb.begin(), profile.gainTenthsDb.end());
    return profile;
}

void RtlSdrWorker::applyPendingCommands(rtlsdr_dev_t *device)
{
    const int directSampling = m_pendingDirectSampling.exchange(-1, std::memory_order_acq_rel);
    if (directSampling >= 0) {
        const int result = rtlsdr_set_direct_sampling(device, directSampling);
        if (result != 0)
            qCWarning(dsdrHal) << "rtlsdr: direct sampling rifiutato" << result;
        else
            qCInfo(dsdrHal) << "rtlsdr: direct sampling" << directSampling;
    }

    const int offsetTuning = m_pendingOffsetTuning.exchange(-1, std::memory_order_acq_rel);
    if (offsetTuning >= 0) {
        const int result = rtlsdr_set_offset_tuning(device, offsetTuning);
        if (result != 0)
            qCWarning(dsdrHal) << "rtlsdr: offset tuning rifiutato" << result;
    }

    const int ppm = m_pendingPpm.exchange(std::numeric_limits<int>::min(),
                                          std::memory_order_acq_rel);
    if (ppm != std::numeric_limits<int>::min()
        && rtlsdr_set_freq_correction(device, ppm) != 0)
        qCWarning(dsdrHal) << "rtlsdr: correzione PPM rifiutata" << ppm;

    const double sampleRate = m_pendingSampleRate.exchange(-1.0, std::memory_order_acq_rel);
    if (sampleRate > 0.0) {
        const int result = rtlsdr_set_sample_rate(device, static_cast<uint32_t>(sampleRate));
        if (result != 0)
            qCWarning(dsdrHal) << "rtlsdr: sample rate rifiutato" << sampleRate << result;
    }

    const qint64 frequency = m_pendingFrequency.exchange(-1, std::memory_order_acq_rel);
    if (frequency > 0) {
        const int result = rtlsdr_set_center_freq(device, static_cast<uint32_t>(frequency));
        if (result != 0)
            qCWarning(dsdrHal) << "rtlsdr: frequenza rifiutata" << frequency << result;
    }

    const int gain = m_pendingGainTenths.exchange(-2, std::memory_order_acq_rel);
    if (gain != -2) {
        if (gain < 0) {
            // Do not combine tuner auto-gain with the RTL2832 AGC.  On V4
            // sticks that combination can drive the 8-bit ADC into clipping
            // on a strong FM multiplex.  AUTO is closed-loop in Decodium:
            // start at a conservative real tuner step, keep the hardware AGC
            // off, and let the overload guard request lower/higher steps.
            const int selected = safeAutoGainTenthsDb(m_gainSteps);
            const int agcResult = rtlsdr_set_agc_mode(device, 0);
            const int modeResult = rtlsdr_set_tuner_gain_mode(device, 1);
            const int gainResult = rtlsdr_set_tuner_gain(device, selected);
            if (agcResult != 0)
                qCWarning(dsdrHal) << "rtlsdr: AGC hardware non disattivabile" << agcResult;
            if (modeResult != 0)
                qCWarning(dsdrHal) << "rtlsdr: modo guadagno manuale non applicabile" << modeResult;
            if (gainResult != 0)
                qCWarning(dsdrHal) << "rtlsdr: guadagno AUTO prudente rifiutato" << selected
                                    << gainResult;
            else
                qCInfo(dsdrHal) << "rtlsdr: AUTO prudente" << selected / 10.0
                                << "dB, AGC hardware off";
        } else {
            int selected = gain;
            if (!m_gainSteps.empty()) {
                selected = *std::min_element(m_gainSteps.begin(), m_gainSteps.end(),
                                             [gain](int left, int right) {
                                                 return std::abs(left - gain) < std::abs(right - gain);
                                             });
            }
            rtlsdr_set_tuner_gain_mode(device, 1);
            rtlsdr_set_agc_mode(device, 0);
            const int result = rtlsdr_set_tuner_gain(device, selected);
            if (result != 0)
                qCWarning(dsdrHal) << "rtlsdr: guadagno rifiutato" << selected << result;
            else
                qCInfo(dsdrHal) << "rtlsdr: guadagno manuale" << selected / 10.0 << "dB";
        }
    }

    const int biasTee = m_pendingBiasTee.exchange(-1, std::memory_order_acq_rel);
    if (biasTee >= 0) {
        const int result = rtlsdr_set_bias_tee(device, biasTee);
        if (result != 0)
            qCWarning(dsdrHal) << "rtlsdr: bias tee rifiutato" << result;
        else
            qCInfo(dsdrHal) << "rtlsdr: bias tee" << (biasTee != 0 ? "on" : "off");
    }
}

void RtlSdrWorker::fail(const QString &message, bool fatal)
{
    qCWarning(dsdrHal) << "rtlsdr:" << message;
    emit failed(message, fatal);
}

void RtlSdrWorker::openAndRun(int deviceIndex, const QString &serial,
                              qint64 frequencyHz, double sampleRate)
{
    rtlsdr_dev_t *device = nullptr;
    int index = deviceIndex;
    if (!serial.isEmpty()) {
        const int serialIndex = rtlsdr_get_index_by_serial(serial.toUtf8().constData());
        if (serialIndex >= 0)
            index = serialIndex;
    }

    if (index < 0 || rtlsdr_open(&device, static_cast<uint32_t>(index)) != 0 || !device) {
        fail(tr("Apertura del device RTL-SDR fallita."), true);
        emit finished();
        return;
    }

    m_device.store(device, std::memory_order_release);
    const RtlSdrDeviceProfile profile = readProfile(device, index, serial);
    m_gainSteps = profile.gainTenthsDb;

    m_pendingSampleRate.store(sampleRate, std::memory_order_release);
    m_pendingFrequency.store(frequencyHz, std::memory_order_release);
    applyPendingCommands(device);
    if (rtlsdr_reset_buffer(device) != 0)
        qCWarning(dsdrHal) << "rtlsdr: reset buffer fallito";

    if (m_ring)
        m_ring->clear();
    m_clock.start();
    m_running.store(true, std::memory_order_release);
    emit opened(profile);

    qCInfo(dsdrHal) << "rtlsdr: streaming" << profile.product
                    << "tuner" << profile.tuner << "index" << index
                    << "rate" << sampleRate << "center" << frequencyHz;

    const int result = rtlsdr_read_async(device, &RtlSdrWorker::asyncCallback, this,
                                         0, static_cast<uint32_t>(kReadBufferBytes));
    const bool stopped = !m_running.load(std::memory_order_acquire);
    m_running.store(false, std::memory_order_release);
    m_device.store(nullptr, std::memory_order_release);
    rtlsdr_close(device);

    if (result != 0 && !stopped)
        fail(tr("Lettura dei campioni RTL-SDR fallita (codice %1).").arg(result), true);
    qCInfo(dsdrHal) << "rtlsdr: streaming terminato";
    emit finished();
}

void RtlSdrWorker::asyncCallback(unsigned char *buffer, quint32 length, void *context)
{
    if (auto *worker = static_cast<RtlSdrWorker *>(context))
        worker->onSamples(buffer, length);
}

void RtlSdrWorker::onSamples(unsigned char *buffer, quint32 length)
{
    if (!m_running.load(std::memory_order_acquire) || !buffer || length < 2)
        return;

    rtlsdr_dev_t *device = m_device.load(std::memory_order_acquire);
    if (device)
        applyPendingCommands(device);

    const std::size_t usable = static_cast<std::size_t>(length) & ~std::size_t(1);
    const float *table = sampleTable();
    for (std::size_t i = 0; i < usable; ++i)
        m_scratch[i] = table[buffer[i]];

    const std::size_t written = m_ring ? m_ring->write(m_scratch.data(), usable) : 0;
    const std::size_t writtenFrames = written / 2;
    const std::size_t droppedFrames = usable / 2 - writtenFrames;
    emit samplesProduced(static_cast<quint32>(writtenFrames),
                         static_cast<quint32>(droppedFrames),
                         static_cast<quint64>(m_clock.nsecsElapsed()));
}

} // namespace dsdr::hal::rtlsdr
