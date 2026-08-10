// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/WaterfallPalette.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dsdr::app {

namespace {

struct ColorStop
{
    float position;
    float r, g, b;
};

/// Il colore del livello «niente»: lo stesso fondo dello spettro nel tema.
constexpr ColorStop kFloorStop{0.00f, 0.02f, 0.03f, 0.06f};

/// Dove finisce la sfumatura dal fondo al primo colore della palette. Breve:
/// deve bastare a evitare uno scalino, non a mangiarsi la scala.
constexpr float kFloorFadeEnd = 0.05f;

/// Sopra questa somma di componenti un primo stop è considerato "acceso", e
/// gli si antepone il fondo. Il nero puro e il quasi-nero di DECODIUM restano
/// sotto; il viola di Turbo sta sopra.
constexpr float kFloorBrightness = 0.25f;

/// Dove cade il passaggio dal freddo al caldo — il salto verde → giallo.
///
/// È il punto in cui una palette smette di dire «fondo» e comincia a dire
/// «segnale», e l'occhio lo trova prima di qualunque altra cosa nell'immagine:
/// è il salto di tinta più grande di tutta la rampa. Metterlo a metà scala,
/// come fa quasi ogni tabella di colore nata per le mappe di calore, vuol dire
/// spenderlo dove non passa quasi nessuno.
///
/// Su un waterfall la scala non è popolata in modo uniforme. Con l'auto-range
/// il fondo si posa poco sopra il rumore e la vetta dieci decibel sopra il
/// 99,5° percentile: il traffico ordinario — una fonia che arriva venti
/// decibel sopra il fondo, una digitale che ne arriva dieci — vive nel primo
/// terzo abbondante di quell'intervallo, mentre la metà alta la raggiungono
/// solo le stazioni locali e i disturbi. Il salto va lì, dove sta il traffico.
///
/// Il prezzo è che i segnali forti si accalcano nella parte calda della rampa,
/// e infatti da questo punto in su le tabelle qui sotto tengono gli stop
/// larghi: `tst_waterfall` verifica che due livelli a un quarto di scala di
/// distanza restino distinguibili, ed è quel test a dire fin dove si può
/// spingere questa scelta.
/// Le palette, nell'ordine in cui la UI le elenca.
///
/// Tutte crescono in intensità percepita, ma non tutte in pura luminanza: nel
/// tratto finale il rosso acceso è più scuro dell'ambra che lo precede, e in
/// Turbo la scala è portata quasi per intero dalla tinta. È una convenzione che
/// vale la pena rispettare — sui ricevitori il rosso significa "forte" da
/// sempre.
///
/// In tutte, il passaggio dal freddo al caldo è centrato attorno a 0,40: gli
/// stop che lo delimitano gli stanno intorno, il tratto freddo sotto è
/// compresso e il tratto caldo sopra è tirato per non perdere risoluzione dove
/// stanno i segnali forti. Il contratto verificato è `kWarmBreakLimit`, che
/// sta nell'header perché è quello che `tst_waterfall` controlla: 0,40 è dove
/// si è scelto di stare, 0,60 è dove non si può andare oltre.
const std::vector<std::vector<ColorStop>> &palettes()
{
    static const std::vector<std::vector<ColorStop>> table = {
        // DECODIUM: nero → indaco → ciano → verde → chartreuse → ambra → rosso
        // → bianco. Il salto è fra il verde a 0,34 e la chartreuse a 0,46.
        {
            {0.00f, 0.02f, 0.03f, 0.06f}, {0.12f, 0.05f, 0.09f, 0.35f},
            {0.24f, 0.05f, 0.42f, 0.62f}, {0.34f, 0.10f, 0.72f, 0.60f},
            {0.46f, 0.55f, 0.85f, 0.25f}, {0.62f, 0.98f, 0.72f, 0.15f},
            {0.80f, 1.00f, 0.35f, 0.15f}, {1.00f, 1.00f, 0.96f, 0.92f},
        },
        // Raptor: il blu → verde → giallo → rosso classico dei ricevitori.
        // L'arancio a 0,70 è nuovo: senza, dal giallo al rosso restava un solo
        // tratto lunghissimo e mezza scala alta finiva indistinguibile.
        {
            {0.00f, 0.00f, 0.00f, 0.00f}, {0.14f, 0.00f, 0.00f, 0.55f},
            {0.27f, 0.00f, 0.55f, 0.55f}, {0.34f, 0.00f, 0.75f, 0.00f},
            {0.46f, 0.95f, 0.95f, 0.00f}, {0.70f, 1.00f, 0.55f, 0.00f},
            {1.00f, 1.00f, 0.10f, 0.00f},
        },
        // Turbo: molte tinte distinte, per separare livelli vicini. Nasce come
        // scala percettivamente uniforme, e uniforme resta nell'ordine dei
        // colori: cambiano solo le quote a cui li si incontra.
        {
            {0.00f, 0.19f, 0.07f, 0.23f}, {0.15f, 0.24f, 0.36f, 0.81f},
            {0.27f, 0.10f, 0.72f, 0.85f}, {0.34f, 0.20f, 0.92f, 0.55f},
            {0.42f, 0.60f, 0.99f, 0.23f}, {0.58f, 0.94f, 0.85f, 0.15f},
            {0.78f, 0.99f, 0.48f, 0.09f}, {1.00f, 0.73f, 0.09f, 0.02f},
        },
        // Fuoco: nero → rosso → arancio → giallo → bianco. Qui il salto è dal
        // rosso all'arancio, e cade fra 0,24 e 0,48.
        {
            {0.00f, 0.00f, 0.00f, 0.00f}, {0.24f, 0.60f, 0.00f, 0.00f},
            {0.48f, 1.00f, 0.55f, 0.00f}, {0.72f, 1.00f, 0.95f, 0.30f},
            {1.00f, 1.00f, 1.00f, 1.00f},
        },
        // Scala di grigi: nessuna tinta a distrarre, utile in stampa e per chi
        // distingue male i colori. Non ha un salto da spostare — è una rampa
        // sola, e spezzarla per compiacere questa regola la renderebbe peggiore
        // proprio in ciò per cui la si sceglie.
        {
            {0.00f, 0.00f, 0.00f, 0.00f}, {1.00f, 1.00f, 1.00f, 1.00f},
        },
    };
    return table;
}

} // namespace

