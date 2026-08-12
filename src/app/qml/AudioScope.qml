// SPDX-License-Identifier: GPL-3.0-or-later
// L'oscilloscopio: la forma d'onda dell'audio, nel dominio del tempo.
//
// Lo spettro dice *quali* frequenze ci sono, e per quasi tutto basta. Ci sono
// però cose che nello spettro non si vedono e nella forma d'onda saltano
// all'occhio al primo sguardo:
//
//   • la **tosatura**. Un audio che sbatte contro il fondo scala nello spettro
//     si annuncia come un tappeto di armoniche, che a occhio somiglia a
//     rumore; qui si vede la cima piatta.
//   • l'**inviluppo**. Una voce ha punte tre volte più alte del suo valore
//     medio; se non le ha, il compressore della radio sta schiacciando tutto
//     — e questo lo spettro non lo dice affatto.
//   • il **battimento**. Due portanti vicine fanno un'onda che si gonfia e si
//     sgonfia a occhio nudo, e la loro distanza si legge dal ritmo.
//
// Il disegno è una Shape e non un Canvas: un Canvas ridisegna su CPU e a
// cinquanta punti per fotogramma si sente, mentre la Shape finisce nel grafo
// della scena come tutto il resto.
import QtQuick
import QtQuick.Shapes
import DecodiumSdr

Item {
    id: root

    /// Quanti punti disegnare. Non è la risoluzione dell'audio: di ogni
    /// gruppo di campioni si prende quello di modulo maggiore, così una punta
    /// che dura un campione solo resta visibile.
    property int points: 512

    /// Quante volte al secondo si aggiorna. Venticinque bastano all'occhio e
    /// costano un quarto di quanto costerebbero cento — e l'audio, a
    /// differenza dello spettro, non ha una storia da non perdere.
    property int refreshHz: 25

    /// Guadagno verticale. Uno significa che il fondo scala tocca i bordi.
    property real gain: 1.0

    /// Acceso solo quando si vede: un oscilloscopio dentro un pannello chiuso
    /// continuerebbe a svuotare il ring venticinque volte al secondo per
    /// disegnare niente.
    property bool running: visible && Session.connected

    clip: true

    Rectangle {
        anchors.fill: parent
        color: Theme.spectrumBackground
        radius: Theme.radiusSmall
        border.width: 1
        border.color: Theme.border
    }

    // ── Riferimenti ──────────────────────────────────────────────────────
    //
    // Lo zero e i due fondo scala. Senza, la forma d'onda è un disegno: non si
    // sa se sta toccando il massimo o se è a metà strada, che è esattamente la
    // cosa che si guarda qui.
    Repeater {
        model: [
            { fraction: 0.0, strong: true },
            { fraction: 0.5, strong: false },
            { fraction: 1.0, strong: true },
        ]

        delegate: Rectangle {
            required property var modelData

            x: 0
            y: Math.round(modelData.fraction * (root.height - 1))
            width: root.width
            height: 1
            color: modelData.strong ? Theme.borderStrong : Theme.border
            opacity: modelData.strong ? 0.7 : 1.0
        }
    }

    Shape {
        anchors.fill: parent
        // Il renderer geometrico e non quello a curve: qui c'è una spezzata
        // di cinquecento segmenti che si rifà venticinque volte al secondo, e
        // il renderer a curve paga l'antialiasing analitico su ogni segmento
        // per una qualità che su una linea spessa un pixel non si vede.

        ShapePath {
            id: trace

            strokeColor: Theme.spectrumTrace
            strokeWidth: 1.4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                id: polyline
                path: [Qt.point(0, 0)]
            }
        }
    }

    /// Il livello di picco appena disegnato, da 0 a 1. Serve a chi vuole
    /// dire, accanto al disegno, quanto manca al fondo scala.
    property real lastPeak: 0

    Timer {
        interval: Math.max(20, 1000 / Math.max(1, root.refreshHz))
        running: root.running
        repeat: true

        onTriggered: {
            const samples = Session.audioWaveform(root.points)
            if (samples.length === 0)
                return

            const w = root.width
            const h = root.height
            if (!(w > 0) || !(h > 0))
                return

            const middle = h / 2
            const step = w / Math.max(1, samples.length - 1)
            const scale = middle * root.gain

            const next = []
            let peak = 0
            for (let i = 0; i < samples.length; ++i) {
                const value = samples[i]
                if (Math.abs(value) > peak)
                    peak = Math.abs(value)
                // Il segno si rovescia perché sullo schermo la y cresce verso
                // il basso, e un'onda disegnata a testa in giù è la stessa
                // onda — ma chi la guarda ci mette un attimo a fidarsi.
                next.push(Qt.point(i * step,
                                   Math.max(0, Math.min(h, middle - value * scale))))
            }

            polyline.path = next
            root.lastPeak = peak
        }
    }
}
