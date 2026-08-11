// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il polling CAT, su un thread suo (SPEC-004 §2).
//
// Il dialogo con una porta seriale è fatto di attese: ogni lettura costa
// qualche millisecondo, e a 5 Hz per la frequenza e 10 Hz per l'S-meter sono
// decine di attese al secondo. Farle sul thread del seam vorrebbe dire un
// backend che ogni tanto non risponde, e nessuno collegherebbe la cosa alla
// porta seriale.
//
// I comandi verso la radio arrivano come slot accodati: il thread li esegue
// fra un giro di polling e l'altro, e chi li manda non aspetta.
#pragma once

#include "hal/backends/audiorig/ICatDriver.h"

#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace dsdr::hal::audiorig {

class CatController : public QObject
{
    Q_OBJECT

public:
    explicit CatController(std::unique_ptr<ICatDriver> driver, QObject *parent = nullptr);
    ~CatController() override;

signals:
    /// Una lettura completa. Il modo viaggia come intero perché attraversa i
    /// thread e non vale la pena registrare un metatype per un enum.
    /// `signalDbm` è NaN quando la radio non sa dire un livello tarato: allora
    /// vale `sMeterRaw`, che è la lettura grezza del suo strumento.
    void stateRead(qint64 frequencyHz, int mode, bool transmitting, int sMeterRaw,
                   double signalDbm);

    /// La radio ha smesso di rispondere. È una cosa seria: senza CAT il
    /// panadattatore non sa più dove sta, e continuare a disegnarlo sulla
    /// vecchia frequenza sarebbe peggio che fermarsi.
    void lost(const QString &reason);

    void opened(const QString &radioModel, const QString &portName, int baudRate);

public slots:
    /// Apre la porta indicata. Con `baudRate` a zero le prova tutte, dalla più
    /// probabile: è la differenza fra «scegli FT-991A e funziona» e una
    /// tendina di velocità da indovinare.
    void open(const QString &portName, int baudRate);
    void close();

    void setFrequency(qint64 hz);
    void setMode(int mode);
    void setPtt(bool transmit);

    /// Cadenza del polling in millisecondi. La specifica dice 5 Hz per
    /// frequenza e modo: 200 ms.
    void setPollInterval(int milliseconds);

private slots:
    void poll();

private:
    std::unique_ptr<ICatDriver> m_driver;
    QTimer *m_timer = nullptr;
    CatState m_state;
    int m_failures = 0;
};

} // namespace dsdr::hal::audiorig
