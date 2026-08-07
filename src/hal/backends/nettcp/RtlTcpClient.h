// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — client rtl_tcp su thread dedicato.
//
// Vive nel thread di ingest del backend: riceve dalla rete, converte i
// campioni a 8 bit in float e li scrive nel ring SPSC di cui è l'unico
// produttore. Non conosce né il core né la UI.
#pragma once

#include "dsp/DspTypes.h"
#include "hal/backends/nettcp/RtlTcpProtocol.h"

#include <QElapsedTimer>
#include <QObject>

#include <vector>

class QTcpSocket;

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::hal::nettcp {

class RtlTcpClient : public QObject
{
    Q_OBJECT

public:
    explicit RtlTcpClient(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~RtlTcpClient() override;

public slots:
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();

    void setFrequency(qint64 hz);
    void setSampleRate(double rate);
    /// `gainTenthsDb` negativo attiva il guadagno automatico.
    void setGain(int gainTenthsDb);
    void setFrequencyCorrection(int ppm);
    void setBiasTee(bool enabled);

signals:
    /// Handshake completato: il device ha dichiarato che cosa è.
    void connected(quint32 tunerType, quint32 gainStepCount);
    void disconnected();
    void failed(const QString &message, bool fatal);

    /// Un blocco è stato scritto nel ring. `dropped` conta le coppie I/Q
    /// perse per overrun del consumatore.
    void samplesProduced(quint32 frames, quint32 dropped, quint64 timestampNs);

private slots:
    void onReadyRead();
    void onSocketError();
    void onDisconnected();

private:
    void sendCommand(RtlTcpCommand command, qint32 value);
    void processGreeting();
    void processSamples();

    QTcpSocket *m_socket = nullptr;
    dsp::SpscRing<float> *m_ring = nullptr;
    QElapsedTimer m_clock;

    QByteArray m_pending;          ///< byte ricevuti non ancora convertiti
    std::vector<float> m_scratch;  ///< conversione uint8 → float interleaved

    bool m_greetingReceived = false;
    quint32 m_tunerType = 0;

    // Impostazioni applicate all'handshake: rtl_tcp accetta comandi solo a
    // connessione stabilita, quindi vanno ricordate e riemesse.
    qint64 m_frequencyHz = 100'000'000;
    double m_sampleRate = 2'048'000.0;
    int m_gainTenthsDb = -1;
    int m_ppm = 0;
    bool m_biasTee = false;
};

} // namespace dsdr::hal::nettcp
