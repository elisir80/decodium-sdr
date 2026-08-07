// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/nettcp/SpyServerClient.h"

#include "dsp/SpscRing.h"
#include "hal/HalLog.h"

#include <QTcpSocket>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace dsdr::hal::nettcp {

using namespace spyserver;

namespace {

constexpr int kMaxFramesPerPass = 32768;

/// Nome con cui ci si presenta al server. Compare nei log di chi lo ospita:
/// è cortesia dire chi si è.
const char *kClientName = "DECODIUM SDR";

/// Tabella di conversione uint8 → float, come per rtl_tcp.
const float *uint8Table()
{
    static const std::vector<float> table = [] {
        std::vector<float> t(256);
        for (int i = 0; i < 256; ++i)
            t[static_cast<std::size_t>(i)] = (static_cast<float>(i) - 127.5f) / 127.5f;
        return t;
    }();
    return table.data();
}

} // namespace

SpyServerClient::SpyServerClient(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    m_scratch.resize(static_cast<std::size_t>(kMaxFramesPerPass) * 2);
}

SpyServerClient::~SpyServerClient()
{
    disconnectFromServer();
}

void SpyServerClient::connectToServer(const QString &host, quint16 port)
{
    disconnectFromServer();

    m_socket = new QTcpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1 << 20);

    connect(m_socket, &QTcpSocket::readyRead, this, &SpyServerClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &SpyServerClient::onSocketError);
    connect(m_socket, &QTcpSocket::disconnected, this, &SpyServerClient::onDisconnected);
    connect(m_socket, &QTcpSocket::connected, this, &SpyServerClient::sendHello);

    m_pending.clear();
    m_deviceInfoReceived = false;
    m_streaming = false;
    if (m_ring)
        m_ring->clear();
    m_clock.start();

    qCInfo(dsdrHal) << "spyserver: connessione a" << host << port;
    m_socket->connectToHost(host, port);
}

void SpyServerClient::disconnectFromServer()
{
    if (!m_socket)
        return;

    m_socket->disconnect(this);
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
    m_deviceInfoReceived = false;
    m_streaming = false;
    m_pending.clear();
}

void SpyServerClient::sendHello()
{
    if (!m_socket)
        return;

    // Corpo: versione di protocollo, poi il nome del client.
    const QByteArray name(kClientName);
    QByteArray body;
    body.resize(4);
    qToLittleEndian<quint32>(kProtocolVersion, body.data());
    body.append(name);

    QByteArray packet;
    packet.resize(8);
    qToLittleEndian<quint32>(static_cast<quint32>(Command::Hello), packet.data());
    qToLittleEndian<quint32>(static_cast<quint32>(body.size()), packet.data() + 4);
    packet.append(body);

    m_socket->write(packet);
}

void SpyServerClient::sendSetting(Setting setting, quint32 value)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    QByteArray body;
    body.resize(8);
    qToLittleEndian<quint32>(static_cast<quint32>(setting), body.data());
    qToLittleEndian<quint32>(value, body.data() + 4);

    QByteArray packet;
    packet.resize(8);
    qToLittleEndian<quint32>(static_cast<quint32>(Command::SetSetting), packet.data());
    qToLittleEndian<quint32>(static_cast<quint32>(body.size()), packet.data() + 4);
    packet.append(body);

    m_socket->write(packet);
}

void SpyServerClient::setFrequency(qint64 hz)
{
    m_frequencyHz = hz;
    sendSetting(Setting::IqFrequency, static_cast<quint32>(hz));
}

void SpyServerClient::setDecimationStage(int stage)
{
    m_decimationStage = std::max(0, stage);
    sendSetting(Setting::IqDecimation, static_cast<quint32>(m_decimationStage));
}

void SpyServerClient::setGainIndex(int index)
{
    m_gainIndex = std::max(0, index);
    sendSetting(Setting::GainMode, static_cast<quint32>(m_gainIndex));
}

void SpyServerClient::startStreaming()
{
    // L'ordine conta: prima si dice *che cosa* si vuole, poi si accende il
    // flusso. Un server che riceve "enabled" senza formato non sa che mandare.
    sendSetting(Setting::StreamingMode, static_cast<quint32>(StreamingMode::Iq));
    sendSetting(Setting::IqFormat, static_cast<quint32>(SampleFormat::Int16));
    sendSetting(Setting::IqDecimation, static_cast<quint32>(m_decimationStage));
    sendSetting(Setting::IqFrequency, static_cast<quint32>(m_frequencyHz));
    sendSetting(Setting::StreamingEnabled, 1);
    m_streaming = true;
}

void SpyServerClient::onReadyRead()
{
    if (!m_socket)
        return;

    m_pending.append(m_socket->readAll());
    if (!parseMessages()) {
        emit failed(tr("Messaggio non valido dal server SpyServer."), true);
        disconnectFromServer();
    }
}

