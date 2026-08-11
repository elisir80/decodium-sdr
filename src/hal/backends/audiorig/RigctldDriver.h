// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — driver CAT via `rigctld`, il demone di rete di Hamlib.
//
// Hamlib parla con qualche centinaio di modelli di ricetrasmettitore, e nessuno
// di noi ha quei modelli sul tavolo per scriverne i driver. `rigctld` è il modo
// in cui quel lavoro si riusa senza linkare hamlib e senza copiarne una riga:
// è un demone che tiene la porta seriale e accetta comandi su TCP.
//
//   rigctld -m 1035 -r COM5 -s 38400 -t 4532
//
// Da qui in giù è un `ICatDriver` come gli altri: il backend `audiorig` non sa
// che dall'altra parte c'è una rete invece di una seriale, e non deve saperlo.
// Cambia solo che `portName` è un `host:porta` e che la velocità non esiste —
// il seam la porta comunque, perché per una seriale serve, e qui si ignora.
//
// Tre cose che valgono la pena di essere dette prima di leggere il codice:
//
// **Il protocollo esteso.** Ogni comando si manda preceduto da `+`, e allora
// `rigctld` risponde con righe `Nome: valore` e chiude con `RPRT n`. Il
// protocollo corto risponde con i soli valori, uno per riga, e chi legge deve
// sapere a memoria quanti sono e in che ordine — un modo eccellente di
// interpretare la passband come se fosse il modo. Costa qualche byte in più su
// una connessione che di solito è verso 127.0.0.1.
//
// **Non ci si attacca a sé stessi.** DECODIUM SDR espone a sua volta un server
// rigctl sulla 4532, e sondare quella porta trova noi. La sonda pretende una
// risposta a `+\dump_caps`, che il nostro server non implementa: il giro si
// chiude prima di cominciare. Non è un accorgimento fragile — è la stessa
// domanda che serve comunque a sapere che radio c'è dall'altra parte.
//
// **Niente hamlib fra le dipendenze.** Il protocollo di rigctl è testo ed è
// documentato nella sua pagina di manuale; questa è una scrittura nuova.
#pragma once

#include "hal/backends/audiorig/ICatDriver.h"

#include <QByteArray>
#include <QString>

#include <memory>

class QTcpSocket;

namespace dsdr::hal::audiorig {

class RigctldDriver : public ICatDriver
{
public:
    RigctldDriver();
    ~RigctldDriver() override;

    QString driverId() const override { return QStringLiteral("rigctld"); }

    /// `portName` è `host:porta`, oppure il solo host: la porta di fabbrica di
    /// `rigctld` è la 4532. `baudRate` non ha significato su una connessione di
    /// rete e viene ignorato.
    bool open(const QString &portName, int baudRate) override;
    void close() override;
    bool isOpen() const override;

    QString radioModel() const override { return m_model; }

    bool poll(CatState &state) override;
    bool setFrequency(qint64 hz) override;
    bool setMode(DemodMode mode) override;
    bool setPtt(bool transmit) override;

    QString errorString() const override { return m_error; }

    /// Nessuna: la velocità di linea la governa `rigctld`, che è quello che ha
    /// la porta seriale in mano. Restituire un elenco vuoto è più onesto che
    /// fingere una tendina che non comanda niente.
    QList<int> candidateBaudRates() const override { return {}; }

    /// Prova a connettersi e chiede chi c'è. Restituisce 0 — «connesso, la
    /// velocità non si applica» — oppure −1.
    int probe(const QString &portName) override;

    // ── Le parti che si possono verificare senza un rigctld acceso ──────

    /// Il vocabolario dei modi di Hamlib e il nostro. Sono la parte che si
    /// sbaglia, ed è giusto che un test possa guardarla da sola.
    static DemodMode modeFromName(const QString &name);
    static QString nameFromMode(DemodMode mode);

    /// Estrae un campo da una risposta estesa (`Frequency: 14074000`).
    /// Stringa vuota se il campo non c'è.
    static QString fieldValue(const QByteArray &reply, const QString &field);

    /// Il modello dichiarato da `\dump_caps`, che è la riga `Model name:`.
    static QString modelFromCaps(const QByteArray &reply);

    /// Divide `host:porta` nei suoi due pezzi, con la 4532 come porta di
    /// fabbrica. Un indirizzo IPv6 fra parentesi quadre resta intero.
    static bool splitEndpoint(const QString &endpoint, QString &host, quint16 &port);

    /// Se una risposta si chiude con `RPRT 0`, cioè se il comando è riuscito.
    /// Una risposta che c'è non vuol dire che sia andata bene: chi ascolta su
    /// quella porta senza essere rigctld risponde comunque.
    static bool succeeded(const QByteArray &reply);

    /// Da `STRENGTH` di Hamlib ai dBm.
    ///
    /// Hamlib dà i decibel rispetto a S9, che per definizione IARU è −73 dBm
    /// sotto i 30 MHz. È una misura tarata dal profilo della radio, e va
    /// consegnata così com'è: farla passare per la scala grezza 0…255 — quella
    /// che una radio manda sulla seriale, il cui significato dipende dal
    /// modello — vorrebbe dire buttare via proprio ciò che la rende utile.
    static double dbmFromStrengthDb(int strengthDb);

private:
    /// Manda un comando in forma estesa e restituisce tutta la risposta fino a
    /// `RPRT`. Vuota se il dialogo si è interrotto o è scaduto il tempo.
    QByteArray ask(const QByteArray &command, int timeoutMs = 700);

    /// Come sopra, ma interessa solo se `RPRT` ha detto zero.
    bool tell(const QByteArray &command);

    std::unique_ptr<QTcpSocket> m_socket;
    QString m_endpoint;
    QString m_model;
    QString m_error;

    /// Se la radio sa dire l'S-meter. Alcuni backend di Hamlib non lo
    /// implementano, e chiederglielo cinque volte al secondo per tutta la
    /// sessione è tempo speso a farsi dire di no.
    bool m_strengthAvailable = true;
};

} // namespace dsdr::hal::audiorig
