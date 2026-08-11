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
// **Due dialetti, e si riconoscono.** `rigctld` accetta il prefisso `+` e
// allora risponde con righe `Nome: valore` chiuse da `RPRT n`: è la forma che
// non si può fraintendere, e quella che si preferisce. Ma «rigctl_net» non è
// solo `rigctld`: mezzo ecosistema espone un server rigctl **minimo**, che del
// protocollo implementa la parte corta — i soli valori, una riga per ciascuno,
// senza esito — e ignora il `+`. Fra questi c'è DECODIUM 4, che è il motivo
// per cui questo driver esiste sul banco di chi lo scrive: la porta seriale
// della radio ce l'ha lui, e il CAT lo si prende da lì. Il dialetto si
// stabilisce all'apertura con una domanda di sola lettura, e da quel momento
// non si indovina più niente.
//
// **Come si riconosce una radio.** Non da `dump_caps`: il server minimo non ce
// l'ha. Si chiede la frequenza, e una frequenza plausibile è la prova che
// dall'altra parte c'è una radio — con il nome del modello, se `dump_caps`
// c'è, altrimenti senza. Non attaccarsi al proprio server rigctl è compito di
// chi decide gli indirizzi da sondare: il core dichiara la porta che si è
// preso e il backend la salta.
//
// **Niente hamlib fra le dipendenze.** Il protocollo di rigctl è testo ed è
// documentato nella sua pagina di manuale; questa è una scrittura nuova.
#pragma once

#include "hal/backends/audiorig/ICatDriver.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

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

    /// Da `STRENGTH` di Hamlib ai dBm.
    ///
    /// Hamlib dà i decibel rispetto a S9, che per definizione IARU è −73 dBm
    /// sotto i 30 MHz. È una misura tarata dal profilo della radio, e va
    /// consegnata così com'è: farla passare per la scala grezza 0…255 — quella
    /// che una radio manda sulla seriale, il cui significato dipende dal
    /// modello — vorrebbe dire buttare via proprio ciò che la rende utile.
    static double dbmFromStrengthDb(int strengthDb);

    /// Quale delle due forme del protocollo parla il server dall'altra parte.
    enum class Dialect {
        Extended,   ///< `+comando` → righe `Nome: valore`, poi `RPRT n`
        Short,      ///< `comando` → i soli valori, una riga ciascuno
    };

    /// Ciò che torna da una domanda.
    struct Reply
    {
        /// I valori, nell'ordine in cui li manda il protocollo. In forma estesa
        /// sono già ripuliti del nome del campo.
        QStringList values;

        /// La risposta grezza, per chi deve leggerci dentro un campo per nome.
        QByteArray raw;

        /// L'esito dichiarato, quando c'è: zero è riuscito. Il protocollo corto
        /// lo manda solo per i comandi che non restituiscono valori, o quando
        /// qualcosa è andato storto.
        int rprt = 0;

        /// Se una risposta è arrivata. Falso vuol dire dialogo interrotto o
        /// tempo scaduto, che è una cosa diversa da «la radio ha detto di no».
        bool answered = false;

        bool ok() const { return answered && rprt == 0; }
    };

private:
    /// Manda un comando e aspetta la risposta.
    ///
    /// `expectedValues` è quante righe di valore il comando restituisce: serve
    /// solo al protocollo corto, che non manda un terminatore e va contato. In
    /// forma estesa si aspetta comunque `RPRT`, che è il terminatore vero.
    /// Zero significa «aspetta `RPRT`»: è il caso dei comandi che non
    /// restituiscono valori, e di `dump_caps`.
    Reply ask(const QByteArray &command, int expectedValues = 0, int timeoutMs = 700);

    /// Come sopra, ma interessa solo se è andata bene.
    bool tell(const QByteArray &command);

    /// Stabilisce il dialetto con una domanda di sola lettura, e restituisce la
    /// frequenza che ha letto per strada — che è anche la prova che dall'altra
    /// parte c'è una radio.
    qint64 negotiate();

    std::unique_ptr<QTcpSocket> m_socket;
    QString m_endpoint;
    QString m_model;
    QString m_error;
    Dialect m_dialect = Dialect::Extended;

    /// Se la radio sa dire l'S-meter. Alcuni backend di Hamlib non lo
    /// implementano, e chiederglielo cinque volte al secondo per tutta la
    /// sessione è tempo speso a farsi dire di no.
    bool m_strengthAvailable = true;
};

} // namespace dsdr::hal::audiorig
