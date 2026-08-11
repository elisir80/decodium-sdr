// SPDX-License-Identifier: GPL-3.0-or-later
// S-meter: scala S1–S9+ derivata dal livello in dBFS del canale.
import QtQuick
import DecodiumSdr

Item {
    id: root

    required property real levelDb
    property real floorDb: -140
    property real ceilingDb: -20
    property bool showSUnits: true

    /// Il fondo di rumore del canale: da lì nasce il riferimento della scala
    /// S, perché su un ricevitore non tarato non esiste un livello assoluto
    /// che valga S9 — dipende dal guadagno della catena.
    property real noiseFloorDb: -140
    readonly property real s9ReferenceDb: SMeterScale.s9From(noiseFloorDb)

    implicitHeight: 26

    readonly property real fraction:
        Math.max(0, Math.min(1, (levelDb - floorDb) / (ceilingDb - floorDb)))

    /// La lettura viene da [SMeterScale], non da un conto fatto qui: tre
    /// strumenti che si calcolano i punti S per conto proprio prima o poi
    /// dicono numeri diversi guardando lo stesso segnale.
    readonly property string readout:
        showSUnits ? SMeterScale.readout(levelDb, s9ReferenceDb) : qsTr("AF")

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.border

        // La barra è una finestra che scopre un gradiente fermo, non un
        // rettangolo colorato che si allunga.
        //
        // Con il gradiente applicato direttamente alla barra, le tre tinte si
        // comprimevano nella larghezza raggiunta: un S3 mostrava comunque la
        // punta rossa, e il colore — che dovrebbe dire da solo quanto è forte
        // il segnale — non significava più niente.
        Item {
            id: barWindow

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 1
            width: Math.max(0, (parent.width - 2) * root.fraction)
            clip: true

            Behavior on width {
                NumberAnimation { duration: Theme.animationFast }
            }

            Rectangle {
                // Larghezza dell'intera scala, indipendente da quanta se ne
                // vede: è questo a tenere ferme le tinte.
                width: Math.max(0, barWindow.parent.width - 2)
                height: barWindow.height
                radius: Theme.radiusSmall - 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Theme.success }
                    GradientStop { position: 0.62; color: Theme.warning }
                    GradientStop { position: 1.0; color: Theme.danger }
                }
            }
        }

        // Tacche a S3, S6, S9. Stavano a un quarto, metà e tre quarti della
        // barra, che sono i punti giusti solo se un punto S vale un dodicesimo
        // della dinamica: ne vale sei decibel, e la tacca «S9» cadeva dove il
        // segnale era S9+30.
        Repeater {
            model: [3, 6, 9]
            delegate: Rectangle {
                required property int modelData

                readonly property real span: root.ceilingDb - root.floorDb
                readonly property real position: span === 0 ? 0
                    : (SMeterScale.levelFor(modelData, root.s9ReferenceDb) - root.floorDb) / span

                x: parent.width * Math.max(0, Math.min(1, position))
                width: 1
                height: parent.height
                color: Theme.background
                opacity: 0.5
            }
        }

        // A destra, dove la barra arriva solo con i segnali forti: al centro
        // stava quasi sempre sopra il colore, ed era la parte meno leggibile
        // del pannello.
        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: root.readout + "   " + Math.round(root.levelDb) + " dBFS"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
        }
    }
}
