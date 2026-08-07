// SPDX-License-Identifier: GPL-3.0-or-later
// S-meter: scala S1–S9+ derivata dal livello in dBFS del canale.
import QtQuick
import DecodiumSdr

Item {
    id: root

    required property real levelDb
    property real floorDb: -140
    property real ceilingDb: -20

    implicitHeight: 26

    readonly property real fraction:
        Math.max(0, Math.min(1, (levelDb - floorDb) / (ceilingDb - floorDb)))

    /// S9 convenzionalmente a metà scala; oltre si conta in dB "più".
    readonly property string readout: {
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

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 1
            width: Math.max(0, (parent.width - 2) * root.fraction)
            radius: Theme.radiusSmall - 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.success }
                GradientStop { position: 0.62; color: Theme.warning }
                GradientStop { position: 1.0; color: Theme.danger }
            }

            Behavior on width {
                NumberAnimation { duration: Theme.animationFast }
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

        Text {
            anchors.centerIn: parent
            text: root.readout + "   " + Math.round(root.levelDb) + " dBFS"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
        }
    }
}
