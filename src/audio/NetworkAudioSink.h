// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — copia dell'audio ricevuto verso la rete.
//
// Il formato è volutamente piccolo e dichiarato: PCM signed 16 bit,
// little-endian, 48 kHz, senza intestazione. È il formato predefinito del
// Network Sink di SDR++, quindi chi ascolta dall'altra parte non deve usare
// un adattatore proprietario.
#pragma once

#include "dsp/SpscRing.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <memory>

class QThread;

namespace dsdr::audio {

class NetworkAudioSink final : public QObject
{
    Q_OBJECT

public:
    enum class Protocol {
        Udp,
        TcpServer,
    };

    struct Config {
        Protocol protocol = Protocol::Udp;
        QString host = QStringLiteral("127.0.0.1");
        quint16 port = 7355;
        bool stereo = false;
    };

    explicit NetworkAudioSink(QObject *parent = nullptr);
    ~NetworkAudioSink() override;

    /// Avvia UDP verso host:porta oppure un listener TCP. Il metodo non apre
    /// socket nel thread GUI: il risultato definitivo arriva in stateChanged.
    bool start(const Config &config);
    void stop();

    bool isActive() const noexcept { return m_active.load(std::memory_order_acquire); }
    QVariantMap status() const;
    QString errorString() const { return m_error; }

    /// Tap chiamato dal thread DSP. `stereo` è interlacciato L/R, ±1; non
    /// blocca mai il demodulatore se il ricevitore di rete resta indietro.
    void feed(const float *stereo, std::size_t frames) noexcept;

signals:
    void stateChanged();
    void failed(const QString &message);

private:
    class Worker;

    static QString protocolName(Protocol protocol);

    std::unique_ptr<dsp::SpscRing<float>> m_ring;
    QThread *m_thread = nullptr;
    Worker *m_worker = nullptr;
    Config m_config;
    QString m_detail;
    QString m_error;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_accepting{false};
    std::atomic<quint64> m_framesSent{0};
    std::atomic<quint64> m_framesDropped{0};
    quint64 m_generation = 0;
};

} // namespace dsdr::audio
