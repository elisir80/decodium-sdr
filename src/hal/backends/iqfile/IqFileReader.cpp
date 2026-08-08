// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/iqfile/IqFileReader.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace dsdr::hal::iqfile {

namespace {

constexpr quint16 kFormatPcm = 1;
constexpr quint16 kFormatIeeeFloat = 3;
constexpr quint16 kFormatExtensible = 0xFFFE;

/// Tetto al buffer di conversione: un blocco più grande non rende le letture
/// più veloci e terrebbe occupata memoria per nulla.
constexpr std::size_t kMaxConvertFrames = 1 << 16;

int bytesPerSample(SampleFormat format)
{
    switch (format) {
    case SampleFormat::Float32: return 4;
    case SampleFormat::Int16:   return 2;
    case SampleFormat::Uint8:   return 1;
    }
    return 4;
}

quint32 readU32(const char *p) { return qFromLittleEndian<quint32>(p); }
quint16 readU16(const char *p) { return qFromLittleEndian<quint16>(p); }
quint64 readU64(const char *p) { return qFromLittleEndian<quint64>(p); }

} // namespace

IqFileReader::~IqFileReader()
{
    close();
}

bool IqFileReader::parseHeader(QFile &file, RecordingInfo &info, qint64 &dataOffset,
                               QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    char riff[12];
    if (file.read(riff, 12) != 12)
        return fail(QObject::tr("File troppo corto per essere un WAV."));

    const bool isRf64 = std::memcmp(riff, "RF64", 4) == 0;
    if (!isRf64 && std::memcmp(riff, "RIFF", 4) != 0)
        return fail(QObject::tr("Non è un file RIFF/WAV."));
    if (std::memcmp(riff + 8, "WAVE", 4) != 0)
        return fail(QObject::tr("File RIFF ma non WAVE."));

    bool haveFormat = false;
    quint64 dataBytes = 0;
    quint64 ds64DataBytes = 0;
    int channels = 0;

    // Scansione dei chunk. `data` non è per forza l'ultimo, e in RF64 la sua
    // dimensione vera arriva dal chunk ds64 che lo precede.
    while (!file.atEnd()) {
        char header[8];
        if (file.read(header, 8) != 8)
            break;

        const quint32 declared = readU32(header + 4);
        const qint64 chunkStart = file.pos();

        if (std::memcmp(header, "ds64", 4) == 0) {
            QByteArray body = file.read(std::min<quint32>(declared, 28));
            if (body.size() >= 16)
                ds64DataBytes = readU64(body.constData() + 8);
        } else if (std::memcmp(header, "fmt ", 4) == 0) {
            QByteArray body = file.read(std::min<quint32>(declared, 40));
            if (body.size() < 16)
                return fail(QObject::tr("Chunk fmt incompleto."));

            quint16 tag = readU16(body.constData());
            channels = readU16(body.constData() + 2);
            info.sampleRate = readU32(body.constData() + 4);
            const quint16 bits = readU16(body.constData() + 14);

            // WAVE_FORMAT_EXTENSIBLE incapsula il formato vero nel GUID; ne
            // bastano i primi due byte, che replicano il tag classico.
            if (tag == kFormatExtensible && body.size() >= 26)
                tag = readU16(body.constData() + 24);

            if (tag == kFormatIeeeFloat && bits == 32) {
                info.format = SampleFormat::Float32;
            } else if (tag == kFormatPcm && bits == 16) {
                info.format = SampleFormat::Int16;
            } else if (tag == kFormatPcm && bits == 8) {
                info.format = SampleFormat::Uint8;
            } else {
                return fail(QObject::tr("Formato campioni non supportato "
                                        "(tag %1, %2 bit): servono float32, PCM 16 bit "
                                        "o PCM 8 bit.")
                                .arg(tag)
                                .arg(bits));
            }
            haveFormat = true;
        } else if (std::memcmp(header, "data", 4) == 0) {
            dataOffset = chunkStart;
            dataBytes = declared;
            if (declared == 0xFFFFFFFFu || isRf64)
                dataBytes = ds64DataBytes ? ds64DataBytes : declared;

            // Un file troncato — registrazione interrotta, copia parziale —
            // dichiara più byte di quanti ne abbia. Ci si fida di ciò che c'è
            // davvero, invece di leggere oltre la fine.
            const quint64 actual = static_cast<quint64>(file.size() - chunkStart);
            dataBytes = std::min(dataBytes, actual);
            break;
        }

        // I chunk RIFF sono allineati a due byte.
        qint64 next = chunkStart + declared + (declared & 1);
        if (next <= chunkStart || !file.seek(next))
            break;
    }

    if (!haveFormat)
        return fail(QObject::tr("Chunk fmt assente."));
    if (dataOffset == 0)
        return fail(QObject::tr("Chunk data assente."));
    if (channels != 2)
        return fail(QObject::tr("Servono due canali (I e Q), trovati %1.").arg(channels));
    if (info.sampleRate <= 0.0)
        return fail(QObject::tr("Frequenza di campionamento nulla."));

    const int frameBytes = 2 * bytesPerSample(info.format);
    info.frameCount = static_cast<qint64>(dataBytes) / frameBytes;
    info.durationMs = static_cast<qint64>(info.frameCount * 1000.0 / info.sampleRate);

    if (info.frameCount <= 0)
        return fail(QObject::tr("La registrazione non contiene campioni."));

    return true;
}