QStringList waterfallPaletteNames()
{
    // I nomi restano quelli d'uso: chi arriva da un altro programma li
    // riconosce senza doverli provare tutti.
    return {QObject::tr("DECODIUM"), QObject::tr("Raptor"), QObject::tr("Turbo"),
            QObject::tr("Fuoco"), QObject::tr("Scala di grigi")};
}

QByteArray buildWaterfallColorMap(int paletteIndex)
{
    const auto &all = palettes();
    const auto &table = all[static_cast<std::size_t>(
        std::clamp(paletteIndex, 0, static_cast<int>(all.size()) - 1))];

    // Il livello più basso di un waterfall significa «qui non c'è niente», e
    // deve avere il colore del fondo. Quasi tutte le palette partono già dal
    // nero; Turbo no — nasce come scala per mappe di calore, dove ogni punto è
    // un dato valido, e il suo primo colore è un viola pieno. Su un waterfall,
    // dove il livello minimo copre quasi tutta l'immagine, quel viola diventa
    // un velo steso sullo schermo: è il difetto che si vedeva prima ancora
    // della taratura della scala.
    //
    // Gli si antepone il fondo su un tratto breve, senza toccare il resto della
    // rampa: chi sceglie Turbo continua a vedere Turbo dove ci sono segnali.
    std::vector<ColorStop> stops = table;
    if (!stops.empty() && stops.front().r + stops.front().g + stops.front().b > kFloorBrightness) {
        stops.front().position = kFloorFadeEnd;
        stops.insert(stops.begin(), kFloorStop);
    }

    QByteArray data;
    data.resize(kColorMapSize * 4);
    auto *out = reinterpret_cast<uchar *>(data.data());

    for (int i = 0; i < kColorMapSize; ++i) {
        const float t = static_cast<float>(i) / (kColorMapSize - 1);

        std::size_t segment = 0;
        while (segment + 2 < stops.size() && t > stops[segment + 1].position)
            ++segment;

        const ColorStop &a = stops[segment];
        const ColorStop &b = stops[segment + 1];
        const float span = std::max(b.position - a.position, 1e-6f);
        const float k = std::clamp((t - a.position) / span, 0.0f, 1.0f);

        out[i * 4 + 0] = static_cast<uchar>(std::lround(255.0f * (a.r + (b.r - a.r) * k)));
        out[i * 4 + 1] = static_cast<uchar>(std::lround(255.0f * (a.g + (b.g - a.g) * k)));
        out[i * 4 + 2] = static_cast<uchar>(std::lround(255.0f * (a.b + (b.b - a.b) * k)));
        out[i * 4 + 3] = 255;
    }
    return data;
}

} // namespace dsdr::app
