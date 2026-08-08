// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — thread di riproduzione di una registrazione IQ.
//
// Consegna i campioni al ritmo con cui furono registrati, non alla velocità
// del disco: una registrazione riprodotta a tutta forza riempirebbe il ring in
// un lampo e il DSP vedrebbe una banda che scorre a caso. L'orologio è lo
// stesso schema del backend demo — si calcola quanti campioni sarebbero dovuti
// uscire da `start()` a ora, e si recupera solo entro un limite.
#pragma once

#include "hal/backends/iqfile/IqFileReader.h"

#include <QElapsedTimer>
#include <QObject>

#include <vector>

class QTimer;

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::hal::iqfile {

class IqFileWorker : public QObject
{
    Q_OBJECT

public:
    explicit IqFileWorker(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~IqFileWorker() override;

public slots:
    /// Apre la registrazione. Emette `opened` o `failed`; da qui in poi il
    /// file appartiene a questo thread e nessun altro lo tocca.
    void openFile(const QString &path);
    void start();
    void stop();
    void setPaused(bool paused);
    void setLoop(bool loop);
    void setSpeed(double factor);
    void seekMs(qint64 ms);

signals:
    void opened(const dsdr::hal::iqfile::RecordingInfo &info);
    void failed(const QString &message);
    void framesProduced(quint32 frames, quint32 dropped, quint64 timestampNs);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    void finished();   ///< fine della registrazione, senza loop attivo

private slots:
    void tick();

private:
    void resetClock();

    dsp::SpscRing<float> *m_ring = nullptr;
    QTimer *m_timer = nullptr;
    IqFileReader m_reader;
    QElapsedTimer m_clock;

    std::vector<float> m_block;
    quint64 m_framesDelivered = 0;   ///< da `resetClock()`, per l'orologio
    bool m_running = false;
    bool m_paused = false;
    bool m_loop = true;
    double m_speed = 1.0;
    qint64 m_lastReportedMs = -1;
};

} // namespace dsdr::hal::iqfile

Q_DECLARE_METATYPE(dsdr::hal::iqfile::RecordingInfo)
