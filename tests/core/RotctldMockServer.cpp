// SPDX-License-Identifier: GPL-3.0-or-later
#include "RotctldMockServer.h"

#include <QTcpSocket>

namespace dsdr::test {

RotctldMockServer::RotctldMockServer(Dialect dialect, QObject *parent)
    : QObject(parent)
    , m_dialect(dialect)
{
    connect(&m_server, &QTcpServer::newConnection, this,
            &RotctldMockServer::onConnection);
}

bool RotctldMockServer::listen()
{
    return m_server.listen(QHostAddress::LocalHost, 0);
}

void RotctldMockServer::setPosition(double azimuth, double elevation)
{
    m_azimuth = azimuth;
    m_elevation = elevation;
}

void RotctldMockServer::hangUp()
{
    if (m_client)
        m_client->abort();
}

void RotctldMockServer::onConnection()
{
    m_client = m_server.nextPendingConnection();
    connect(m_client, &QTcpSocket::readyRead, this, [this] {
        m_pending += m_client->readAll();
        int newline = m_pending.indexOf('\n');
        while (newline >= 0) {
            const QByteArray line = m_pending.left(newline).trimmed();
            m_pending.remove(0, newline + 1);
            newline = m_pending.indexOf('\n');
            if (!line.isEmpty())
                onLine(line);
        }
    });
}

void RotctldMockServer::onLine(const QByteArray &line)
{
    if (m_silent || !m_client)
        return;

    // Il prefisso `+` chiede la forma estesa. Un server minimo non lo capisce e
    // lo prende come parte del nome del comando: da lì in poi non risponde più
    // a niente, ed è esattamente il comportamento da riprodurre.
    const bool wantsExtended = line.startsWith('+');
    if (wantsExtended && m_dialect == Dialect::Short)
        return;

    const auto say = [this](const QByteArray &text) { m_client->write(text); };

    QByteArray verb = wantsExtended ? line.mid(1) : line;

    // La barra rovesciata **serve**, e il finto rotore la pretende come la
    // pretende quello vero: in rotctl i comandi lunghi si scrivono `\get_pos`,
    // e senza la barra `get_pos` verrebbe letto come un comando di una lettera
    // seguito da spazzatura.
    //
    // Al primo giro questo controllo non c'era, il finto rotore accettava
    // entrambe le forme, e un `"\\get_pos"` scritto con una barra sola — che
    // in C++ diventa `get_pos` — passava tutta la suite e sarebbe stato
    // rifiutato dal primo rotore vero. Lo ha preso `-Werror`, non il test: un
    // simulatore più permissivo dell'originale non simula, assolve.
    if (!verb.startsWith('\\')) {
        say(QByteArrayLiteral("RPRT -11\n"));
        return;
    }
    verb = verb.mid(1);

    const QList<QByteArray> parts = verb.split(' ');
    const QByteArray command = parts.value(0);

    if (command == "get_pos") {
        if (m_dialect == Dialect::Extended) {
            say(QByteArrayLiteral("Azimuth: ")
                + QByteArray::number(m_azimuth, 'f', 2) + "\n");
            say(QByteArrayLiteral("Elevation: ")
                + QByteArray::number(m_elevation, 'f', 2) + "\n");
            say(QByteArrayLiteral("RPRT 0\n"));
        } else {
            say(QByteArray::number(m_azimuth, 'f', 2) + "\n");
            say(QByteArray::number(m_elevation, 'f', 2) + "\n");
        }
        return;
    }

    if (command == "set_pos") {
        m_targetAzimuth = parts.value(1).toDouble();
        m_targetElevation = parts.value(2).toDouble();
        ++m_moveCount;
        say(QByteArrayLiteral("RPRT 0\n"));
        return;
    }

    if (command == "stop") {
        ++m_stopCount;
        m_targetAzimuth = -1.0;
        say(QByteArrayLiteral("RPRT 0\n"));
        return;
    }

    if (command == "park") {
        say(QByteArrayLiteral("RPRT 0\n"));
        return;
    }

    if (command == "dump_caps") {
        if (m_dialect != Dialect::Extended) {
            // Un server minimo non ce l'ha, e risponde che non lo conosce.
            say(QByteArrayLiteral("RPRT -11\n"));
            return;
        }
        say(QByteArrayLiteral("Model name:\tGS-232B\n"));
        say(QByteArrayLiteral("Min Azimuth:\t0.00\n"));
        say(QByteArrayLiteral("Max Azimuth:\t450.00\n"));
        say(QByteArrayLiteral("Min Elevation:\t0.00\n"));
        say(QByteArrayLiteral("Max Elevation:\t180.00\n"));
        say(QByteArrayLiteral("RPRT 0\n"));
        return;
    }

    say(QByteArrayLiteral("RPRT -11\n"));
}

} // namespace dsdr::test
