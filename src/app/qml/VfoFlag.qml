// SPDX-License-Identifier: GPL-3.0-or-later
// Flag VFO trascinabile sullo spettro, colorato come il canale (§6.1).
//
// Le proprietà hanno prefisso `vfo`/`band` di proposito: il delegate del
// Repeater dichiara i ruoli del modello come `required property`, e un nome in
// comune fra le due parti creerebbe un binding su sé stesso.
import QtQuick
import DecodiumSdr

Item {
    id: root

    required property int vfoRow
    required property color vfoColor
    required property string vfoLabel
    required property real vfoFrequency
    required property int bandLowHz
    required property int bandHighHz
    required property real levelDb
    required property bool vfoSelected

    /// Conversioni fornite dal pannello contenitore.
    required property var xForFrequency
    required property var frequencyAt

    readonly property real centerX: xForFrequency(vfoFrequency)
    readonly property real filterLeft: xForFrequency(vfoFrequency + bandLowHz)
    readonly property real filterRight: xForFrequency(vfoFrequency + bandHighHz)

    anchors.fill: parent

    // ── Banda passante ───────────────────────────────────────────────────
    Rectangle {
        x: Math.min(root.filterLeft, root.filterRight)
        width: Math.max(2, Math.abs(root.filterRight - root.filterLeft))
        y: 0
        height: parent.height
        color: root.vfoColor
        opacity: root.vfoSelected ? 0.16 : 0.09
        visible: x + width > 0 && x < root.width

        Behavior on opacity {
            NumberAnimation { duration: Theme.animationFast }
        }
    }

    // ── Asse del VFO ─────────────────────────────────────────────────────
    Rectangle {
        x: root.centerX - width / 2
        width: root.vfoSelected ? 2 : 1
        y: 0
        height: parent.height
        color: root.vfoColor
        opacity: 0.9
        visible: x > -4 && x < root.width + 4
    }

    // ── Bandierina con etichetta ─────────────────────────────────────────
    Rectangle {
        id: flag

        x: Math.max(0, Math.min(root.width - width, root.centerX - width / 2))
        y: Theme.spacingTight
        width: labelText.implicitWidth + 2 * Theme.spacing
        height: 20
        radius: Theme.radiusSmall
        color: root.vfoSelected ? root.vfoColor
                                : Qt.rgba(root.vfoColor.r, root.vfoColor.g, root.vfoColor.b, 0.35)
        border.width: 1
        border.color: root.vfoColor

        Text {
            id: labelText
            anchors.centerIn: parent
            text: root.vfoLabel + "  " + (root.levelDb > -139 ? Math.round(root.levelDb) + " dB" : "—")
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: root.vfoSelected ? Theme.background : Theme.textPrimary
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.SizeHorCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            property real grabOffsetHz: 0

            onPressed: (mouse) => {
                Session.channels.currentIndex = root.vfoRow
                const pointer = mapToItem(root, mouse.x, mouse.y)
                grabOffsetHz = root.vfoFrequency - root.frequencyAt(pointer.x)
            }

            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const pointer = mapToItem(root, mouse.x, mouse.y)
                const target = root.frequencyAt(pointer.x) + grabOffsetHz
                Session.setChannelFrequency(root.vfoRow, Math.round(target))
            }

            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton && Session.channels.count > 1)
                    Session.removeChannel(root.vfoRow)
            }
        }
    }
}
