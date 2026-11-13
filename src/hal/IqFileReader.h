// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — lettura di una registrazione IQ su file.
//
// Legge il formato che scrive `IqRecorder` — WAV float32 a due canali, con
// promozione a RF64 oltre i 4 GB — e anche i due formati in cui arrivano di
// solito le registrazioni fatte con altri programmi: PCM a 16 bit e a 8 bit
// senza segno. Sono venti righe di conversione che rendono il backend utile
// con i file di qualcun altro, non solo con i nostri.
//
// La frequenza centrale non sta nel WAV: senza il sidecar JSON che scriviamo
// accanto, una registrazione IQ è un file di numeri senza contesto. Quando il
// sidecar manca si tenta il nome del file, che nei nostri contiene i MHz.
#pragma once

#include <QFile>
#include <QString>

#include <cstdint>
#include <vector>

namespace dsdr::hal::iqfile {

/// Come sono codificati i campioni nel file.
enum class SampleFormat {
    Float32,   ///< il nostro formato
    Int16,     ///< PCM con segno, il più diffuso fra i registratori SDR
    Uint8,     ///< PCM senza segno, tipico dei dump grezzi da RTL-SDR
};

/// Ciò che si sa di una registrazione senza averne letto i campioni.
struct RecordingInfo
{
    QString filePath;
    QString displayName;
    qint64 centerFrequencyHz = 0;
    double sampleRate = 0.0;
    SampleFormat format = SampleFormat::Float32;
    qint64 frameCount = 0;        ///< coppie I/Q totali
    qint64 durationMs = 0;
    QString backendId;            ///< con quale radio fu registrata, se noto
    QString deviceName;
    bool hasSidecar = false;      ///< false = frequenza centrale incerta

    bool isValid() const noexcept { return sampleRate > 0.0 && frameCount > 0; }
};

class IqFileReader
{
public:
    IqFileReader() = default;
    ~IqFileReader();

    IqFileReader(const IqFileReader &) = delete;
    IqFileReader &operator=(const IqFileReader &) = delete;

    /// Legge solo intestazione e sidecar, senza tenere il file aperto. Serve
    /// alla discovery, che deve descrivere decine di registrazioni in fretta.
    static bool probe(const QString &path, RecordingInfo &info, QString *error = nullptr);

    bool open(const QString &path, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_file.isOpen(); }

    const RecordingInfo &info() const { return m_info; }

    /// Legge fino a `frames` coppie I/Q convertite in float interleaved.
    /// Restituisce quante ne ha davvero lette: meno del richiesto significa
    /// fine del file.
    std::size_t read(float *interleaved, std::size_t frames);

    /// Posizione corrente, in coppie I/Q dall'inizio.
    qint64 position() const { return m_position; }
    bool seek(qint64 frame);
    bool atEnd() const { return m_position >= m_info.frameCount; }

private:
    static bool parseHeader(QFile &file, RecordingInfo &info, qint64 &dataOffset,
                            QString *error);
    static void applySidecar(const QString &path, RecordingInfo &info);
    static qint64 frequencyFromFileName(const QString &path);

    QFile m_file;
    RecordingInfo m_info;
    qint64 m_dataOffset = 0;
    qint64 m_position = 0;
    std::vector<char> m_raw;   ///< buffer di conversione, allocato all'apertura
};

} // namespace dsdr::hal::iqfile
