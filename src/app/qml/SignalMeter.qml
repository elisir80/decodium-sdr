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

    implicitHeight: 26

    readonly property real fraction:
        Math.max(0, Math.min(1, (levelDb - floorDb) / (ceilingDb - floorDb)))

    /// S9 convenzionalmente a metà scala; oltre si conta in dB "più".
    readonly property string readout: {
        if (!showSUnits)
            return qsTr("AF")
        const sUnits = fraction * 12
        if (sUnits <= 9)
            return "S" + Math.max(0, Math.round(sUnits))
        return "S9+" + Math.round((sUnits - 9) * 6) + "dB"
    }

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

        // Tacche a S3, S6, S9.
        Repeater {
            model: [0.25, 0.5, 0.75]
            delegate: Rectangle {
                required property real modelData
                x: parent.width * modelData
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
