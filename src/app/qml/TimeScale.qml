// SPDX-License-Identifier: GPL-3.0-or-later
// Asse dei tempi del waterfall.
//
// Risponde alla domanda che ci si fa guardando una traccia che sta scendendo:
// «quello quando è passato?». Senza, la storia è un'immagine senza scala, e
// due segnali a distanza di mezzo schermo possono essere lontani due secondi o
// venti a seconda della banda e della FFT.
//
// I secondi arrivano da una misura, non da una costante: vedi
// PanadapterView::historySeconds().
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Quanti secondi copre l'intera altezza del waterfall. Zero finché la
    /// misura non è pronta: in quel caso non si disegna nulla, che è meglio
    /// di una scala inventata.
    property real historySeconds: 0

    /// Frazione di altezza occupata dallo spettro: il waterfall è il resto.
    property real spectrumRatio: 0.45

    /// Spaziatura desiderata fra le tacche, in pixel.
    property real targetPixelSpacing: 64

    readonly property real waterfallTop: height * spectrumRatio
    readonly property real waterfallHeight: Math.max(0, height - waterfallTop)

    readonly property bool ready: historySeconds > 0 && isFinite(historySeconds)
                                 && waterfallHeight > 20

    /// Passo fra le tacche, in secondi, scelto fra valori che si leggono:
    /// 1, 2, 5, 10, 15, 30, 60.
    ///
    /// Stessa cautela di FrequencyGrid e LevelScale: un passo degenerato a
    /// zero renderebbe `Infinity` il numero di tacche e il Repeater
    /// istanzierebbe delegate finché la UI non smette di rispondere.
    readonly property real stepSeconds: {
        if (!ready)
            return 1

        const divisions = Math.max(2, Math.floor(waterfallHeight / Math.max(1, targetPixelSpacing)))
        const raw = historySeconds / divisions
        if (!(raw > 0) || !isFinite(raw))
            return 1

        const nice = [1, 2, 5, 10, 15, 30, 60, 120, 300]
        for (let i = 0; i < nice.length; ++i) {
            if (raw <= nice[i])
                return nice[i]
        }
        return nice[nice.length - 1]
    }

    readonly property int tickCount:
        ready ? Math.max(1, Math.min(24, Math.floor(historySeconds / stepSeconds) + 1)) : 0

    /// Ordinata di un istante, in secondi trascorsi dalla riga più recente.
    function yForSeconds(seconds) {
        if (!ready)
            return 0
        return waterfallTop + (seconds / historySeconds) * waterfallHeight
    }

    Repeater {
        model: root.tickCount

        delegate: Item {
            id: tick

            required property int index

            readonly property real seconds: index * root.stepSeconds

            x: 0
            y: root.yForSeconds(seconds)
            width: root.width
            height: 1
            // La tacca dello zero coincide con il bordo: la riga la
            // coprirebbe il piano bande, e l'etichetta basta da sola.
            visible: index > 0 && y < root.height - 2

            Rectangle {
                anchors.fill: parent
                color: Theme.spectrumGrid
                opacity: 0.55
            }

            Text {
                // A destra, dove il waterfall ha meno da dire: la sintonia sta
                // al centro e i segnali interessanti pure.
                anchors.right: parent.right
                anchors.rightMargin: 5
                y: -height - 1
                text: root.stepSeconds >= 60
                      ? qsTr("%1 min").arg(Math.round(tick.seconds / 60))
                      : qsTr("%1 s").arg(Math.round(tick.seconds))
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textDisabled
            }
        }
    }
}