bool SpyServerClient::parseMessages()
{
    while (m_pending.size() >= kMessageHeaderSize) {
        const char *data = m_pending.constData();

        MessageHeader header;
        header.protocolId = qFromLittleEndian<quint32>(data);
        header.messageType = qFromLittleEndian<quint32>(data + 4);
        header.streamType = qFromLittleEndian<quint32>(data + 8);
        header.sequenceNumber = qFromLittleEndian<quint32>(data + 12);
        header.bodySize = qFromLittleEndian<quint32>(data + 16);

        // Un corpo assurdo significa che ci siamo persi nel flusso: meglio
        // chiudere che continuare a interpretare byte a caso.
        if (header.bodySize > (16u << 20))
            return false;

        if (static_cast<quint32>(m_pending.size()) < kMessageHeaderSize + header.bodySize)
            break;   // messaggio incompleto: si attende il resto

        const QByteArray body = m_pending.mid(kMessageHeaderSize,
                                              static_cast<int>(header.bodySize));
        m_pending.remove(0, kMessageHeaderSize + static_cast<int>(header.bodySize));

        switch (static_cast<MessageType>(header.messageType)) {
        case MessageType::DeviceInfo:
            handleDeviceInfo(body);
            break;
        case MessageType::Uint8Iq:
        case MessageType::Int16Iq:
        case MessageType::Float32Iq:
            handleIqData(body, static_cast<MessageType>(header.messageType));
            break;
        case MessageType::ClientSync:
        case MessageType::PongData:
        case MessageType::ReadSetting:
            break;   // informativi: non cambiano ciò che facciamo
        default:
            break;
        }
    }

    return true;
}

void SpyServerClient::handleDeviceInfo(const QByteArray &body)
{
    if (body.size() < kDeviceInfoSize)
        return;

    const char *data = body.constData();
    m_deviceInfo.deviceType = qFromLittleEndian<quint32>(data);
    m_deviceInfo.deviceSerial = qFromLittleEndian<quint32>(data + 4);
    m_deviceInfo.maximumSampleRate = qFromLittleEndian<quint32>(data + 8);
    m_deviceInfo.maximumBandwidth = qFromLittleEndian<quint32>(data + 12);
    m_deviceInfo.decimationStageCount = qFromLittleEndian<quint32>(data + 16);
    m_deviceInfo.gainStageCount = qFromLittleEndian<quint32>(data + 20);
    m_deviceInfo.maximumGainIndex = qFromLittleEndian<quint32>(data + 24);
    m_deviceInfo.minimumFrequency = qFromLittleEndian<quint32>(data + 28);
    m_deviceInfo.maximumFrequency = qFromLittleEndian<quint32>(data + 32);
    m_deviceInfo.resolution = qFromLittleEndian<quint32>(data + 36);
    m_deviceInfo.minimumIqDecimation = qFromLittleEndian<quint32>(data + 40);
    m_deviceInfo.forcedIqFormat = qFromLittleEndian<quint32>(data + 44);

    m_deviceInfoReceived = true;

    qCInfo(dsdrHal) << "spyserver: device" << deviceTypeName(m_deviceInfo.deviceType)
                    << "rate max" << m_deviceInfo.maximumSampleRate
                    << "stadi decimazione" << m_deviceInfo.decimationStageCount;

    emit connected(m_deviceInfo);

    if (!m_streaming)
        startStreaming();
}

void SpyServerClient::handleIqData(const QByteArray &body, MessageType type)
{
    if (body.isEmpty() || !m_ring)
        return;

    std::size_t floats = 0;

    switch (type) {
    case MessageType::Uint8Iq: {
        const auto *raw = reinterpret_cast<const quint8 *>(body.constData());
        floats = std::min(static_cast<std::size_t>(body.size()), m_scratch.size());
        const float *table = uint8Table();
        for (std::size_t i = 0; i < floats; ++i)
            m_scratch[i] = table[raw[i]];
        break;
    }
    case MessageType::Int16Iq: {
        const auto *raw = reinterpret_cast<const char *>(body.constData());
        const std::size_t count = std::min(static_cast<std::size_t>(body.size()) / 2,
                                           m_scratch.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto value = static_cast<qint16>(
                qFromLittleEndian<quint16>(raw + i * 2));
            m_scratch[i] = static_cast<float>(value) / 32768.0f;
        }
        floats = count;
        break;
    }
    case MessageType::Float32Iq: {
        const std::size_t count = std::min(static_cast<std::size_t>(body.size()) / 4,
                                           m_scratch.size());
        std::memcpy(m_scratch.data(), body.constData(), count * sizeof(float));
        floats = count;
        break;
    }
    default:
        return;
    }

    if (floats == 0)
        return;

    const std::size_t written = m_ring->write(m_scratch.data(), floats);
    const std::size_t frames = written / 2;
    const std::size_t dropped = (floats - written) / 2;

    emit samplesProduced(static_cast<quint32>(frames),
                         static_cast<quint32>(dropped),
                         static_cast<quint64>(m_clock.nsecsElapsed()));
}

void SpyServerClient::onSocketError()
{
    if (!m_socket)
        return;
    const QString message = m_socket->errorString();
    qCWarning(dsdrHal) << "spyserver: errore di socket:" << message;
    emit failed(message, true);
}

void SpyServerClient::onDisconnected()
{
    qCInfo(dsdrHal) << "spyserver: connessione chiusa dal server";
    m_deviceInfoReceived = false;
    m_streaming = false;
    emit disconnected();
}

} // namespace dsdr::hal::nettcp
