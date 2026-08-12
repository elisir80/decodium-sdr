// SPDX-License-Identifier: GPL-3.0-or-later
// Prima e dopo la catena, sullo stesso grafico.
//
// **Perché sovrapposti e non affiancati.** Due grafici accanto costringono a
// spostare lo sguardo, e fra uno sguardo e l'altro si mette in mezzo la
// memoria — che di un'immagine dura poco più che di un suono. Sovrapposti, la
// differenza fra le due catene è l'area fra le curve: si legge in un colpo, e
// non c'è niente da ricordare.
//
// L'orecchio dice se una voce è bella; questo dice **perché**. Una gobba a
// duecento hertz che l'orecchio chiama «calda» qui si vede, e si vede anche
// che sta occupando banda che ai corrispondenti non serve.
import QtQuick
import QtQuick.Shapes
import DecodiumSdr

Item {
    id: root

    implicitHeight: 72

    /// Fondo scala della verticale, in dBFS. Sessanta decibel: la stessa scala
    /// della barra dei livelli, così i due strumenti si leggono con lo stesso
    /// metro.
    readonly property real floorDb: -72
    readonly property real ceilingDb: -6

    /// Le curve si disegnano solo mentre passa qualcosa. Ferme sarebbero una
    /// misura vecchia che si crederebbe attuale, e uno spettro fermo è
    /// indistinguibile da uno spettro giusto.
    readonly property bool live: Session.transmitting

    function pointsFor(values) {
        const out = []
        if (!values || values.length === 0)
            return out
        const span = root.ceilingDb - root.floorDb
        for (let i = 0; i < values.length; ++i) {
            const x = plot.width * i / (values.length - 1)
            const clamped = Math.max(root.floorDb, Math.min(root.ceilingDb, values[i]))
            out.push(Qt.point(x, plot.height * (1 - (clamped - root.floorDb) / span)))
        }
        return out
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.border
        radius: 3
    }

    // Le tacche del chilohertz. Senza, la curva non ha una scala e la gobba
    // che si vede non si sa dove sia — che è quanto serve per non poterci fare
    // niente.
    Repeater {
        model: Math.floor(Session.voiceSpectrumSpanHz / 1000)

        delegate: Item {
            required property int index

            Rectangle {
                x: plot.x + plot.width * (parent.index + 1) * 1000
                   / Session.voiceSpectrumSpanHz
                y: plot.y
                width: 1
                height: plot.height
                color: Theme.border
                opacity: 0.5
            }

            Text {
                x: plot.x + plot.width * (parent.index + 1) * 1000
                   / Session.voiceSpectrumSpanHz + 3
                y: plot.y + plot.height - 12
                text: qsTr("%1k").arg(parent.index + 1)
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
            }
        }
    }

    Item {
        id: plot

        anchors.fill: parent
        anchors.margins: 4
        clip: true

        // Il «prima» sotto e spento: è il riferimento, non il risultato. Se
        // avesse lo stesso peso dell'altro si guarderebbero entrambi, e non si
        // guarderebbe la differenza.
        Shape {
            anchors.fill: parent
            visible: root.live
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Theme.textDisabled
                strokeWidth: 1
                fillColor: "transparent"

                PathPolyline {
                    path: root.pointsFor(Session.voiceSpectrumDry)
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.live
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Theme.accent
                strokeWidth: 1.6
                fillColor: "transparent"

                PathPolyline {
                    path: root.pointsFor(Session.voiceSpectrumWet)
                }
            }
        }
    }

    // A riposo il riquadro non resta vuoto senza spiegazione: dire che non c'è
    // niente da mostrare costa una riga e toglie il dubbio che sia rotto.
    // In basso e non al centro: al centro finisce sotto i punti
    // dell'equalizzatore, che restano visibili anche a riposo perche' e' li'
    // che si va a cercarli.
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3
        visible: !root.live
        text: qsTr("il confronto compare mentre si trasmette")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
    }

    // La legenda sta in alto a destra, dove non c'è mai segnale: la voce sta
    // in basso a sinistra dello spettro.
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 5
        spacing: Theme.spacingTight
        visible: root.live

        Text {
            text: qsTr("PRIMA")
            font.pixelSize: Theme.fontSmall
            color: Theme.textDisabled
        }

        Text {
            text: qsTr("DOPO")
            font.pixelSize: Theme.fontSmall
            font.bold: true
            color: Theme.accent
        }
    }
}
