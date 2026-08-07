// SPDX-License-Identifier: GPL-3.0-or-later
// Griglia di frequenza dello spettro.
//
// Componente volutamente privo di dipendenze da Session: riceve span e centro
// come proprietà, così la logica del passo può essere verificata da un test
// Qt Quick senza tirarsi dietro backend, DSP e audio.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Larghezza della banda mostrata, in Hz. Deve essere > 0.
    property real spanHz: 1
    /// Frequenza al centro della vista, in Hz.
    property real centerHz: 0
    /// Frazione di altezza occupata dallo spettro (informativa: il rendering
    /// della divisione avviene sulla GPU, dentro PanadapterView).
    property real spectrumRatio: 0.45
    /// Distanza delle etichette dal bordo superiore.
    property real labelTopMargin: 28
    /// Spaziatura desiderata fra le tacche, in pixel.
    property real targetPixelSpacing: 110

    readonly property real startHz: centerHz - spanHz / 2

    /// Passo "tondo" (1, 2, 5 × 10ⁿ) più vicino alla spaziatura desiderata.
    ///
    /// Il risultato è sempre finito e positivo: durante il primo layout
    /// `width` vale 0 e il calcolo può degenerare in 0 o NaN. Un passo nullo
    /// renderebbe `Infinity` il numero di tacche, e il Repeater proverebbe a
    /// istanziare delegate senza fine bloccando il thread della UI — è
    /// esattamente il difetto che questo componente esiste per non ripetere.
    readonly property real stepHz: {
        const usableSpan = (spanHz > 0 && isFinite(spanHz)) ? spanHz : 1
        const divisions = Math.max(3, Math.floor(width / Math.max(1, targetPixelSpacing)))
        const raw = usableSpan / divisions
        const fallback = Math.max(1, usableSpan / 10)

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

    /// Numero di tacche, con un tetto esplicito: nessun calcolo di layout deve
    /// poter chiedere alla scena migliaia di elementi.
    readonly property int tickCount:
        Math.max(2, Math.min(64, Math.ceil((spanHz > 0 ? spanHz : 1) / stepHz) + 1))

    /// Numero di decimali con cui etichettare, in funzione del passo.
    readonly property int labelDecimals: stepHz < 1000 ? 5 : (stepHz < 10000 ? 4 : 3)

    function xForFrequency(hz) {
        return (hz - startHz) / (spanHz > 0 ? spanHz : 1) * width
    }

    Repeater {
        id: ticks
        model: root.tickCount

        // Il delegate ha una geometria propria (larga quanto la linea, alta
        // quanto la griglia): posizionare figli dentro un Item di dimensione
        // nulla rende il layout dipendente da dettagli non garantiti.
        delegate: Item {
            id: tick

            required property int index

            readonly property real tickHz:
                Math.ceil(root.startHz / root.stepHz) * root.stepHz + index * root.stepHz

            x: root.xForFrequency(tickHz)
            y: 0
            width: 1
            height: root.height
            visible: x >= 0 && x <= root.width

            Rectangle {
                anchors.fill: parent
                color: Theme.spectrumGrid
            }

            Text {
                id: label

                // L'etichetta esce dalla linea: niente clip, e all'ultimo
                // tick si rientra a sinistra per non uscire dal pannello.
                x: (tick.x + width + 6 > root.width) ? -(width + 4) : 4

                // In alto, sotto la fascia dei flag VFO: la linea che separa
                // spettro e waterfall è decisa dal rendering GPU e non è un
                // riferimento che il QML possa calcolare con certezza.
                y: root.labelTopMargin

                text: (tick.tickHz / 1e6).toFixed(root.labelDecimals)
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textSecondary
            }
        }
    }
}
