// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — uscita audio verso un dispositivo scelto.
//
// È il gemello di MicSource, nell'altro verso: là un QAudioSource riempie un
// ring, qui un QAudioSink lo svuota. Non va confuso con AudioRouter, che porta
// l'audio *all'operatore* ed è stereo, ha il volume e la sordina; questo porta
// audio a una **macchina** — il codec di una radio — e non ha nulla da
// regolare, perché il livello lo decide la catena di trasmissione.
//
// Le regole restano quelle: nessun lock e nessuna allocazione nella callback,
// e un ring vuoto produce silenzio invece di ripetere l'ultimo blocco. Un
// ronzio periodico in trasmissione sarebbe peggio di un buco: il buco lo si
// sente e si cerca, il ronzio lo si scambia per il finale.
#pragma once

#include "dsp/SpscRing.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QAudioSink;

namespace dsdr::audio {

class AudioOut : public QObject
{
    Q_OBJECT

public:
    explicit AudioOut(QObject *parent = nullptr);
    ~AudioOut() override;

    /// Il ring da cui l'uscita attinge: mono, ±1, alla frequenza di
    /// `sampleRate()`. Esiste da subito e non cambia mai, così chi ci scrive
    /// lo può prendere una volta sola.
    dsp::SpscRing<float> *ring() const noexcept { return m_ring.get(); }

    bool start(const QAudioDevice &device);
    void stop();

    bool isActive() const;
    QString deviceName() const { return m_deviceName; }
    QString errorString() const { return m_error; }
    double sampleRate() const { return m_format.sampleRate(); }

    /// Le uscite audio disponibili, per chi deve sceglierne una.
    static QList<QAudioDevice> outputs();

    /// Blocchi in cui il ring era vuoto. Se cresce durante la trasmissione, il
    /// motore TX non sta stando dietro e in aria ci sono buchi.
    quint64 underrunCount() const noexcept { return m_underruns.load(std::memory_order_relaxed); }

signals:
    void activeChanged();

private:
    class RingSource;

    std::unique_ptr<QAudioSink> m_sink;
    std::unique_ptr<RingSource> m_source;
    std::unique_ptr<dsp::SpscRing<float>> m_ring;
    QAudioFormat m_format;
    QString m_deviceName;
    QString m_error;
    std::atomic<quint64> m_underruns{0};
};

} // namespace dsdr::audio
