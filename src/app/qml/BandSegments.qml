// SPDX-License-Identifier: GPL-3.0-or-later
// Striscia dei segmenti d'uso, sul confine fra spettro e waterfall.
//
// Dice dove si sta: CW, dati, fari, fonia. Chi opera conosce a memoria i
// confini delle bande che frequenta, ma su una banda nuova — o dopo un cambio
// di piano — è la differenza fra chiamare dove si può e dove non si dovrebbe.
//
// Come FrequencyGrid, non conosce Session: riceve l'intervallo e lo disegna.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Estremi della porzione di banda mostrata, in hertz.
    property real startHz: 0
    property real endHz: 0

    /// Frazione di altezza occupata dallo spettro: la striscia si appoggia al
    /// confine con il waterfall.
    property real spectrumRatio: 0.45

    /// Altezza della striscia.
    property real stripHeight: 15

    readonly property real spanHz: {
        const span = endHz - startHz
        return (span > 0 && isFinite(span)) ? span : 0
    }

    /// I segmenti che toccano la porzione visibile. Vuoto fuori dalle bande
    /// amatoriali, ed è giusto così: là non c'è nessun piano da mostrare.
    readonly property var visibleSegments:
        spanHz > 0 ? BandPlan.segmentsIn(startHz, endHz) : []

    function xForFrequency(hz) {
        return (hz - startHz) / (spanHz > 0 ? spanHz : 1) * width
    }

    function colorFor(kind) {
        switch (kind) {
        case "cw":     return Theme.segmentCw
        case "digi":   return Theme.segmentDigi
        case "beacon": return Theme.segmentBeacon
        case "phone":  return Theme.segmentPhone
        }
        return Theme.border
    }

    // Il contenitore copre tutto il pannello — chi lo usa gli dà `anchors.fill`
    // — e a posizionarsi è la striscia: appoggiata sopra il confine con il
    // waterfall, non a cavallo, perché mangiarne le prime righe vorrebbe dire
    // perdere proprio i secondi appena trascorsi.
    Rectangle {
        id: strip

        y: Math.max(0, root.height * root.spectrumRatio - root.stripHeight)
        width: root.width
        height: root.stripHeight
        color: Theme.surfaceSunken
        visible: root.visibleSegments.length > 0

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
        }

        Repeater {
            model: root.visibleSegments

            delegate: Item {
                id: segment

                required property var modelData

                readonly property real x0: root.xForFrequency(modelData.start)
                readonly property real x1: root.xForFrequency(modelData.end)

                x: Math.max(0, x0)
                y: 1
                width: Math.max(0, Math.min(root.width, x1) - Math.max(0, x0))
                height: strip.height - 1
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: root.colorFor(segment.modelData.kind)
                    opacity: Theme.segmentOpacity
                }

                // Il confine destro del segmento, dove sta la riga che conta:
                // è lì che finisce un modo e ne comincia un altro.
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: root.colorFor(segment.modelData.kind)
                    opacity: 0.7
                }

                Text {
                    anchors.centerIn: parent
                    // Sotto una certa larghezza l'etichetta non ci sta e
                    // verrebbe tagliata a metà parola: meglio il solo colore.
                    visible: segment.width > implicitWidth + 8
                    text: BandPlan.segmentLabel(segment.modelData.kind)
                    font.pixelSize: Theme.fontSmall - 2
                    font.letterSpacing: 1
                    color: Theme.textSecondary
                }
            }
        }
    }
}
