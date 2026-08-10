// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — ingresso audio (microfono).
//
// Speculare ad AudioRouter: là un QAudioSink tira dal ring, qui un QAudioSource
// spinge dentro un ring. Le regole non cambiano — nessun lock e nessuna
// allocazione nella callback, e un overrun scarta i campioni più vecchi invece
// di bloccare il driver, che è l'unico modo per non far scattare l'audio di
// sistema quando il motore TX ha un momento di ritardo.
//
// Il ring appartiene a questa classe e non cambia mai: chi trasmette lo prende
// una volta e continua a leggerlo, anche mentre il dispositivo d'ingresso viene
// cambiato sotto.
#pragma once

#include "dsp/SpscRing.h"

#include <QAudioFormat>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QAudioSource;

namespace dsdr::audio {

class MicSource : public QObject
{
    Q_OBJECT

public:
    explicit MicSource(QObject *parent = nullptr);
    ~MicSource() override;

    /// Il ring in cui finisce l'audio del microfono: mono, ±1, alla frequenza
    /// di `sampleRate()`. Esiste da subito.
    dsp::SpscRing<float> *ring() const noexcept { return m_ring.get(); }

    /// Apre il dispositivo d'ingresso predefinito. Restituisce false — e lo
    /// dice con `errorString()` — se non ce n'è uno utilizzabile: un computer
    /// senza microfono deve poter usare tutto il resto dell'applicazione.
    bool start();
    void stop();

    bool isActive() const;
    QString deviceName() const { return m_deviceName; }
    QString errorString() const { return m_error; }
    double sampleRate() const { return m_format.sampleRate(); }

    /// Picco dell'ultimo blocco arrivato, 0…1. Serve all'indicatore del
    /// microfono, che deve muoversi anche quando non si sta trasmettendo:
    /// è così che si regola il guadagno prima di premere il PTT.
    float lastPeak() const noexcept { return m_peak.load(std::memory_order_relaxed); }

    /// Campioni scartati per overrun dall'avvio. Se cresce, chi legge il ring
    /// non sta stando dietro.
    quint64 overrunCount() const noexcept { return m_overruns.load(std::memory_order_relaxed); }

signals:
    void activeChanged();
    void deviceChanged();

private:
    class RingSink;

    std::unique_ptr<QAudioSource> m_source;
    std::unique_ptr<RingSink> m_sink;
    std::unique_ptr<dsp::SpscRing<float>> m_ring;
    QAudioFormat m_format;
    QString m_deviceName;
    QString m_error;
    std::atomic<float> m_peak{0.0f};
    std::atomic<quint64> m_overruns{0};
};

} // namespace dsdr::audio
