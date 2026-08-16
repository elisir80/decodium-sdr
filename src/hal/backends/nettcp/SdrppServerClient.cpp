// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/nettcp/SdrppServerClient.h"

#include "dsp/SpscRing.h"
#include "hal/HalLog.h"
#include "hal/backends/nettcp/SdrppServerProtocol.h"

#include <QTcpSocket>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsdr::hal::nettcp {

namespace {
constexpr int kMaxFramesPerPass = 16'384;

IqSampleFormat validSdrppFormat(int value)
{
    switch (value) {
    case static_cast<int>(IqSampleFormat::Int8):    return IqSampleFormat::Int8;
    case static_cast<int>(IqSampleFormat::Int16):   return IqSampleFormat::Int16;
    case static_cast<int>(IqSampleFormat::Float32): return IqSampleFormat::Float32;
    default:                                         return IqSampleFormat::Int16;
    }
}

sdrpp::PcmType sdrppPcmType(IqSampleFormat format)
{
    switch (format) {
    case IqSampleFormat::Int8:    return sdrpp::PcmType::Int8;
    case IqSampleFormat::Float32: return sdrpp::PcmType::Float32;
    case IqSampleFormat::Int16:
    case IqSampleFormat::Int32:   return sdrpp::PcmType::Int16;
    }
    return sdrpp::PcmType::Int16;
}

float floatFromLittleEndian(const char *data)
{
    const quint32 bits = qFromLittleEndian<quint32>(data);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value) ? value : 0.0f;
}

QByteArray littleEndianDouble(double value)
{
    static_assert(sizeof(double) == sizeof(quint64), "wire double expects 64 bits");
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    QByteArray body(sizeof(bits), Qt::Uninitialized);
    qToLittleEndian(bits, body.data());
    return body;
}

double littleEndianDouble(const char *data)
{
    const quint64 bits = qFromLittleEndian<quint64>(data);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
} // namespace

SdrppServerClient::SdrppServerClient(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    m_scratch.resize(static_cast<std::size_t>(kMaxFramesPerPass) * 2);
}

SdrppServerClient::~SdrppServerClient()
{
    disconnectFromServer();
}

void SdrppServerClient::connectToServer(const QString &host, quint16 port, qint64 frequencyHz,
                                        int sampleFormat)
{
    disconnectFromServer();
    m_format = validSdrppFormat(sampleFormat);
    m_pending.clear();
    m_sampleRateKnown = false;
    if (m_ring)
        m_ring->clear();
    m_clock.start();

    m_socket = new QTcpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1 << 20);
    connect(m_socket, &QTcpSocket::connected, this, &SdrppServerClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &SdrppServerClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &SdrppServerClient::onSocketError);
    connect(m_socket, &QTcpSocket::disconnected, this, &SdrppServerClient::onDisconnected);
    m_socket->setProperty("initialFrequencyHz", frequencyHz);
    qCInfo(dsdrHal) << "sdrpp: connessione a" << host << port;
    m_socket->connectToHost(host, port);
}

void SdrppServerClient::disconnectFromServer()
{
    if (!m_socket)
        return;
    m_socket->disconnect(this);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
    m_pending.clear();
    m_sampleRateKnown = false;
}

void SdrppServerClient::onConnected()
{
    if (!m_socket)
        return;
    // Il server invia il sample rate appena accetta il client. La richiesta UI
    // e' un controllo di compatibilita'; ignoriamo il payload grafico per non
    // legare Decodium all'interfaccia remota di SDR++.
    sendCommand(static_cast<quint32>(sdrpp::Command::GetUi));
    sendCommand(static_cast<quint32>(sdrpp::Command::SetSampleType),
                QByteArray(1, static_cast<char>(sdrppPcmType(m_format))));
    sendCommand(static_cast<quint32>(sdrpp::Command::SetCompression), QByteArray(1, 0));
    setFrequency(m_socket->property("initialFrequencyHz").toLongLong());
    sendCommand(static_cast<quint32>(sdrpp::Command::Start));
}

void SdrppServerClient::setFrequency(qint64 frequencyHz)
{
    sendCommand(static_cast<quint32>(sdrpp::Command::SetFrequency),
                littleEndianDouble(static_cast<double>(frequencyHz)));
}

void SdrppServerClient::sendCommand(quint32 command, const QByteArray &body)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray packet(static_cast<int>(sdrpp::kPacketHeaderSize + sdrpp::kCommandHeaderSize
                                       + body.size()), Qt::Uninitialized);
    qToLittleEndian(static_cast<quint32>(sdrpp::PacketType::Command), packet.data());
    qToLittleEndian(static_cast<quint32>(packet.size()), packet.data() + 4);
    qToLittleEndian(command, packet.data() + sdrpp::kPacketHeaderSize);
    if (!body.isEmpty())
        std::memcpy(packet.data() + sdrpp::kPacketHeaderSize + sdrpp::kCommandHeaderSize,
                    body.constData(), static_cast<std::size_t>(body.size()));
    m_socket->write(packet);
}

void SdrppServerClient::onReadyRead()
{
    if (!m_socket)
        return;
    m_pending.append(m_socket->readAll());
    parsePackets();
}

