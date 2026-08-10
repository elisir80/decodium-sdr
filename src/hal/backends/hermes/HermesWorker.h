// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il dialogo UDP con una radio OpenHPSDR, su un thread suo.
//
// Il protocollo 1 non è una radio che parla e un client che ascolta: sono due
// flussi. La radio manda campioni, e il PC **deve** rispondere con un pacchetto
// per ognuno — anche in sola ricezione, anche senza niente da trasmettere —
// perché quei pacchetti sono l'unico veicolo dei byte di comando. Un client che
// tacesse riceverebbe campioni per sempre, sempre dalla stessa frequenza,
// senza modo di cambiarla.
//
// A 384 kS/s sono circa 3000 pacchetti al secondo in ciascun verso: sta su un
// thread proprio, e nella sua callback non alloca.
#pragma once

#include "hal/backends/hermes/HermesProtocol.h"

#include <QHostAddress>
#include <QObject>

#include <atomic>
#include <memory>
#include <vector>

class QUdpSocket;

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::hal::hermes {

class HermesWorker : public QObject
{
    Q_OBJECT

public:
    explicit HermesWorker(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~HermesWorker() override;

    /// Chiamata dal thread proprietario PRIMA di `moveToThread()`.
    void configure(const QHostAddress &address, double sampleRate, qint64 centerHz);

public slots:
    void start();
    void stop();
    void setCenterFrequency(qint64 hz);
    void setSampleRate(double rate);
    void setGainDb(double db);
    void setPtt(bool transmit);

signals:
    /// Un pacchetto è stato tradotto e messo nel ring. `dropped` conta le
    /// coppie perse: per overrun del ring, o per buchi nella numerazione — e
    /// la differenza la dice `lost`.
    void framesProduced(quint32 frames, quint32 dropped, bool adcOverload);

    /// Pacchetti mai arrivati, dedotti dai salti nella numerazione. La radio
    /// non ritrasmette: un buco è audio perduto, e saperlo distingue una rete
    /// che perde da un DSP troppo lento.
    void packetsLost(quint64 total);

    void failed(const QString &reason);

private slots:
    void readPending();

private:
    void sendCommand();
    Command nextCommand();

    dsp::SpscRing<float> *m_ring = nullptr;
    std::unique_ptr<QUdpSocket> m_socket;
    QHostAddress m_address;

    std::vector<float> m_decoded;

    double m_sampleRate = 48000.0;
    qint64 m_centerHz = 7'100'000;
    double m_gainDb = 0.0;
    std::atomic<bool> m_ptt{false};

    quint32 m_txSequence = 0;
    quint32 m_expectedRx = 0;
    quint64 m_lost = 0;
    int m_commandTurn = 0;
    bool m_running = false;
    bool m_haveSequence = false;
};

} // namespace dsdr::hal::hermes
