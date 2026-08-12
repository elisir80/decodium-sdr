// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TxProfiles.h"

#include <QSettings>

namespace dsdr::core {

namespace {

/// Le cinque campane di partenza, uguali per tutti i profili: le frequenze
/// sono quelle che contano in una voce, e il guadagno lo mette il profilo.
///
///  120 Hz  il rimbombo che non serve a nessuno e ruba potenza
///  300 Hz  il corpo: sotto i 300 in SSB non passa comunque niente
///  900 Hz  il fondo della vocale, dove una voce diventa piena o cupa
/// 2000 Hz  la consonante: è qui che si distingue «bravo» da «tango»
/// 3200 Hz  il bordo della passata, dove si guadagna presenza o sibilo
constexpr double kBandHz[dsp::ParametricEq::kBands] = {120.0, 300.0, 900.0, 2000.0, 3200.0};

QString groupFor(TxProfiles::Profile profile)
{
    switch (profile) {
    case TxProfiles::Ragchew: return QStringLiteral("tx/profili/chiacchierata");
    case TxProfiles::Contest: return QStringLiteral("tx/profili/dx");
    case TxProfiles::Data:    return QStringLiteral("tx/profili/dati");
    case TxProfiles::Count:   break;
    }
    return QStringLiteral("tx/profili/altro");
}

} // namespace

TxProfiles::TxProfiles(QObject *parent)
    : QObject(parent)
{
    for (int i = 0; i < Count; ++i)
        m_states[static_cast<std::size_t>(i)] = factory(static_cast<Profile>(i));
}

TxProfileState TxProfiles::factory(Profile profile)
{
    TxProfileState s;
    for (int i = 0; i < dsp::ParametricEq::kBands; ++i) {
        s.eq[static_cast<std::size_t>(i)].frequencyHz = kBandHz[i];
        s.eq[static_cast<std::size_t>(i)].gainDb = 0.0;
        s.eq[static_cast<std::size_t>(i)].q = 1.0;
    }

    switch (profile) {
    case Ragchew:
        // Una chiacchierata si ascolta per mezz'ora, e mezz'ora di voce
        // compressa stanca. Il gate toglie la stanza, il leveller corregge la
        // distanza dal microfono, e la compressione resta quel poco che serve
        // a non far sparire le frasi dette piano.
        s.gateEnabled = true;
        s.gateThresholdDb = -45.0;
        s.levellerEnabled = true;
        s.levellerTargetDb = -18.0;
        s.compressionDb = 4.0;
        s.eqEnabled = true;
        s.eq[0].gainDb = -4.0;    // via il rimbombo: non porta niente in aria
        s.eq[3].gainDb = 2.0;     // un filo di consonante
        s.cfcEnabled = false;
        s.limiterEnabled = true;
        s.limiterCeilingDb = -1.0;
        s.drive = 0.25;
        break;

    case Contest:
        // In un pile-up conta una cosa sola: essere capiti al primo colpo, e
        // quello che fa capire non è il volume ma la banda 1–3 kHz, dove
        // stanno le consonanti. Sotto i 300 Hz si toglie senza pietà: quel che
        // c'è là sotto occupa potenza e non porta nessuna informazione.
        s.gateEnabled = true;
        s.gateThresholdDb = -42.0;
        s.levellerEnabled = true;
        s.levellerTargetDb = -14.0;
        s.compressionDb = 9.0;
        s.eqEnabled = true;
        s.eq[0].gainDb = -10.0;
        s.eq[1].gainDb = -4.0;
        s.eq[2].gainDb = 1.0;
        s.eq[3].gainDb = 5.0;
        s.eq[4].gainDb = 3.0;
        s.cfcEnabled = true;
        s.cfcPunch = 6.0;
        s.limiterEnabled = true;
        s.limiterCeilingDb = -0.5;
        s.drive = 0.3;
        break;

    case Data:
    case Count:
        // Sui dati la catena si spegne tutta, e non è una precauzione: un
        // compressore davanti a un modulatore FT8 allarga il segnale e non
        // aggiunge un decibel di rapporto segnale-rumore a chi decodifica.
        // È l'errore più diffuso che ci sia, e si fa una volta sola e resta.
        //
        // Anche il livello parte più basso: sui dati si trasmette a ciclo
        // pieno, e un finale tenuto al massimo per quindici secondi scalda
        // come non fa mai in fonia.
        s.gateEnabled = false;
        s.levellerEnabled = false;
        s.compressionDb = 0.0;
        s.eqEnabled = false;
        s.cfcEnabled = false;
        s.limiterEnabled = false;
        s.drive = 0.15;
        break;
    }
    return s;
}

QString TxProfiles::name(Profile profile)
{
    switch (profile) {
    case Ragchew: return tr("SSB chiacchierata");
    case Contest: return tr("SSB DX e contest");
    case Data:    return tr("Dati e CW — catena spenta");
    case Count:   break;
    }
    return QString();
}

TxProfiles::Profile TxProfiles::profileForMode(DemodMode mode) const
{
    switch (mode) {
    case DemodMode::Usb:
    case DemodMode::Lsb:
    case DemodMode::Am:
    case DemodMode::Sam:
    case DemodMode::Dsb:
    case DemodMode::Fm:
    case DemodMode::Nfm:
        return m_ssbProfile;

    case DemodMode::DigU:
    case DemodMode::DigL:
    case DemodMode::Iq:
    case DemodMode::Cw:
    case DemodMode::Cwr:
        // Qui non c'è una scelta da offrire. Sui dati la catena va spenta, e
        // in CW non la attraversa niente: dare un'opzione vorrebbe dire
        // suggerire che ci sia un caso in cui accenderla convenga.
        return Data;
    }
    return m_ssbProfile;
}

void TxProfiles::setSsbProfile(Profile profile)
{
    if (profile == Data || m_ssbProfile == profile)
        return;
    m_ssbProfile = profile;
    emit changed();
}

const TxProfileState &TxProfiles::state(Profile profile) const
{
    const auto index = static_cast<std::size_t>(
        profile >= 0 && profile < Count ? profile : Ragchew);
    return m_states[index];
}

void TxProfiles::setState(Profile profile, const TxProfileState &state)
{
    if (profile < 0 || profile >= Count)
        return;
    m_states[static_cast<std::size_t>(profile)] = state;
}

void TxProfiles::restoreDefaults(Profile profile)
{
    if (profile < 0 || profile >= Count)
        return;
    m_states[static_cast<std::size_t>(profile)] = factory(profile);
    emit changed();
}

void TxProfiles::load()
{
    QSettings settings;
    m_ssbProfile = static_cast<Profile>(
        settings.value(QStringLiteral("tx/profili/ssb"), Ragchew).toInt());
    if (m_ssbProfile != Ragchew && m_ssbProfile != Contest)
        m_ssbProfile = Ragchew;

    for (int i = 0; i < Count; ++i) {
        const auto profile = static_cast<Profile>(i);
        TxProfileState s = factory(profile);
        settings.beginGroup(groupFor(profile));

        // Ogni valore ha come predefinito quello di fabbrica: un profilo
        // salvato da una versione che non conosceva uno stadio non deve
        // riportarlo a zero, deve lasciarlo com'è.
        s.gateEnabled = settings.value(QStringLiteral("gate"), s.gateEnabled).toBool();
        s.gateThresholdDb = settings.value(QStringLiteral("gateSoglia"),
                                           s.gateThresholdDb).toDouble();
        s.levellerEnabled = settings.value(QStringLiteral("leveller"),
                                           s.levellerEnabled).toBool();
        s.levellerTargetDb = settings.value(QStringLiteral("levellerBersaglio"),
                                            s.levellerTargetDb).toDouble();
        s.micGainDb = settings.value(QStringLiteral("microfono"), s.micGainDb).toDouble();
        s.compressionDb = settings.value(QStringLiteral("compressione"),
                                         s.compressionDb).toDouble();
        s.eqEnabled = settings.value(QStringLiteral("eq"), s.eqEnabled).toBool();
        for (int b = 0; b < dsp::ParametricEq::kBands; ++b) {
            auto &band = s.eq[static_cast<std::size_t>(b)];
            band.frequencyHz = settings.value(QStringLiteral("eq%1Hz").arg(b),
                                              band.frequencyHz).toDouble();
            band.gainDb = settings.value(QStringLiteral("eq%1Db").arg(b),
                                         band.gainDb).toDouble();
            band.q = settings.value(QStringLiteral("eq%1Q").arg(b), band.q).toDouble();
        }
        s.cfcEnabled = settings.value(QStringLiteral("cfc"), s.cfcEnabled).toBool();
        s.cfcPunch = settings.value(QStringLiteral("cfcPunch"), s.cfcPunch).toDouble();
        s.limiterEnabled = settings.value(QStringLiteral("limiter"),
                                          s.limiterEnabled).toBool();
        s.limiterCeilingDb = settings.value(QStringLiteral("limiterTetto"),
                                            s.limiterCeilingDb).toDouble();
        s.drive = settings.value(QStringLiteral("drive"), s.drive).toDouble();

        settings.endGroup();
        m_states[static_cast<std::size_t>(i)] = s;
    }
}

void TxProfiles::save() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("tx/profili/ssb"), static_cast<int>(m_ssbProfile));

    for (int i = 0; i < Count; ++i) {
        const TxProfileState &s = m_states[static_cast<std::size_t>(i)];
        settings.beginGroup(groupFor(static_cast<Profile>(i)));
        settings.setValue(QStringLiteral("gate"), s.gateEnabled);
        settings.setValue(QStringLiteral("gateSoglia"), s.gateThresholdDb);
        settings.setValue(QStringLiteral("leveller"), s.levellerEnabled);
        settings.setValue(QStringLiteral("levellerBersaglio"), s.levellerTargetDb);
        settings.setValue(QStringLiteral("microfono"), s.micGainDb);
        settings.setValue(QStringLiteral("compressione"), s.compressionDb);
        settings.setValue(QStringLiteral("eq"), s.eqEnabled);
        for (int b = 0; b < dsp::ParametricEq::kBands; ++b) {
            const auto &band = s.eq[static_cast<std::size_t>(b)];
            settings.setValue(QStringLiteral("eq%1Hz").arg(b), band.frequencyHz);
            settings.setValue(QStringLiteral("eq%1Db").arg(b), band.gainDb);
            settings.setValue(QStringLiteral("eq%1Q").arg(b), band.q);
        }
        settings.setValue(QStringLiteral("cfc"), s.cfcEnabled);
        settings.setValue(QStringLiteral("cfcPunch"), s.cfcPunch);
        settings.setValue(QStringLiteral("limiter"), s.limiterEnabled);
        settings.setValue(QStringLiteral("limiterTetto"), s.limiterCeilingDb);
        settings.setValue(QStringLiteral("drive"), s.drive);
        settings.endGroup();
    }
}

} // namespace dsdr::core
