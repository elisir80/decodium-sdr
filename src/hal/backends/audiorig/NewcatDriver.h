// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — driver CAT nativo per le Yaesu «newcat» (SPEC-004 §2, §3).
//
// Il protocollo newcat è documentato nei manuali di Yaesu: comandi ASCII di
// pochi caratteri chiusi da punto e virgola, risposta della stessa forma.
// Quello che serve qui sono cinque comandi — identità, frequenza, modo, PTT,
// S-meter — e sono implementati da questa scrittura, non copiati da altro
// codice.
//
// Radio di riferimento: FT-991A, un solo cavo USB per audio e controllo.
// La stessa grammatica vale per FT-891, FT-DX10, FT-710 e famiglia.
#pragma once

#include "hal/backends/audiorig/ICatDriver.h"

#include <QByteArray>
#include <memory>

class QSerialPort;

namespace dsdr::hal::audiorig {

class NewcatDriver : public ICatDriver
{
public:
    NewcatDriver();
    ~NewcatDriver() override;

    QString driverId() const override { return QStringLiteral("newcat"); }

    bool open(const QString &portName, const CatSerialConfig &serial) override;
    void close() override;
    bool isOpen() const override;

    QString radioModel() const override { return m_model; }

    bool poll(CatState &state) override;
    bool setFrequency(qint64 hz) override;
    bool setMode(DemodMode mode) override;
    bool setPtt(bool transmit) override;

    QString errorString() const override { return m_error; }
    QList<int> candidateBaudRates() const override;
    int probe(const QString &portName) override;

    /// Conversione fra il codice di modo newcat e il nostro vocabolario.
    /// Esposte perché sono la parte che si sbaglia, ed è giusto che un test
    /// possa guardarle senza una radio attaccata.
    static DemodMode modeFromCode(char code);
    static char codeFromMode(DemodMode mode);

    /// Il modello ricavato dalla risposta a `ID;`. Statica per lo stesso
    /// motivo: `ID0670;` è un FT-991A, e saperlo non richiede la radio.
    static QString modelFromId(const QByteArray &reply);

private:
    /// Apre la porta con la configurazione scelta. Con RTS/CTS la linea RTS
    /// la governa Qt, come richiedono le Yaesu con «CAT RTS» abilitato.
    bool openPort(const QString &portName, const CatSerialConfig &serial);

    /// Chiede `ID;` e ne ricava il modello. Vero se dall'altra parte c'è una
    /// radio che parla newcat.
    bool identify();

    /// Manda un comando e aspetta la risposta terminata da `;`.
    /// Vuota se la radio non risponde entro il tempo: è il caso normale
    /// quando si sonda una porta che non è una radio.
    QByteArray ask(const QByteArray &command, int timeoutMs = 300);
    bool tell(const QByteArray &command);

    std::unique_ptr<QSerialPort> m_port;
    QString m_model;
    QString m_error;
};

} // namespace dsdr::hal::audiorig
