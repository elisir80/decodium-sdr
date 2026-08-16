// SPDX-License-Identifier: GPL-3.0-or-later
// L'alfabeto della barra dei pannelli: ogni icona disegna quello che fa.
//
// **Perché non caratteri.** La fila di prima era ⌗ ◔ ♫ ⇉ ⏱ ⨍ ▲ ⚙ ▤ ≡ ◐ ◢:
// un simbolo matematico, una nota musicale, due frecce, un orologio, un
// integrale e quattro figure geometriche prese perché somigliavano a qualcosa.
// Nessuna famiglia, nessuna regola, e tre cerchi riempiti in tre modi diversi
// che non volevano dire tre cose diverse. Una fila così non si impara: si
// prova, e ci si ricorda la posizione invece del segno.
//
// **La regola è quella dei blocchi della catena** (`ChainFlowPanel`), che il
// progetto usa già: si disegna *quello che il pannello fa*, non un simbolo che
// gli assomiglia. Una scala con la sua tacca è la sintonia; un ago su un arco è
// lo strumento; due curve che si scostano sono le condizioni della banda.
//
// **Il riquadro è quadrato**, ventiquattro per ventiquattro, e non quello
// 72×26 dei blocchi: quello è largo e basso perché i blocchi lo sono, e
// schiacciarlo dentro un pulsante quadrato trasformava ogni cerchio in
// un'ellisse — il globo della linea grigia veniva un uovo.
pragma Singleton

import QtQuick

QtObject {
    /// Il lato del riquadro in cui i tracciati sono disegnati.
    readonly property real box: 24

    /// Il glifo di un pannello, per chiave. Vuoto se non lo conosce: chi
    /// disegna deve saperlo gestire invece di mostrare un buco.
    function of(key) {
        return glyphs[key] !== undefined ? glyphs[key] : ""
    }

    readonly property var glyphs: ({
        // ── Sintonia ─────────────────────────────────────────────────────
        // La scala di un ricevitore: le tacche, e quella lunga è dove si è.
        "sintonia":
            "M2,17 L22,17 M5,17 L5,13 M9,17 L9,14 M12,17 L12,5 " +
            "M16,17 L16,14 M20,17 L20,13",

        // ── Strumento (DECØMETER) ────────────────────────────────────────
        // L'arco di un quadrante e il suo ago: l'unico oggetto della stazione
        // che si riconosce da lontano.
        "strumento":
            "M4,18 A9,9 0 0 1 20,18 M12,18 L17,10",

        // ── Studio audio ─────────────────────────────────────────────────
        // Una forma d'onda: quello che il pannello misura.
        "audio":
            "M2,12 L5,12 L7,5 L10,19 L13,8 L16,15 L19,10 L22,12",

        // ── Audio di rete ────────────────────────────────────────────────
        // Un'onda che esce da una presa: non una freccia generica, ma audio
        // che lascia l'applicazione verso un altro ricevitore.
        "rete-audio":
            "M2,12 L5,12 L7,6 L10,18 L13,8 L16,14 M17,6 L22,6 L22,18 L17,18",

        // ── Flusso ───────────────────────────────────────────────────────
        // Due stadi in fila, collegati: è il diagramma, in piccolo.
        "flusso":
            "M1,12 L4,12 M4,8 L10,8 L10,16 L4,16 Z M10,12 L14,12 " +
            "M14,8 L20,8 L20,16 L14,16 Z M20,12 L23,12",

        // ── Macchina del tempo ───────────────────────────────────────────
        // Un orologio. Non una freccia all'indietro: una freccia curva in
        // ventidue punti si legge come uno scarabocchio, e questo pannello si
        // riconosce dalla cosa che governa.
        "tempo":
            "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 M12,6 L12,12 L16,14",

        // ── Catena di ricezione ──────────────────────────────────────────
        // Un filtro: la campana che lascia passare una banda e taglia il
        // resto. È il disegno che ogni operatore ha già visto.
        "catena":
            "M1,18 L6,18 C9,18 9,7 12,7 C15,7 15,18 18,18 L23,18",

        // ── Trasmissione ─────────────────────────────────────────────────
        // Un'antenna che irradia: lo stilo, la base e due fronti d'onda.
        "trasmissione":
            "M12,21 L12,7 M8,20 L16,20 M8,9 A5,5 0 0 1 16,9 " +
            "M5,6 A9,9 0 0 1 19,6",

        // ── Device / sorgenti ────────────────────────────────────────────
        // Il frontale di un ricevitore: la manopola e la scala. È l'oggetto,
        // non un ingranaggio — un ingranaggio vuol dire «impostazioni» e
        // questo pannello non è quello.
        "device":
            "M2,6 L22,6 L22,18 L2,18 Z M7,12 A2.5,2.5 0 1 0 7.1,12 " +
            "M12,10 L19,10 M12,14 L19,14",

        // ── Waterfall ────────────────────────────────────────────────────
        // Righe che scorrono, di densità diversa: la storia della banda.
        "waterfall":
            "M2,5 L22,5 M2,9 L10,9 M13,9 L22,9 M2,13 L7,13 M10,13 L16,13 " +
            "M19,13 L22,13 M2,17 L22,17 M2,21 L14,21",

        // ── Canali ───────────────────────────────────────────────────────
        // Un elenco con il primo scelto: sono i ricevitori aperti.
        "canali":
            "M2,5 L4,5 L4,7 L2,7 Z M7,6 L22,6 M7,12 L22,12 M7,18 L22,18",

        // ── Linea grigia ─────────────────────────────────────────────────
        // Il globo tagliato dal terminatore: metà giorno, metà notte.
        //
        // Il terminatore sta vicino al centro, non largo: al primo tentativo
        // sporgeva fino a un terzo del raggio e il disegno si leggeva come due
        // cerchi sovrapposti — un diagramma di Venn, non un pianeta.
        "greyline":
            "M3,12 A9,9 0 1 0 21,12 A9,9 0 1 0 3,12 " +
            "M12,3 C9.5,7 9.5,17 12,21",

        // ── Condizioni ───────────────────────────────────────────────────
        // Due curve che si scostano: oggi contro il solito. È esattamente
        // quello che il pannello mostra.
        "condizioni":
            "M2,17 C7,17 9,8 14,8 C18,8 20,12 22,12 " +
            "M2,20 C7,20 9,16 14,16 C18,16 20,18 22,18",
    })
}
