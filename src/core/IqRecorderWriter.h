// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — scrittore su disco della registrazione IQ.
//
// Vive nel thread del registratore: drena il ring e scrive. È l'unico punto
// del progetto in cui il flusso di campioni tocca un file.
#pragma once

#include "core/IqRecorder.h"

#include <QFile>
#include <QObject>

#include <atomic>
#include <vector>

class QTimer;

namespace dsdr::core {

class IqRecorderWriter : public QObject
{
    Q_OBJECT

public:
    IqRecorderWriter(dsp::SpscRing<float> *ring, QObject *parent = nullptr);
    ~IqRecorderWriter() override;

    qint64 bytesWritten() const { return m_dataBytes.load(std::memory_order_relaxed); }
    qint64 durationMs() const;

public slots:
    void open(const QString &path, const dsdr::core::IqRecordingInfo &info);
    void close();

signals:
    void failed(const QString &message);

private slots:
    void drain();

private:
    bool writeHeader();
    bool finalizeHeader();
    bool writeSidecar();

    dsp::SpscRing<float> *m_ring = nullptr;
    QTimer *m_timer = nullptr;
    QFile m_file;
    IqRecordingInfo m_info;
    std::vector<float> m_buffer;

    std::atomic<qint64> m_dataBytes{0};
    qint64 m_junkChunkOffset = 0;   ///< posizione del JUNK che può diventare ds64
    qint64 m_dataSizeOffset = 0;    ///< posizione del campo size del chunk data
    bool m_open = false;
};

} // namespace dsdr::core
