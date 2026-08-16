// SPDX-License-Identifier: GPL-3.0-or-later
// CAT universale: avvia rigctld per una radio fisicamente collegata al Mac e
// parla poi il protocollo rigctl come ogni altro client.
#pragma once

#include "hal/backends/audiorig/ICatDriver.h"
#include "hal/backends/audiorig/RigctldDriver.h"

#include <memory>

class QProcess;

namespace dsdr::hal::audiorig {

class LocalRigctldDriver final : public ICatDriver
{
public:
    LocalRigctldDriver();
    ~LocalRigctldDriver() override;

    QString driverId() const override { return QStringLiteral("hamlib-local"); }
    bool open(const QString &portName, const CatSerialConfig &serial) override;
    void close() override;
    bool isOpen() const override;
    QString radioModel() const override { return m_model; }

    bool poll(CatState &state) override { return m_remote.poll(state); }
    bool pollPtt(CatState &state) override { return m_remote.pollPtt(state); }
    bool setFrequency(qint64 hz) override { return m_remote.setFrequency(hz); }
    bool setMode(DemodMode mode) override { return m_remote.setMode(mode); }
    bool setPtt(bool transmit) override { return m_remote.setPtt(transmit); }
    QString errorString() const override;

    /// Hamlib riceve il baud selezionato dal dialogo. Con «automatico» si
    /// prova la velocita' di fabbrica piu' comune e alcune alternative.
    QList<int> candidateBaudRates() const override;
    int probe(const QString &portName) override;

    /// Elenco runtime dei modelli che il rigctld installato sa aprire. Non e'
    /// una lista mantenuta a mano: quando Hamlib aggiunge una radio compare
    /// qui, senza una nuova versione di DECODIUM SDR.
    static QVariantList availableModels(QString *error = nullptr);
    static QString rigctldExecutable();

    /// Impostazioni della linea seriale dichiarate dal backend Hamlib per il
    /// modello selezionato. Se Hamlib non e' interrogabile si torna ai valori
    /// conservativi 9600/8N1 senza handshake: il profilo resta modificabile.
    static QVariantMap serialDefaultsForModel(int hamlibModel);

    /// Parser puro della risposta `rigctld -m <id> -L`, esposto per la suite
    /// senza dover avere ne' una radio ne' Hamlib installato durante i test.
    static QVariantMap serialDefaultsFromListing(const QString &listing);

    /// Con RTS/CTS Hamlib deve lasciare RTS non forzato: e' il driver seriale
    /// a commutarlo seguendo CTS. Forzarlo OFF annulla l'handshake.
    static QString serialControlLineStateName(int flowControl, bool asserted);

    /// QSerialPort enumera normalmente solo `tty.usbserial-*`, mentre
    /// rigctld apre un nodo del filesystem. Su Unix richiede quindi il
    /// prefisso `/dev/`; su Windows COMx resta invariato.
    static QString serialDevicePathForRigctld(const QString &portName);

private:
    bool openForBaud(const QString &portName, CatSerialConfig serial);
    static quint16 findFreeLoopbackPort();
    static QString serialParityName(int parity);
    static QString serialHandshakeName(int flowControl);
    static QString serialStopBitsName(int stopBits);
    QString processError() const;

    std::unique_ptr<QProcess> m_process;
    RigctldDriver m_remote;
    QString m_model;
    QString m_error;
};

} // namespace dsdr::hal::audiorig
