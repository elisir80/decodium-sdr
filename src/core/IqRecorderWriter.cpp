// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/IqRecorderWriter.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>
#include <QtEndian>

Q_DECLARE_LOGGING_CATEGORY(dsdrCore)

namespace dsdr::core {

namespace {

/// Ogni quanto si svuota il ring verso il disco. Un intervallo lungo rende le
/// scritture più grandi ed efficienti; troppo lungo richiederebbe un ring
/// enorme per non perdere campioni.
constexpr int kDrainIntervalMs = 120;

/// I campi di dimensione del WAV sono a 32 bit: oltre questa soglia il file
/// deve dichiararsi RF64.
constexpr quint64 kWavSizeLimit = 0xFFFFFFFEull;

constexpr int kChannels = 2;             // I e Q
constexpr int kBitsPerSample = 32;       // float32
constexpr quint16 kFormatIeeeFloat = 3;

void appendTag(QByteArray &out, const char *tag)
{
    out.append(tag, 4);
}

void appendU32(QByteArray &out, quint32 value)
{
    char buffer[4];
    qToLittleEndian<quint32>(value, buffer);
    out.append(buffer, 4);
}

void appendU16(QByteArray &out, quint16 value)
{
    char buffer[2];
    qToLittleEndian<quint16>(value, buffer);
    out.append(buffer, 2);
}

void appendU64(QByteArray &out, quint64 value)
{
    char buffer[8];
    qToLittleEndian<quint64>(value, buffer);
    out.append(buffer, 8);
}

} // namespace

IqRecorderWriter::IqRecorderWriter(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    m_buffer.resize(1 << 16);
}

IqRecorderWriter::~IqRecorderWriter()
{
    close();
}

qint64 IqRecorderWriter::durationMs() const
{
    if (m_info.sampleRate <= 0.0)
        return 0;
    const qint64 frames = m_dataBytes.load(std::memory_order_relaxed)
        / (kChannels * (kBitsPerSample / 8));
    return static_cast<qint64>(frames * 1000.0 / m_info.sampleRate);
}

void IqRecorderWriter::open(const QString &path, const IqRecordingInfo &info)
{
    close();

    m_info = info;
    m_dataBytes.store(0, std::memory_order_relaxed);

    const QFileInfo fileInfo(path);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        emit failed(tr("Impossibile creare la cartella %1.").arg(fileInfo.absolutePath()));
        return;
    }

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(tr("Impossibile scrivere %1: %2").arg(path, m_file.errorString()));
        return;
    }

    if (!writeHeader()) {
        m_file.close();
        emit failed(tr("Scrittura dell'intestazione fallita."));
        return;
    }

    m_open = true;

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kDrainIntervalMs);
        connect(m_timer, &QTimer::timeout, this, &IqRecorderWriter::drain);
    }
    m_timer->start();
}

bool IqRecorderWriter::writeHeader()
{
    QByteArray header;
    header.reserve(128);

    appendTag(header, "RIFF");
    appendU32(header, 0);          // dimensione: si aggiorna alla chiusura
    appendTag(header, "WAVE");

    // Segnaposto che alla chiusura può diventare il chunk ds64 di RF64.
    m_junkChunkOffset = header.size();
    appendTag(header, "JUNK");
    appendU32(header, 28);
    header.append(28, '\0');

    appendTag(header, "fmt ");
    appendU32(header, 16);
    appendU16(header, kFormatIeeeFloat);
    appendU16(header, kChannels);
    appendU32(header, static_cast<quint32>(m_info.sampleRate));
    appendU32(header, static_cast<quint32>(m_info.sampleRate) * kChannels * (kBitsPerSample / 8));
    appendU16(header, kChannels * (kBitsPerSample / 8));
    appendU16(header, kBitsPerSample);

    appendTag(header, "data");
    m_dataSizeOffset = header.size();
    appendU32(header, 0);          // dimensione: si aggiorna alla chiusura

    return m_file.write(header) == header.size();
}

