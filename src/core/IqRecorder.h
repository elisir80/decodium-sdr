// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — registrazione del flusso IQ (RF-17).
//
// Formato WAV con campioni float32 su due canali (I e Q), leggibile da SDR#,
// SDRuno, GNU Radio e dagli altri strumenti che trattano le registrazioni IQ
// come file audio stereo.
//
// Il WAV classico non supera i 4 GB perché le dimensioni sono a 32 bit — a
// 2,048 MS/s sono meno di nove minuti. Per non troncare le sessioni lunghe si
// riserva in testa un chunk JUNK di 28 byte che, se il file supera la soglia,
// alla chiusura diventa il chunk `ds64` di RF64: è la stessa strategia usata
// dai registratori professionali, e lascia il file compatibile con i lettori
// WAV quando la conversione non serve.
//
// Threading: il thread DSP chiama solo `feed()`, che scrive in un ring
// lock-free. Un thread interno drena il ring e tocca il disco. Il percorso
// caldo non fa mai I/O (CONSTITUTION §5).
#pragma once

#include "dsp/SpscRing.h"

#include <QDateTime>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QThread;
class QTimer;

namespace dsdr::core {

/// Metadati che accompagnano la registrazione nel sidecar.
struct IqRecordingInfo
{
    qint64 centerFrequencyHz = 0;
    double sampleRate = 0.0;
    QString backendId;
    QString deviceName;
    QString antenna;
    QString operatorCall;
    QDateTime startedAt;
};

class IqRecorderWriter;

class IqRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY recordingChanged)
    Q_PROPERTY(qint64 bytesWritten READ bytesWritten NOTIFY progressChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY progressChanged)
    Q_PROPERTY(int droppedBlocks READ droppedBlocks NOTIFY progressChanged)

public:
    explicit IqRecorder(QObject *parent = nullptr);
    ~IqRecorder() override;

    /// Avvia una registrazione. `path` vuoto genera un nome dalla data e
    /// dalla frequenza, nella cartella predefinita.
    bool start(const IqRecordingInfo &info, const QString &path = QString());
    void stop();

    bool isRecording() const { return m_recording.load(std::memory_order_acquire); }
    QString currentFile() const { return m_currentFile; }
    qint64 bytesWritten() const;
    qint64 durationMs() const;
    int droppedBlocks() const;

    /// Chiamata dal thread DSP: accoda campioni IQ interleaved (I,Q,I,Q…).
    /// Non blocca e non alloca; se il disco non tiene il passo scarta e conta.
    void feed(const float *interleaved, std::size_t floatCount) noexcept;

    /// Cartella predefinita delle registrazioni.
    static QString defaultDirectory();

    /// Nome file suggerito: data, ora e frequenza, come fanno i registratori
    /// SDR — ordinabile alfabeticamente e leggibile a colpo d'occhio.
    static QString suggestedFileName(const IqRecordingInfo &info);

signals:
    void recordingChanged();
    void progressChanged();
    void failed(const QString &message);

private:
    std::unique_ptr<dsp::SpscRing<float>> m_ring;
    IqRecorderWriter *m_writer = nullptr;
    QThread *m_thread = nullptr;
    QTimer *m_progressTimer = nullptr;

    QString m_currentFile;
    std::atomic<bool> m_recording{false};
    std::atomic<int> m_dropped{0};
};

} // namespace dsdr::core

// I metadati attraversano una connessione queued verso il thread scrittore.
Q_DECLARE_METATYPE(dsdr::core::IqRecordingInfo)
