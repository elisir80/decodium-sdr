// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — uscita audio.
//
// Il sink Qt tira i campioni dal ring del DSP Engine attraverso un QIODevice
// leggerissimo: nessun lock, nessuna allocazione nella callback, e un underrun
// produce silenzio invece di far crollare il flusso (RNF-03).
#pragma once

#include "common/Types.h"
#include "dsp/ParametricEq.h"
#include "dsp/SpscRing.h"

#include <QAudioFormat>
#include <QObject>
#include <QString>

#include <memory>

class QAudioSink;
class QIODevice;
class QTimer;

namespace dsdr::audio {

class AudioRouter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(int latencyMs READ latencyMs NOTIFY deviceChanged)

public:
    explicit AudioRouter(QObject *parent = nullptr);
    ~AudioRouter() override;

    /// Avvia la riproduzione leggendo da `source` (consumatore unico).
    bool start(dsp::SpscRing<float> *source);
    void stop();

    bool isActive() const;
    QString deviceName() const { return m_deviceName; }
    int latencyMs() const { return m_latencyMs; }

    float volume() const { return m_volume; }
    void setVolume(float volume);

    bool isMuted() const { return m_muted; }
    void setMuted(bool muted);

    /// L'equalizzatore dell'ascolto.
    ///
    /// Sta qui e non nel motore DSP per una ragione sola, che è anche
    /// l'interlock della SPEC-005: questo è il punto in cui l'audio smette di
    /// essere un segnale da misurare e diventa un suono da sentire. Quello che
    /// alimenta i decoder si è già staccato prima e resta lineare.
    dsdr::dsp::ParametricEq &equalizer() noexcept { return m_equalizer; }

    /// La sorgente che **prende il posto** della ricezione, finché ha
    /// campioni.
    ///
    /// Serve al riascolto della propria voce e, più avanti, al monitor: sono
    /// le due cose che si vogliono sentire *invece* della banda, non sopra. Un
    /// riascolto mescolato al fruscio dei 40 metri non si giudica.
    ///
    /// La ricezione continua a scorrere sotto: si legge e si butta, tanti
    /// campioni quanti se ne consumano. Lasciarla ferma vorrebbe dire trovarsi
    /// alla fine del riascolto con un ring pieno di passato da smaltire.
    void setInterruptSource(dsp::SpscRing<float> *ring);

    /// Numero di underrun dall'avvio: se cresce, il DSP non sta stando dietro.
    quint64 underrunCount() const;

signals:
    void volumeChanged();
    void mutedChanged();
    void activeChanged();
    void deviceChanged();

private:
    class RingSource;

    void applyVolume();
    void startSinkWhenPrimed();

    std::unique_ptr<QAudioSink> m_sink;
    std::unique_ptr<RingSource> m_source;
    QTimer *m_diagnosticTimer = nullptr;
    QString m_deviceName;
    QAudioFormat m_format;
    float m_volume = 0.8f;
    int m_latencyMs = 0;
    dsdr::dsp::ParametricEq m_equalizer;
    dsp::SpscRing<float> *m_interruptSource = nullptr;
    QTimer *m_primeTimer = nullptr;
    int m_primeAttempts = 0;
    bool m_muted = false;
};

} // namespace dsdr::audio
