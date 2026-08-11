// SPDX-License-Identifier: GPL-3.0-or-later
// Un interruttore compatto della targa: acceso o spento, e si vede da lontano.
//
// I filtri di disturbo si accendono guardando lo spettro, non la colonna: si
// alza il rumore, si preme, si sente se è servito. Per questo stanno anche
// qui, e per questo devono dire il proprio stato con il colore — chi ascolta
// non ha tempo di leggere quale dei quattro è attivo.
import QtQuick
import DecodiumSdr

Rectangle {
    id: root

    property alias text: label.text
    property bool checked: false

    /// La tinta da accesa. Alcuni di questi filtri costano banda passante o
    /// ritardo, e chi li tiene accesi deve saperlo: il colore lo ricorda.
    property color activeColor: Theme.accent

    signal toggled()

    implicitWidth: Math.max(34, label.implicitWidth + 2 * Theme.spacing)
    implicitHeight: label.implicitHeight + 6
    radius: Theme.radiusSmall
    color: checked ? activeColor : "transparent"
    border.width: 1
    border.color: checked ? activeColor
                : hover.hovered ? Theme.borderStrong : Theme.border
    opacity: enabled ? 1 : 0.4

    Behavior on color {
        ColorAnimation { duration: Theme.animationFast }
    }

    Text {
        id: label

        anchors.centerIn: parent
        font.pixelSize: Theme.fontSmall
        font.bold: root.checked
        // Da acceso il fondo è chiaro: qui il contrasto è lo sfondo della
        // finestra, non una tinta del tema.
        color: root.checked ? Theme.background
             : hover.hovered ? Theme.textPrimary : Theme.textSecondary
    }

    HoverHandler {
        id: hover
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: root.enabled
        onTapped: root.toggled()
    }
}
