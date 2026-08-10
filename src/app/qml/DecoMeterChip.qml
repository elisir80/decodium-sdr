// SPDX-License-Identifier: GPL-3.0-or-later
// Il tastino degli strumenti: piccolo, con il bordo acceso quando è scelto.
//
// Sta a parte perché ne servono sei sullo stesso quadrante — tre letture e tre
// canali — e ripeterne il disegno sei volte vorrebbe dire che al primo ritocco
// due di essi resterebbero indietro.
import QtQuick
import DecodiumSdr

Rectangle {
    id: root

    property alias text: label.text
    property bool current: false

    signal pressed()

    implicitWidth: label.implicitWidth + 2 * Theme.spacingTight
    implicitHeight: label.implicitHeight + Theme.spacingTight
    radius: Theme.radiusSmall
    color: current ? Theme.accentDim : "transparent"
    border.width: 1
    border.color: current ? Theme.accent : Theme.border
    opacity: enabled ? 1 : 0.35

    Behavior on color {
        ColorAnimation { duration: Theme.animationFast }
    }

    Text {
        id: label

        anchors.centerIn: parent
        font.pixelSize: Math.max(8, Theme.fontSmall - 2)
        font.family: Theme.monoFamily
        font.bold: root.current
        color: root.current ? Theme.textPrimary : Theme.textSecondary
    }

    TapHandler {
        enabled: root.enabled
        onTapped: root.pressed()
    }
}
