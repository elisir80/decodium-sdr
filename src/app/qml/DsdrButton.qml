// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

Button {
    id: control

    property color accentColor: Theme.accent
    property bool danger: false

    /// Misura del carattere. Le pulsantiere fitte — modi, filtri, AGC — stanno
    /// in una colonna stretta e con il corpo normale troncano proprio le
    /// parole che distinguono un pulsante dall'altro.
    property real fontSize: Theme.fontNormal

    /// Il grassetto sul pulsante scelto. In una pulsantiera fitta lo sfondo
    /// pieno dice già quale sia, e il grassetto aggiunge solo larghezza: la
    /// stessa parola in tondo ci sta, in grassetto si tronca.
    property bool boldWhenChecked: true

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(84, contentItem.implicitWidth + 2 * Theme.spacingLoose)
    padding: Theme.spacing

    contentItem: Text {
        text: control.text
        font.pixelSize: control.fontSize
        font.bold: control.checked && control.boldWhenChecked
        color: !control.enabled ? Theme.textDisabled
             : control.checked ? Theme.background
             : Theme.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: {
            const base = control.danger ? Theme.danger : control.accentColor
            if (!control.enabled)
                return Theme.surface
            if (control.checked)
                return base
            if (control.pressed)
                return Qt.darker(Theme.surfaceRaised, 1.3)
            if (control.hovered)
                return Theme.surfaceRaised
            return Theme.surface
        }
        border.width: 1
        border.color: control.checked ? Qt.lighter(control.danger ? Theme.danger : control.accentColor, 1.2)
                                      : Theme.border

        Behavior on color {
            ColorAnimation { duration: Theme.animationFast }
        }
    }
}
