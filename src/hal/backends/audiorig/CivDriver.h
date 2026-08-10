// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — driver CAT per le Icom (CI-V).
//
// Il CI-V è documentato nei manuali Icom. È un bus, non un collegamento punto
// a punto, e da lì vengono le sue due particolarità:
//
//   - ogni telaio ha un **indirizzo**, e i comandi lo portano dentro. Un
//     IC-7300 è 0x94, un IC-7610 0x98, un IC-7851 0x8E. Chi non conosce
//     l'indirizzo non riceve risposta, ed è per questo che si sonda.
//
//   - la radio **rimanda indietro** quello che le si è scritto, perché sul bus
//     tutti sentono tutti. Chi non scarta la propria eco legge come risposta
//     la domanda che ha appena fatto, e su una lettura di frequenza vuol dire
//     leggere la frequenza che si stava per impostare.
//
// La frequenza viaggia in BCD, cinque byte, dalle unità ai gigahertz. È
// l'aritmetica che si sbaglia in silenzio: un byte invertito non produce un
// errore, produce una frequenza plausibile — 14,074 che diventa 47,041 — e la
// si attribuisce alla radio.
#pragma once

#include "hal/backends/audiorig/ICatDriver.h"

#include <QByteArray>

#include <memory>

class QSerialPort;

namespace dsdr::hal::audiorig {

class CivDriver : public ICatDriver
{
public:
    CivDriver();
    ~CivDriver() override;

    QString driverId() const override { return QStringLiteral("civ"); }

    bool open(const QString &portName, int baudRate) override;
    void close() override;
    bool isOpen() const override;

    QString radioModel() const override { return m_model; }

    bool poll(CatState &state) override;
    bool setFrequency(qint64 hz) override;
    bool setMode(DemodMode mode) override;
    bool setPtt(bool transmit) override;

    QString errorString() const override { return m_error; }
    QList<int> candidateBaudRates() const override;

    // ── Le parti pure ───────────────────────────────────────────────────
    //
    // Statiche apposta: la codifica BCD e la tabella dei modi si sbagliano
    // senza far rumore, e vanno verificate senza una radio attaccata.

    /// La frequenza nei cinque byte BCD del CI-V, dal meno significativo.
    static QByteArray frequencyToBcd(qint64 hz);

    /// E il verso opposto. Restituisce −1 se i byte non sono BCD validi: una
    /// cifra maggiore di nove non è un numero, è un pacchetto sbagliato.
    static qint64 bcdToFrequency(const QByteArray &bcd);

    static DemodMode modeFromCode(quint8 code);
    static quint8 codeFromMode(DemodMode mode);

    /// Il modello dall'indirizzo del telaio.
    static QString modelFromAddress(quint8 address);

    /// Gli indirizzi da provare, dai più diffusi.
    static QList<quint8> candidateAddresses();

    /// Costruisce un comando completo, preambolo e chiusura compresi.
    static QByteArray buildFrame(quint8 radioAddress, const QByteArray &payload);

private:
    /// Manda un comando e aspetta la risposta della radio, scartando l'eco.
    QByteArray ask(const QByteArray &payload, int timeoutMs = 300);
    bool tell(const QByteArray &payload);

    std::unique_ptr<QSerialPort> m_port;
    quint8 m_address = 0;
    QString m_model;
    QString m_error;
};

} // namespace dsdr::hal::audiorig
