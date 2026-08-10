// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/flex/FlexClient.h"
#include "hal/backends/flex/FlexProtocol.h"
#include "hal/HalLog.h"

#include <QTcpSocket>

namespace dsdr::hal::flex {

namespace {
constexpr quint16 kCommandPort = 4992;
} // namespace

FlexClient::FlexClient(QObject *parent)
    : QObject(parent)
{
}

FlexClient::~FlexClient()
{
    disconnectFrom();
}

void FlexClient::connectTo(const QString &address, int timeoutMs)
{
    disconnectFrom();

    m_socket = std::make_unique<QTcpSocket>();
    connect(m_socket.get(), &QTcpSocket::readyRead, this, &FlexClient::readLines);
    connect(m_socket.get(), &QTcpSocket::disconnected, this, [this] {
        emit disconnected();
    });
    connect(m_socket.get(), &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                emit failed(m_socket ? m_socket->errorString() : QString());
            });

    m_socket->connectToHost(address, kCommandPort);
    if (!m_socket->waitForConnected(timeoutMs)) {
        emit failed(tr("Nessuna risposta da %1 sulla porta %2: %3")
                        .arg(address).arg(kCommandPort)
                        .arg(m_socket->errorString()));
        m_socket.reset();
        return;
    }

    qCInfo(dsdrHal) << "flex: collegato a" << address;
    emit connected();

    // La radio si presenta da sé — versione e handle — e poi manda gli stati
    // senza che nessuno chieda. Non serve interrogarla per sapere chi è.
}

void FlexClient::disconnectFrom()
{
    if (!m_socket)
        return;
    m_socket->disconnectFromHost();
    m_socket.reset();
    m_buffer.clear();
    m_version.clear();
    m_handle.clear();
    m_radio.clear();
    m_slices.clear();
}

bool FlexClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

quint32 FlexClient::send(const QString &command)
{
    if (!isConnected())
        return 0;
    const quint32 sequence = ++m_sequence;
    m_socket->write(buildCommand(sequence, command).toUtf8());
    return sequence;
}

QStringList FlexClient::slices() const
{
    QStringList list = m_slices.values();
    list.sort();
    return list;
}

void FlexClient::readLines()
{
    if (!m_socket)
        return;

    m_buffer += m_socket->readAll();

    // Le righe si spezzano fra un pacchetto TCP e l'altro: accumulare fino al
    // ritorno a capo è l'unico modo di non troncarne una a metà, che qui
    // vorrebbe dire perdere uno stato e non accorgersene.
    int newline = m_buffer.indexOf('\n');
    while (newline >= 0) {
        const QString line = QString::fromUtf8(m_buffer.left(newline));
        m_buffer.remove(0, newline + 1);
        handleLine(line);
        newline = m_buffer.indexOf('\n');
    }
}

void FlexClient::handleLine(const QString &raw)
{
    const Line line = parseLine(raw);

    switch (line.kind) {
    case LineKind::Version:
        m_version = line.payload;
        break;

    case LineKind::Handle:
        m_handle = line.handle;
        break;

    case LineKind::Response:
        emit responseReceived(line.sequence, line.code, line.payload);
        if (line.isError()) {
            qCWarning(dsdrHal) << "flex: comando" << line.sequence
                               << "fallito, codice" << Qt::hex << line.code;
        }
        break;

    case LineKind::Status: {
        const QHash<QString, QString> fields = parseFields(line.payload);

        // Gli stati arrivano a raffica e riguardano cose diverse: interessano
        // quelli della radio e quelli delle fette, che sono ciò che si può
        // mostrare senza ricevere un campione.
        if (line.payload.startsWith(QLatin1String("radio"))) {
            for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
                m_radio.insert(it.key(), it.value());
        } else if (line.payload.startsWith(QLatin1String("slice "))) {
            const QString index = line.payload.section(QLatin1Char(' '), 1, 1);
            const QString frequency = fields.value(QStringLiteral("RF_frequency"));
            const QString mode = fields.value(QStringLiteral("mode"));
            if (!index.isEmpty() && !frequency.isEmpty()) {
                m_slices.insert(index, QStringLiteral("%1 MHz %2")
                                           .arg(frequency, mode));
            }
        }

        const QString summary = describeRadio(m_radio);
        if (!summary.isEmpty())
            emit described(summary);
        break;
    }

    case LineKind::Message:
        qCInfo(dsdrHal) << "flex:" << line.payload;
        break;

    case LineKind::Unknown:
        break;
    }
}

} // namespace dsdr::hal::flex
