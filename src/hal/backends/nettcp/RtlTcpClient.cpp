// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/nettcp/RtlTcpClient.h"

#include "dsp/SpscRing.h"
#include "hal/HalLog.h"

#include <QTcpSocket>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace dsdr::hal::nettcp {

namespace {

/// Massimo numero di coppie I/Q convertite in un colpo. A 2,048 MS/s sono
/// ~8 ms: abbastanza per non frammentare, abbastanza poco da non far crescere
/// la latenza quando la rete consegna a raffiche.
constexpr int kMaxFramesPerPass = 16384;

/// Tabella di conversione uint8 → float, costruita una volta sola.
///
/// rtl_tcp consegna campioni non segnati centrati su 127,5. Sottrarre e
/// dividere per ogni campione costerebbe due operazioni in virgola mobile su
/// milioni di campioni al secondo; una tabella di 256 voci sta in cache e
/// riduce il tutto a un accesso.
const float *sampleTable()
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

RtlTcpClient::RtlTcpClient(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    m_scratch.resize(static_cast<std::size_t>(kMaxFramesPerPass) * 2);
}

RtlTcpClient::~RtlTcpClient()
{
    disconnectFromServer();
}

void RtlTcpClient::connectToServer(const QString &host, quint16 port)
{
    disconnectFromServer();

    m_socket = new QTcpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    // Il flusso è continuo e denso: un buffer di ricezione generoso evita che
    // una pausa dello scheduler si traduca in campioni persi dal kernel.
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1 << 20);

    connect(m_socket, &QTcpSocket::readyRead, this, &RtlTcpClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &RtlTcpClient::onSocketError);
    connect(m_socket, &QTcpSocket::disconnected, this, &RtlTcpClient::onDisconnected);

    m_greetingReceived = false;
    m_pending.clear();
    if (m_ring)
        m_ring->clear();
    m_clock.start();

    qCInfo(dsdrHal) << "nettcp: connessione a" << host << port;
    m_socket->connectToHost(host, port);
}

void RtlTcpClient::disconnectFromServer()
{
    if (!m_socket)
        return;

    m_socket->disconnect(this);
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->deleteLater();
    m_socket = nullptr;
    m_greetingReceived = false;
    m_pending.clear();
}

void RtlTcpClient::sendCommand(RtlTcpCommand command, qint32 value)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    // Cinque byte: opcode e intero big-endian. Nessun ack, nessuna risposta.
    char packet[5];
    packet[0] = static_cast<char>(command);
    qToBigEndian<quint32>(static_cast<quint32>(value), packet + 1);
    m_socket->write(packet, sizeof(packet));
}

void RtlTcpClient::setFrequency(qint64 hz)
{
    m_frequencyHz = hz;
    sendCommand(RtlTcpCommand::SetFrequency, static_cast<qint32>(hz));
}

void RtlTcpClient::setSampleRate(double rate)
{
    m_sampleRate = rate;
    sendCommand(RtlTcpCommand::SetSampleRate, static_cast<qint32>(rate));
}

void RtlTcpClient::setGain(int gainTenthsDb)
{
    m_gainTenthsDb = gainTenthsDb;
    if (gainTenthsDb < 0) {
        sendCommand(RtlTcpCommand::SetGainMode, 0); // automatico
    } else {
        sendCommand(RtlTcpCommand::SetGainMode, 1);
        sendCommand(RtlTcpCommand::SetGain, gainTenthsDb);
    }
}

void RtlTcpClient::setFrequencyCorrection(int ppm)
{
    m_ppm = ppm;
    sendCommand(RtlTcpCommand::SetFrequencyCorrection, ppm);
}

void RtlTcpClient::setBiasTee(bool enabled)
{
    m_biasTee = enabled;
    sendCommand(RtlTcpCommand::SetBiasTee, enabled ? 1 : 0);
}

void RtlTcpClient::onReadyRead()
{
    if (!m_socket)
        return;

    m_pending.append(m_socket->readAll());

    if (!m_greetingReceived) {
        if (m_pending.size() < kGreetingSize)
            return;
        processGreeting();
        if (!m_greetingReceived)
            return;
    }

    processSamples();
}

void RtlTcpClient::processGreeting()
{
    const char *data = m_pending.constData();

    if (std::memcmp(data, "RTL0", 4) != 0) {
        emit failed(tr("Il server non parla il protocollo rtl_tcp."), true);
        disconnectFromServer();
        return;
    }

    m_tunerType = qFromBigEndian<quint32>(data + 4);
    const quint32 gainCount = qFromBigEndian<quint32>(data + 8);
    m_pending.remove(0, kGreetingSize);
    m_greetingReceived = true;

    qCInfo(dsdrHal) << "nettcp: connesso, tuner" << m_tunerType
                    << "passi di guadagno" << gainCount;

    // rtl_tcp accetta comandi solo dopo l'handshake: è qui che lo stato
    // desiderato diventa stato reale.
    setSampleRate(m_sampleRate);
    setFrequency(m_frequencyHz);
    setFrequencyCorrection(m_ppm);
    setGain(m_gainTenthsDb);
    if (m_biasTee)
        setBiasTee(true);

    emit connected(m_tunerType, gainCount);
}

void RtlTcpClient::processSamples()
{
    const qint64 timestampNs = m_clock.nsecsElapsed();

    while (m_pending.size() >= 2) {
        // Solo coppie complete: un byte spaiato resta in attesa del compagno.
        const int usableBytes =
            std::min<int>(static_cast<int>(m_pending.size()) & ~1, kMaxFramesPerPass * 2);
        const int frames = usableBytes / 2;
        if (frames == 0)
            break;

        const auto *raw = reinterpret_cast<const quint8 *>(m_pending.constData());
        const float *table = sampleTable();
        for (int i = 0; i < usableBytes; ++i)
            m_scratch[static_cast<std::size_t>(i)] = table[raw[i]];

        std::size_t written = 0;
        if (m_ring)
            written = m_ring->write(m_scratch.data(), static_cast<std::size_t>(usableBytes));

        const std::size_t writtenFrames = written / 2;
        const std::size_t dropped = static_cast<std::size_t>(frames) - writtenFrames;

        m_pending.remove(0, usableBytes);

        emit samplesProduced(static_cast<quint32>(writtenFrames),
                             static_cast<quint32>(dropped),
                             static_cast<quint64>(timestampNs));
    }
}

void RtlTcpClient::onSocketError()
{
    if (!m_socket)
        return;

    const QString message = m_socket->errorString();
    qCWarning(dsdrHal) << "nettcp: errore di socket:" << message;
    emit failed(message, true);
}

void RtlTcpClient::onDisconnected()
{
    qCInfo(dsdrHal) << "nettcp: connessione chiusa dal server";
    m_greetingReceived = false;
    emit disconnected();
}

} // namespace dsdr::hal::nettcp
