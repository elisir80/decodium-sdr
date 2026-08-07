// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — descrittori di frame della HAL.
//
// NOTA NORMATIVA (§4.1, CONSTITUTION §5): questi struct NON contengono
// campioni. Sono metadati leggeri che viaggiano sui signal Qt per segnalare
// che nuovi dati sono disponibili; i campioni stanno nei ring buffer SPSC
// esposti da IRadioBackend::iqStream()/audioStream()/spectrumStream().
//
// Chi modifica questo file aggiungendo un contenitore di campioni sta violando
// il contratto: la conformance suite lo rileva misurando le allocazioni per
// frame nel percorso caldo.
#pragma once

#include "common/Types.h"

#include <QMetaType>
#include <QString>

namespace dsdr::hal {

/// Blocco di campioni IQ reso disponibile nel ring del canale (o del device,
/// quando `channel == kInvalidChannel`).
struct IqFrame
{
    ChannelId channel = kInvalidChannel;
    quint64 sequence = 0;          ///< monotono: un salto = campioni persi
    qint64 centerFrequencyHz = 0;
    double sampleRate = 0.0;
    quint32 frameCount = 0;        ///< coppie I/Q rese disponibili
    quint32 droppedFrames = 0;     ///< overrun dal frame precedente
    quint64 timestampNs = 0;       ///< orologio monotono del backend
};

/// Audio già demodulato dalla radio (backend server-DSP).
struct AudioFrame
{
    ChannelId channel = kInvalidChannel;
    quint64 sequence = 0;
    double sampleRate = 0.0;
    quint32 frameCount = 0;
    quint32 channelCount = 1;
    quint64 timestampNs = 0;
};

/// Riga di spettro prodotta dalla radio (backend con `spectrum == Device`).
struct SpectrumFrame
{
    PanId pan = kInvalidPan;
    quint64 sequence = 0;
    qint64 centerFrequencyHz = 0;
    double spanHz = 0.0;
    quint32 binCount = 0;
    float referenceLevelDb = 0.0f;
    quint64 timestampNs = 0;
};

/// Misure istantanee di un canale.
struct MeterFrame
{
    ChannelId channel = kInvalidChannel;
    float signalDbm = -140.0f;
    float noiseFloorDbm = -140.0f;
    float agcGainDb = 0.0f;
    float swr = 1.0f;      ///< significativo solo in TX
    float powerWatt = 0.0f;
    quint64 timestampNs = 0;
};

/// Errore riportato dal backend. Nessuna eccezione attraversa il seam (§4.1).
struct BackendError
{
    enum Code {
        None = 0,
        NotFound,          ///< device sparito o mai trovato
        PermissionDenied,  ///< udev/driver/firewall
        Unsupported,       ///< richiesta legittima ma non implementabile qui
        ProtocolError,
        TransportError,    ///< rete o USB caduti
        ResourceExhausted, ///< limite di canali/banda del device
        InternalError,
    };

    Code code = None;
    QString message;   ///< già localizzato, mostrabile all'utente
    QString detail;    ///< diagnostica tecnica per il log
    bool fatal = false;///< se true la sessione va chiusa

    bool isError() const noexcept { return code != None; }
};

/// Configurazione richiesta per un nuovo canale RX.
struct RxChannelConfig
{
    qint64 frequencyHz = 0;
    DemodMode mode = DemodMode::Usb;
    int filterLowHz = 300;
    int filterHighHz = 2700;
    QString label;
};

/// Configurazione di un panadattatore.
struct PanConfig
{
    qint64 centerFrequencyHz = 0;
    double spanHz = 0.0;      ///< 0 = tutta la banda disponibile
    int binCount = 2048;
    float framesPerSecond = 25.0f;
};

} // namespace dsdr::hal

Q_DECLARE_METATYPE(dsdr::hal::IqFrame)
Q_DECLARE_METATYPE(dsdr::hal::AudioFrame)
Q_DECLARE_METATYPE(dsdr::hal::SpectrumFrame)
Q_DECLARE_METATYPE(dsdr::hal::MeterFrame)
Q_DECLARE_METATYPE(dsdr::hal::BackendError)
