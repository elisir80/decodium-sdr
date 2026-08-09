// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — tipi di dominio condivisi.
//
// Questo header è il solo punto di contatto di tipo fra HAL, DSP, core e UI.
// Non contiene logica: solo il vocabolario comune. Includerlo NON crea una
// dipendenza fra i livelli (CONSTITUTION §4).
#pragma once

#include <QObject>
#include <QString>
#include <cstdint>

namespace dsdr {
Q_NAMESPACE

/// Handle opaco di un canale RX. Il valore è assegnato dal backend; il core non
/// deve dedurne alcuna semantica (§4.1).
using ChannelId = quint32;

/// Handle opaco di un panadattatore.
using PanId = quint32;

inline constexpr ChannelId kInvalidChannel = 0;
inline constexpr PanId kInvalidPan = 0;

/// Modalità di demodulazione. `Iq` consegna il canale non demodulato
/// (usato dal ponte verso DECODIUM 4, §7).
enum class DemodMode {
    Usb,
    Lsb,
    Cw,     ///< CW con BFO sul lato superiore
    Cwr,    ///< CW reverse (BFO sul lato inferiore)
    Am,
    Sam,    ///< AM sincrona (PLL sulla portante)
    Fm,     ///< FM larga
    Nfm,    ///< FM stretta
    DigU,
    DigL,
    Iq,
    Dsb,    ///< Double-sideband suppressed-carrier AM
};
Q_ENUM_NS(DemodMode)

/// Capacità di trasmissione dichiarata dal backend (§4.2).
enum class TxSupport {
    None,       ///< Receive-only: la UI non mostra il PTT, non lo disabilita
    Ptt,        ///< Half-duplex
    FullDuplex,
};
Q_ENUM_NS(TxSupport)

/// Dove viene eseguito uno stadio di elaborazione (§3.1).
enum class DspLocation {
    Client,     ///< Lo stadio gira nel DSP Engine di DECODIUM SDR
    Device,     ///< Lo stadio gira nella radio; il client è pass-through
};
Q_ENUM_NS(DspLocation)

/// Stato del ciclo di vita di un backend.
enum class BackendState {
    Idle,
    Discovering,
    Connecting,
    Ready,
    Streaming,
    Error,
};
Q_ENUM_NS(BackendState)

/// Modalità AGC (§5.1). L'implementazione è originale (§11).
enum class AgcMode {
    Off,
    Fast,
    Medium,
    Slow,
    Long,
};
Q_ENUM_NS(AgcMode)

/// Tabella PTY per il multiplex RDS: Europa (IEC 62106) o Nord America
/// (RBDS). I codici del gruppo restano uguali, cambia solo la legenda.
enum class RdsRegion {
    Europe,
    NorthAmerica,
};
Q_ENUM_NS(RdsRegion)

/// Frequenza di lavoro interna dell'AudioRouter.
inline constexpr int kInternalAudioRate = 48000;

/// Frequenza del canale IQ esportato verso DECODIUM 4 (§5.1, §7).
inline constexpr int kDecodiumIqRate = 12000;

QString demodModeName(DemodMode mode);

} // namespace dsdr
