// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — chi può ascoltare che cosa (DSDR-SPEC-003 §8.3, IMPL-001 §5).
//
// C'è una regola che non si può lasciare alla buona volontà: **l'audio uscito
// dalla riduzione neurale non deve mai arrivare a un decodificatore.**
//
// Il motivo è che una rete addestrata sulla voce fa esattamente il suo mestiere
// anche quando il segnale non è voce: toglie quello che non le somiglia. Su un
// FT8 al limite del rumore toglie il segnale. Il decodificatore non se ne
// accorge — decodifica meno, e nessuno ha modo di sapere perché. Non c'è
// errore, non c'è avviso: c'è una stazione che non si aggancia più.
//
// Perciò l'audio porta un'etichetta. `EarOnly` è quello passato dalla rete: va
// all'orecchio e basta. `Clean` è quello lineare, e può andare ovunque.
//
// Il rifiuto avviene **quando si costruisce il grafo**, non quando passano i
// campioni. Un controllo a runtime scatterebbe la millesima volta, in mezzo a
// un contest, e nessuno saprebbe leggerlo; un rifiuto alla costruzione lo vede
// chi scrive il collegamento, subito, con il motivo scritto.
#pragma once

#include <QString>
#include <QStringList>

namespace dsdr::dsp {
template <typename T>
class SpscRing;
}

namespace dsdr::audio {

/// Che cosa è passato su questo audio.
enum class AudioTag {
    /// Lineare: nessuno stadio ha deciso che cosa fosse segnale.
    Clean,
    /// Passato dalla riduzione neurale. Solo per l'orecchio.
    EarOnly,
};

/// Dove l'audio può andare.
enum class AudioSink {
    Ear,              ///< gli altoparlanti dell'operatore
    AudioRecorder,    ///< registrazione audio su file
    NetworkStream,    ///< PCM lineare verso un ricevitore di rete
    DigitalDecoder,   ///< DECODIUM 4 e chiunque decodifichi
    Transmit,         ///< il percorso di trasmissione
};

struct AudioNode
{
    QString name;
    AudioTag tag = AudioTag::Clean;
    dsp::SpscRing<float> *ring = nullptr;
};

/// La regola, pura e statica: si può verificare senza costruire niente.
bool mayRoute(AudioTag tag, AudioSink sink);

/// Il nome della destinazione, per i messaggi.
QString sinkName(AudioSink sink);

/// Il grafo. Non trasporta campioni — quelli passano dai ring — ma decide
/// quali collegamenti abbiano diritto di esistere, e li tiene scritti.
class AudioGraph
{
public:
    /// Prova a collegare. Restituisce false e spiega perché, senza aggiungere
    /// nulla: un grafo che accettasse un collegamento vietato «tanto poi lo
    /// controlliamo» non servirebbe a niente.
    bool connect(const AudioNode &source, AudioSink sink, QString *why = nullptr);

    /// I collegamenti esistenti, in forma leggibile. Serve a chi deve capire
    /// da dove arriva l'audio che sta ascoltando.
    QStringList routes() const { return m_routes; }

    void clear() { m_routes.clear(); }

private:
    QStringList m_routes;
};

} // namespace dsdr::audio