qint64 IqFileReader::frequencyFromFileName(const QString &path)
{
    // I nostri nomi hanno la forma <data>_<MHz>MHz_<kSps>kSps.wav. È un
    // ripiego, non una fonte: serve solo a non lasciare la radio sintonizzata
    // su zero quando il sidecar è stato perso per strada.
    static const QRegularExpression pattern(
        QStringLiteral("([0-9]+(?:[.,][0-9]+)?)\\s*MHz"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = pattern.match(QFileInfo(path).completeBaseName());
    if (!match.hasMatch())
        return 0;

    bool ok = false;
    const double mhz = QString(match.captured(1)).replace(QLatin1Char(','), QLatin1Char('.'))
                           .toDouble(&ok);
    return ok ? static_cast<qint64>(mhz * 1e6) : 0;
}

void IqFileReader::applySidecar(const QString &path, RecordingInfo &info)
{
    const QFileInfo fileInfo(path);
    QFile sidecar(fileInfo.absolutePath() + QLatin1Char('/') + fileInfo.completeBaseName()
                  + QStringLiteral(".json"));

    if (!sidecar.open(QIODevice::ReadOnly)) {
        info.centerFrequencyHz = frequencyFromFileName(path);
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(sidecar.readAll()).object();
    if (root.isEmpty()) {
        info.centerFrequencyHz = frequencyFromFileName(path);
        return;
    }

    info.hasSidecar = true;
    info.centerFrequencyHz = static_cast<qint64>(
        root.value(QStringLiteral("centerFrequencyHz")).toDouble());
    info.backendId = root.value(QStringLiteral("backendId")).toString();
    info.deviceName = root.value(QStringLiteral("deviceName")).toString();

    // Il rate autorevole resta quello del chunk fmt: il sidecar lo ripete, ma
    // è il WAV a descrivere i byte che stiamo per leggere.
    if (info.centerFrequencyHz == 0)
        info.centerFrequencyHz = frequencyFromFileName(path);
}

bool IqFileReader::probe(const QString &path, RecordingInfo &info, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    info = RecordingInfo();
    info.filePath = path;
    info.displayName = QFileInfo(path).completeBaseName();

    qint64 dataOffset = 0;
    if (!parseHeader(file, info, dataOffset, error))
        return false;

    applySidecar(path, info);
    return true;
}

bool IqFileReader::open(const QString &path, QString *error)
{
    close();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = m_file.errorString();
        return false;
    }

    m_info = RecordingInfo();
    m_info.filePath = path;
    m_info.displayName = QFileInfo(path).completeBaseName();

    if (!parseHeader(m_file, m_info, m_dataOffset, error)) {
        m_file.close();
        return false;
    }

    applySidecar(path, m_info);

    // Il buffer nasce qui e non cambia più: leggere campioni non deve allocare
    // (CONSTITUTION §5).
    if (m_info.format != SampleFormat::Float32)
        m_raw.resize(kMaxConvertFrames * 2 * bytesPerSample(m_info.format));

    return seek(0);
}

void IqFileReader::close()
{
    if (m_file.isOpen())
        m_file.close();
    m_position = 0;
    m_dataOffset = 0;
}

bool IqFileReader::seek(qint64 frame)
{
    if (!m_file.isOpen())
        return false;

    frame = std::clamp<qint64>(frame, 0, m_info.frameCount);
    const qint64 offset = m_dataOffset + frame * 2 * bytesPerSample(m_info.format);
    if (!m_file.seek(offset))
        return false;

    m_position = frame;
    return true;
}

std::size_t IqFileReader::read(float *interleaved, std::size_t frames)
{
    if (!m_file.isOpen() || frames == 0)
        return 0;

    const qint64 remaining = m_info.frameCount - m_position;
    if (remaining <= 0)
        return 0;
    frames = std::min<std::size_t>(frames, static_cast<std::size_t>(remaining));

    if (m_info.format == SampleFormat::Float32) {
        const qint64 want = static_cast<qint64>(frames) * 2 * sizeof(float);
        const qint64 got = m_file.read(reinterpret_cast<char *>(interleaved), want);
        if (got <= 0)
            return 0;
        const std::size_t readFrames = static_cast<std::size_t>(got) / (2 * sizeof(float));
        m_position += static_cast<qint64>(readFrames);
        return readFrames;
    }

    frames = std::min(frames, kMaxConvertFrames);
    const int sampleBytes = bytesPerSample(m_info.format);
    const qint64 want = static_cast<qint64>(frames) * 2 * sampleBytes;
    const qint64 got = m_file.read(m_raw.data(), want);
    if (got <= 0)
        return 0;

    const std::size_t samples = static_cast<std::size_t>(got) / sampleBytes;
    if (m_info.format == SampleFormat::Int16) {
        for (std::size_t i = 0; i < samples; ++i) {
            const auto raw = qFromLittleEndian<qint16>(m_raw.data() + i * 2);
            interleaved[i] = static_cast<float>(raw) / 32768.0f;
        }
    } else {
        // PCM a 8 bit senza segno: 128 è lo zero, come esce dai dump RTL-SDR.
        for (std::size_t i = 0; i < samples; ++i) {
            const auto raw = static_cast<unsigned char>(m_raw[i]);
            interleaved[i] = (static_cast<float>(raw) - 127.5f) / 127.5f;
        }
    }

    const std::size_t readFrames = samples / 2;
    m_position += static_cast<qint64>(readFrames);
    return readFrames;
}

} // namespace dsdr::hal::iqfile