void IqRecorderWriter::drain()
{
    if (!m_open || !m_ring)
        return;

    while (m_ring->available() > 0) {
        const std::size_t got = m_ring->read(m_buffer.data(), m_buffer.size());
        if (got == 0)
            break;

        const qint64 bytes = static_cast<qint64>(got * sizeof(float));
        const qint64 written = m_file.write(reinterpret_cast<const char *>(m_buffer.data()), bytes);
        if (written != bytes) {
            emit failed(tr("Scrittura interrotta: %1").arg(m_file.errorString()));
            close();
            return;
        }
        m_dataBytes.fetch_add(written, std::memory_order_relaxed);
    }
}

bool IqRecorderWriter::finalizeHeader()
{
    const quint64 dataBytes = static_cast<quint64>(m_dataBytes.load(std::memory_order_relaxed));
    const quint64 riffSize = dataBytes + static_cast<quint64>(m_dataSizeOffset) + 4 - 8;

    if (riffSize <= kWavSizeLimit) {
        // Sta in un WAV normale: bastano le due dimensioni a 32 bit.
        QByteArray value;
        appendU32(value, static_cast<quint32>(riffSize));
        if (!m_file.seek(4) || m_file.write(value) != 4)
            return false;

        value.clear();
        appendU32(value, static_cast<quint32>(dataBytes));
        return m_file.seek(m_dataSizeOffset) && m_file.write(value) == 4;
    }

    // Oltre i 4 GB il file diventa RF64: "RIFF" → "RF64", le dimensioni a
    // 32 bit vanno a -1 e quelle vere finiscono nel chunk ds64, che prende il
    // posto del JUNK riservato all'inizio.
    if (!m_file.seek(0) || m_file.write("RF64", 4) != 4)
        return false;

    QByteArray minusOne;
    appendU32(minusOne, 0xFFFFFFFFu);
    if (m_file.write(minusOne) != 4)
        return false;

    QByteArray ds64;
    appendTag(ds64, "ds64");
    appendU32(ds64, 28);
    appendU64(ds64, riffSize);
    appendU64(ds64, dataBytes);
    appendU64(ds64, dataBytes / (kChannels * (kBitsPerSample / 8))); // conteggio campioni
    appendU32(ds64, 0);                                              // nessuna tabella
    if (!m_file.seek(m_junkChunkOffset) || m_file.write(ds64) != ds64.size())
        return false;

    if (!m_file.seek(m_dataSizeOffset) || m_file.write(minusOne) != 4)
        return false;

    return true;
}

bool IqRecorderWriter::writeSidecar()
{
    // Il WAV sa dire quanti campioni al secondo, non su quale frequenza:
    // senza il sidecar una registrazione IQ è un file di numeri senza senso.
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("decodium-iq/1"));
    root.insert(QStringLiteral("centerFrequencyHz"), m_info.centerFrequencyHz);
    root.insert(QStringLiteral("sampleRate"), m_info.sampleRate);
    root.insert(QStringLiteral("sampleFormat"), QStringLiteral("float32"));
    root.insert(QStringLiteral("channels"), kChannels);
    root.insert(QStringLiteral("backendId"), m_info.backendId);
    root.insert(QStringLiteral("deviceName"), m_info.deviceName);
    root.insert(QStringLiteral("startedAt"), m_info.startedAt.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("durationMs"), durationMs());
    root.insert(QStringLiteral("dataBytes"), m_dataBytes.load(std::memory_order_relaxed));
    if (!m_info.antenna.isEmpty())
        root.insert(QStringLiteral("antenna"), m_info.antenna);
    if (!m_info.operatorCall.isEmpty())
        root.insert(QStringLiteral("operator"), m_info.operatorCall);
    root.insert(QStringLiteral("application"), QStringLiteral("DECODIUM SDR"));

    QFile sidecar(QFileInfo(m_file).absolutePath() + QLatin1Char('/')
                  + QFileInfo(m_file).completeBaseName() + QStringLiteral(".json"));
    if (!sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    sidecar.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void IqRecorderWriter::close()
{
    if (m_timer)
        m_timer->stop();

    if (!m_open)
        return;

    drain();          // ultimo giro: i campioni residui non vanno persi
    m_open = false;

    if (!finalizeHeader())
        emit failed(tr("Impossibile completare l'intestazione del file."));
    if (!writeSidecar())
        emit failed(tr("Impossibile scrivere i metadati della registrazione."));

    m_file.close();
}

} // namespace dsdr::core
