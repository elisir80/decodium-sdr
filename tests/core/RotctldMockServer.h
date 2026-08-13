// SPDX-License-Identifier: GPL-3.0-or-later
// Un finto `rotctld` che sta su una porta e si comporta come i due che
// esistono davvero.
//
// Serve perché il rotore è la cosa meno provabile che ci sia: c'è un motore in
// cima a una torre, e chi scrive il codice quasi mai ce l'ha sotto mano. Un
// server finto non prova che l'antenna gira — quello lo prova solo l'antenna —
// ma prova tutto il resto, che è dove stanno gli errori: il dialetto sbagliato,
// una risposta letta a metà, una coda che si sfasa di un giro e da lì in poi
// risponde sempre alla domanda precedente.
//
// Parla i due dialetti veri, e si può dirgli quale: `rotctld` di hamlib capisce
// il prefisso `+` e chiude con `RPRT`, i server minimi rispondono i soli valori
// e basta.
#pragma once

#include <QObject>
#include <QTcpServer>

namespace dsdr::test {

class RotctldMockServer : public QObject
{
    Q_OBJECT

public:
    enum class Dialect {
        Extended,   ///< come `rotctld`: `Azimuth: 123.4` … `RPRT 0`
        Short,      ///< come i server minimi: i soli valori, senza esito
    };

    explicit RotctldMockServer(Dialect dialect, QObject *parent = nullptr);

    bool listen();
    quint16 port() const { return m_server.serverPort(); }

    void setPosition(double azimuth, double elevation);
    double targetAzimuth() const { return m_targetAzimuth; }
    double targetElevation() const { return m_targetElevation; }

    /// Quante volte gli è stato detto di muoversi. È il contatore che prende
    /// il difetto che nessuno cerca: un comando mandato cento volte perché
    /// qualcuno stava trascinando un cursore.
    int moveCount() const { return m_moveCount; }
    int stopCount() const { return m_stopCount; }

    /// Da qui in poi non risponde più. Serve a provare che chi aspetta non
    /// resta appeso per sempre.
    void goSilent() { m_silent = true; }

    /// Chiude il collegamento sotto il naso di chi sta parlando.
    void hangUp();

private:
    void onConnection();
    void onLine(const QByteArray &line);

    QTcpServer m_server;
    QTcpSocket *m_client = nullptr;
    Dialect m_dialect;

    double m_azimuth = 0.0;
    double m_elevation = 0.0;
    double m_targetAzimuth = -1.0;
    double m_targetElevation = 0.0;
    int m_moveCount = 0;
    int m_stopCount = 0;
    bool m_silent = false;
    QByteArray m_pending;
};

} // namespace dsdr::test
