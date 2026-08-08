// SPDX-License-Identifier: GPL-3.0-or-later
// Scala di ampiezza dello spettro.
//
// Lo spettro mostrava le frequenze ma non i livelli: si vedeva che un segnale
// era più alto di un altro, non di quanto, e i cursori del fondo e della vetta
// si regolavano alla cieca. Questa è la controparte verticale di
// FrequencyGrid, e come quella non conosce Session: riceve i due estremi e li
// gradua, così un test può verificarla senza backend né DSP.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Estremi della scala, in dB. Sono gli stessi che il panadattatore usa
    /// per normalizzare i livelli.
    property real floorDb: -125
    property real ceilingDb: -25

    /// Frazione di altezza occupata dallo spettro: sotto comincia il waterfall,
    /// dove una scala di ampiezze non significherebbe nulla.
    property real spectrumRatio: 0.45

    /// Spaziatura desiderata fra le tacche, in pixel.
    ///
    /// Più fitta che nella griglia di frequenza: la fascia dello spettro è
    /// alta poche centinaia di punti e con una spaziatura larga il passo tondo
    /// finiva a 50 dB, cioè due sole linee su tutta la scala.
    property real targetPixelSpacing: 34

    /// Da che parte stanno le etichette. A sinistra per difetto: a destra c'è
    /// il pannello dei comandi del waterfall.
    property bool labelsOnLeft: true

    readonly property real spanDb: {
        const span = ceilingDb - floorDb
        return (span > 0 && isFinite(span)) ? span : 1
    }

    readonly property real spectrumHeight: Math.max(0, height * spectrumRatio)

    /// Passo tondo fra le tacche, scelto fra i valori che un operatore legge
    /// senza pensarci: 1, 2, 5, 10, 20 dB e così via.
    ///
    /// Vale qui la stessa cautela di FrequencyGrid: al primo layout l'altezza
    /// è zero e il calcolo degenera. Un passo nullo renderebbe `Infinity` il
    /// numero di tacche e il Repeater istanzierebbe delegate finché il thread
    /// della UI non smette di rispondere.
    readonly property real stepDb: {
        const divisions = Math.max(3, Math.floor(spectrumHeight / Math.max(1, targetPixelSpacing)))
        const raw = spanDb / divisions
        const fallback = Math.max(1, spanDb / 5)

        if (!(raw > 0) || !isFinite(raw))
            return fallback

        const magnitude = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10))
        if (!(magnitude > 0) || !isFinite(magnitude))
            return fallback

        const normalized = raw / magnitude
        let nice = 1
        if (normalized > 5) nice = 10
        else if (normalized > 2) nice = 5
        else if (normalized > 1) nice = 2
        return nice * magnitude
    }

    /// Numero di tacche, con un tetto esplicito. Vedi FrequencyGrid.
    readonly property int tickCount:
        Math.max(2, Math.min(32, Math.ceil(spanDb / stepDb) + 1))

    /// Livello della prima tacca: il multiplo del passo appena sopra il fondo.
    readonly property real firstTickDb: Math.ceil(floorDb / stepDb) * stepDb

    /// Decimali con cui etichettare, in funzione del passo.
    ///
    /// Su una scala compressa il passo scende sotto il decibel, e arrotondando
    /// all'intero si ottengono tacche diverse con la stessa etichetta: −81,
    /// −81, −82, −82. Una scala che ripete i numeri è peggio di una senza
    /// numeri, perché sembra funzionare.
    readonly property int labelDecimals: stepDb >= 1 ? 0 : (stepDb >= 0.1 ? 1 : 2)

    /// Ordinata di un livello. Il fondo scala sta sulla linea che separa
    /// spettro e waterfall, la vetta in cima al pannello.
    function yForLevel(db) {
        return spectrumHeight - (db - floorDb) / spanDb * spectrumHeight
    }

    Repeater {
        model: root.tickCount

        delegate: Item {
            id: tick

            required property int index

            readonly property real tickDb: root.firstTickDb + index * root.stepDb

            x: 0
            y: root.yForLevel(tickDb)
            width: root.width
            height: 1

            // Fuori dalla fascia dello spettro la tacca non ha significato: il
            // waterfall non ha un asse delle ampiezze.
            visible: y >= 0 && y <= root.spectrumHeight

            Rectangle {
                anchors.fill: parent
                color: Theme.spectrumGrid
            }

            Text {
                id: label

                // Appoggiata sopra la linea, non centrata su di essa: così non
                // la copre e resta leggibile anche dove la traccia la sfiora.
                x: root.labelsOnLeft ? 4 : root.width - width - 4
                y: -height - 1

                text: tick.tickDb.toFixed(root.labelDecimals)
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textDisabled
            }
        }
    }

    // L'unità una volta sola, in cima: ripeterla su ogni tacca sarebbe
    // rumore: chi guarda uno spettro sa che quei numeri sono decibel.
    Text {
        x: root.labelsOnLeft ? 4 : root.width - width - 4
        y: 2
        text: qsTr("dB")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
    }
}