void SdrppServerClient::parsePackets()
{
    while (m_pending.size() >= static_cast<int>(sdrpp::kPacketHeaderSize)) {
        const char *header = m_pending.constData();
        const quint32 type = qFromLittleEndian<quint32>(header);
        const quint32 size = qFromLittleEndian<quint32>(header + 4);
        if (size < sdrpp::kPacketHeaderSize || size > sdrpp::kMaxPacketSize) {
            failProtocol(tr("Frame SDR++ Server non valido (%1 byte).").arg(size));
            return;
        }
        if (m_pending.size() < static_cast<int>(size))
            return;
        const QByteArray body = m_pending.mid(static_cast<int>(sdrpp::kPacketHeaderSize),
                                              static_cast<int>(size - sdrpp::kPacketHeaderSize));
        m_pending.remove(0, static_cast<int>(size));
        processPacket(type, body);
        if (!m_socket)
            return;
    }
}

void SdrppServerClient::processPacket(quint32 type, const QByteArray &body)
{
    if (type == static_cast<quint32>(sdrpp::PacketType::Command)) {
        if (body.size() < static_cast<int>(sdrpp::kCommandHeaderSize)) {
            failProtocol(tr("Comando SDR++ Server senza intestazione."));
            return;
        }
        const quint32 command = qFromLittleEndian<quint32>(body.constData());
        const QByteArray payload = body.mid(static_cast<int>(sdrpp::kCommandHeaderSize));
        if (command == static_cast<quint32>(sdrpp::Command::SetSampleRate)) {
            if (payload.size() != static_cast<int>(sizeof(double))) {
                failProtocol(tr("Sample rate SDR++ Server non valido."));
                return;
            }
            const double rate = littleEndianDouble(payload.constData());
            if (!(rate >= 8'000.0 && rate <= 100'000'000.0)) {
                failProtocol(tr("Sample rate SDR++ Server fuori limite: %1.").arg(rate));
                return;
            }
            if (!m_sampleRateKnown) {
                m_sampleRateKnown = true;
                qCInfo(dsdrHal) << "sdrpp: connesso, IQ" << iqSampleFormatName(m_format)
                                 << "rate" << rate;
                emit connected(rate);
            }
        } else if (command == static_cast<quint32>(sdrpp::Command::Disconnect)) {
            failProtocol(tr("Il server SDR++ ha rifiutato il client o e' occupato."));
        }
        return;
    }
    if (type == static_cast<quint32>(sdrpp::PacketType::Baseband)) {
        processBaseband(body);
        return;
    }
    if (type == static_cast<quint32>(sdrpp::PacketType::BasebandCompressed)) {
        failProtocol(tr("Il server SDR++ ha inviato IQ compresso: disattiva Compression sul server."));
        return;
    }
    if (type == static_cast<quint32>(sdrpp::PacketType::Error)) {
        failProtocol(tr("Il server SDR++ ha rifiutato una richiesta."));
    }
}

void SdrppServerClient::processBaseband(const QByteArray &body)
{
    const int bytesPerFrame = bytesPerIqFrame(m_format);
    if (body.size() < bytesPerFrame)
        return;
    const quint64 timestampNs = static_cast<quint64>(m_clock.nsecsElapsed());
    int offset = 0;
    while (body.size() - offset >= bytesPerFrame) {
        const int frames = std::min(static_cast<int>((body.size() - offset) / bytesPerFrame),
                                    kMaxFramesPerPass);
        const char *raw = body.constData() + offset;
        for (int frame = 0; frame < frames; ++frame) {
            const int index = frame * bytesPerFrame;
            switch (m_format) {
            case IqSampleFormat::Int8:
                m_scratch[static_cast<std::size_t>(frame) * 2] =
                    static_cast<float>(static_cast<qint8>(raw[index])) / 128.0f;
                m_scratch[static_cast<std::size_t>(frame) * 2 + 1] =
                    static_cast<float>(static_cast<qint8>(raw[index + 1])) / 128.0f;
                break;
            case IqSampleFormat::Int16:
                m_scratch[static_cast<std::size_t>(frame) * 2] =
                    static_cast<float>(qFromLittleEndian<qint16>(raw + index)) / 32768.0f;
                m_scratch[static_cast<std::size_t>(frame) * 2 + 1] =
                    static_cast<float>(qFromLittleEndian<qint16>(raw + index + 2)) / 32768.0f;
                break;
            case IqSampleFormat::Float32:
                m_scratch[static_cast<std::size_t>(frame) * 2] = floatFromLittleEndian(raw + index);
                m_scratch[static_cast<std::size_t>(frame) * 2 + 1] = floatFromLittleEndian(raw + index + 4);
                break;
            case IqSampleFormat::Int32:
                break;
            }
        }
        std::size_t written = 0;
        if (m_ring)
            written = m_ring->write(m_scratch.data(), static_cast<std::size_t>(frames) * 2);
        emit samplesProduced(static_cast<quint32>(written / 2),
                             static_cast<quint32>(frames - static_cast<int>(written / 2)),
                             timestampNs);
        offset += frames * bytesPerFrame;
    }
}

void SdrppServerClient::onSocketError()
{
    if (!m_socket)
        return;
    const QString message = m_socket->errorString();
    if (!message.isEmpty())
        failProtocol(message);
}

void SdrppServerClient::onDisconnected()
{
    emit disconnected();
}

void SdrppServerClient::failProtocol(const QString &message)
{
    qCWarning(dsdrHal) << "sdrpp:" << message;
    emit failed(message, true);
    if (m_socket)
        m_socket->abort();
}

} // namespace dsdr::hal::nettcp
