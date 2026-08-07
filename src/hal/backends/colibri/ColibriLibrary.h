// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — accesso a colibrinano_lib.
//
// Il ColibriNANO non si pilota via USB grezzo: il trasporto FTDI è di
// `colibrinano_lib`, la libreria di riferimento del costruttore. È l'autorità
// per ogni firma, per la tabella dei sample rate e per i limiti del
// preamplificatore, e questo backend non scende mai sotto la sua API.
//
// La libreria si carica a **runtime** (`QLibrary`), non si linka: nel
// repository non entra alcun header di terze parti, e se la DLL manca il
// backend si limita a non trovare device invece di impedire l'avvio.
//
// Provenienza: l'API è quella pubblicata in
// <https://github.com/maksimus1210/ColibriNANO_lib> (Unlicense, pubblico
// dominio). Il porting su questo seam parte dal backend ColibriNANO scritto da
// IU8LMC per AetherSDR: il codice è suo, l'architettura qui è diversa.
#pragma once

#include <QLibrary>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>

namespace dsdr::hal::colibri {

/// Campione complesso consegnato dalla libreria: già float normalizzato, che
/// è esattamente il formato del nostro ring.
struct ColibriComplex
{
    float re;
    float im;
};

using ColibriDescriptor = void *;

#ifdef Q_OS_WIN
#define DSDR_COLIBRI_CALL __stdcall
#else
#define DSDR_COLIBRI_CALL
#endif

/// Callback di ricezione. Restituire false dice alla libreria di smettere.
/// `adcOverload` viaggia su ogni blocco invece di essere una chiamata di stato
/// a parte: è l'unica telemetria che il device offre.
using ColibriRxCallback = bool(DSDR_COLIBRI_CALL *)(ColibriComplex *iq,
                                                    std::uint32_t length,
                                                    bool adcOverload,
                                                    void *user);

/// Le nove frequenze offerte dal ricevitore, nell'ordine degli indici che
/// `start()` accetta. Una lista sola: è insieme la capability dichiarata e
/// l'insieme a cui una richiesta si allinea.
inline constexpr int kSampleRatesHz[] = {
    48000, 96000, 192000, 384000, 768000, 1536000, 1920000, 2560000, 3072000,
};

/// Indice da passare a `start()` per una frequenza della lista, o -1.
int sampleRateIndex(int hz) noexcept;

/// Limiti del preamplificatore/attenuatore, in dB.
inline constexpr float kMinPreampDb = -31.5f;
inline constexpr float kMaxPreampDb = 6.0f;

/// Handle unico sulla libreria.
///
/// È un singleton perché la libreria è stato globale di processo:
/// `initialize()` deve girare una volta sola, e la discovery e il device
/// devono condividere la stessa istanza invece di fare a gara con due
/// caricamenti.
///
/// Le chiamate di controllo sono serializzate da un mutex. La callback di
/// ricezione **non passa di qui**: la libreria la invoca direttamente sul
/// proprio thread, quindi il lock non finisce mai sul percorso dei campioni.
class ColibriLibrary
{
public:
    static ColibriLibrary &instance();

    /// Carica e inizializza, idempotente. Cerca prima il percorso indicato,
    /// poi accanto all'eseguibile, infine per nome nudo lasciando cercare al
    /// sistema.
    bool ensureLoaded(const QString &explicitPath, QString *error);
    bool isLoaded() const { return m_initialized; }
    QString libraryPath() const { return m_path; }

    void version(std::uint32_t &major, std::uint32_t &minor, std::uint32_t &patch);
    QString information();

    std::uint32_t deviceCount();
    bool open(ColibriDescriptor *out, std::uint32_t index);
    void close(ColibriDescriptor device);
    bool start(ColibriDescriptor device, int rateIndex, ColibriRxCallback callback, void *user);
    bool stop(ColibriDescriptor device);
    bool setPreamp(ColibriDescriptor device, float db);
    bool setFrequency(ColibriDescriptor device, std::uint32_t hz);

    /// Vero mentre un device è aperto. La discovery lo controlla e salta
    /// l'enumerazione: sondare il bus FTDI sotto uno stream attivo non è
    /// documentato come sicuro, e non lo si scopre sul ricevitore di qualcuno.
    void setDeviceInUse(bool inUse) { m_deviceInUse.store(inUse, std::memory_order_release); }
    bool deviceInUse() const { return m_deviceInUse.load(std::memory_order_acquire); }

private:
    ColibriLibrary() = default;

    using FnVoid = void(DSDR_COLIBRI_CALL *)();
    using FnVersion = void(DSDR_COLIBRI_CALL *)(std::uint32_t &, std::uint32_t &, std::uint32_t &);
    using FnInformation = void(DSDR_COLIBRI_CALL *)(char **);
    using FnDevices = void(DSDR_COLIBRI_CALL *)(std::uint32_t &);
    using FnOpen = bool(DSDR_COLIBRI_CALL *)(ColibriDescriptor *, std::uint32_t);
    using FnClose = void(DSDR_COLIBRI_CALL *)(ColibriDescriptor);
    using FnStart = bool(DSDR_COLIBRI_CALL *)(ColibriDescriptor, int, ColibriRxCallback, void *);
    using FnStop = bool(DSDR_COLIBRI_CALL *)(ColibriDescriptor);
    using FnSetPreamp = bool(DSDR_COLIBRI_CALL *)(ColibriDescriptor, float);
    using FnSetFrequency = bool(DSDR_COLIBRI_CALL *)(ColibriDescriptor, std::uint32_t);

    static QStringList candidatePaths(const QString &explicitPath);

    QLibrary m_library;
    QString m_path;
    QMutex m_mutex;
    bool m_initialized = false;
    std::atomic<bool> m_deviceInUse{false};

    FnVoid m_initialize = nullptr;
    FnVoid m_finalize = nullptr;   ///< risolta ma mai chiamata: vedi il .cpp
    FnVersion m_version = nullptr;
    FnInformation m_information = nullptr;
    FnDevices m_devices = nullptr;
    FnOpen m_open = nullptr;
    FnClose m_close = nullptr;
    FnStart m_start = nullptr;
    FnStop m_stop = nullptr;
    FnSetPreamp m_setPreamp = nullptr;
    FnSetFrequency m_setFrequency = nullptr;
};

} // namespace dsdr::hal::colibri
