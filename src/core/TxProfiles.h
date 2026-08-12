// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — i profili della catena di trasmissione (SPEC-005 §4.4).
//
// **Il problema.** Una voce regolata per una chiacchierata sui quaranta metri
// non è la stessa che serve dentro un pile-up, e nessuna delle due è quella
// che serve in FT8 — dove la catena non serve affatto, e comprimere fa solo
// danno. Chi lo sa cambia sei manopole ogni volta che cambia modo; chi non lo
// sa trasmette dati con il compressore acceso e non lo scopre mai.
//
// **La scelta.** Il profilo cambia **da sé quando cambia il modo**, perché è
// quello che l'operatore intende quando passa da una chiacchierata a un
// pile-up: non sta chiedendo un preset, sta cambiando mestiere.
//
// **L'insidia, che è tutta qui.** Commutare un profilo automaticamente vuol
// dire sovrascrivere quello che l'operatore ha appena regolato a mano. Se
// succede in silenzio è un furto: uno passa mezz'ora a sistemare la propria
// voce in SSB, prova un FT8, torna in SSB e trova tutto com'era prima.
//
// Quindi **prima di uscire da un profilo lo si salva**. Il profilo non è un
// preset di fabbrica da cui si esce: è la memoria di come piace quel modo. Si
// regola una volta, e da lì in poi torna com'era ogni volta che si torna lì.
// Le impostazioni di fabbrica restano, e ci si torna con un comando esplicito.
#pragma once

#include "common/Types.h"
#include "dsp/ParametricEq.h"

#include <QObject>
#include <QString>

#include <array>

namespace dsdr::core {

class TxEngine;

/// Tutto quello che un profilo ricorda.
///
/// Non c'è dentro la larghezza del filtro di trasmissione: quella segue il
/// modo per conto suo, e tenerne due copie vorrebbe dire vederle divergere.
struct TxProfileState
{
    bool gateEnabled = false;
    double gateThresholdDb = -45.0;

    bool levellerEnabled = false;
    double levellerTargetDb = -18.0;

    double micGainDb = 6.0;
    double compressionDb = 6.0;

    bool eqEnabled = false;
    struct Band {
        double frequencyHz = 0.0;
        double gainDb = 0.0;
        double q = 1.0;
    };
    std::array<Band, dsp::ParametricEq::kBands> eq{};

    bool cfcEnabled = false;
    double cfcPunch = 0.0;

    bool limiterEnabled = false;
    double limiterCeilingDb = -1.0;

    double drive = 0.25;
};

class TxProfiles : public QObject
{
    Q_OBJECT

public:
    /// I tre mestieri.
    ///
    /// Tre e non dieci: un elenco lungo è un elenco che nessuno legge, e le
    /// differenze che contano davvero fra una voce e l'altra sono queste.
    enum Profile {
        Ragchew,   ///< SSB chiacchierata: naturale, poco compressa
        Contest,   ///< SSB DX e pile-up: stretta, densa, che passa
        Data,      ///< dati e CW: catena spenta, lineare
        Count
    };
    Q_ENUM(Profile)

    explicit TxProfiles(QObject *parent = nullptr);

    /// Il profilo che compete a un modo. In SSB dipende da quale delle due
    /// voci l'operatore ha scelto l'ultima volta; fuori dalla SSB non c'è
    /// scelta da fare — sui dati la catena va spenta e basta.
    Profile profileForMode(DemodMode mode) const;

    /// Quale voce si usa in SSB. È l'unica scelta che l'operatore fa: le altre
    /// le decide il modo.
    Profile ssbProfile() const noexcept { return m_ssbProfile; }
    void setSsbProfile(Profile profile);

    Profile current() const noexcept { return m_current; }
    static QString name(Profile profile);

    const TxProfileState &state(Profile profile) const;
    void setState(Profile profile, const TxProfileState &state);

    /// Riporta un profilo com'era di fabbrica.
    void restoreDefaults(Profile profile);

    /// Le impostazioni di fabbrica, per chi vuole sapere da dove si parte.
    static TxProfileState factory(Profile profile);

    // ── Persistenza ──────────────────────────────────────────────────────

    void load();
    void save() const;

signals:
    void changed();

private:
    std::array<TxProfileState, Count> m_states{};
    Profile m_ssbProfile = Ragchew;
    Profile m_current = Ragchew;

    friend class SessionManager;
};

} // namespace dsdr::core
