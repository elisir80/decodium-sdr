// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

Button {
    id: control

    property color accentColor: Theme.accent
    property bool danger: false

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(84, contentItem.implicitWidth + 2 * Theme.spacingLoose)
    padding: Theme.spacing

    contentItem: Text {
        text: control.text
        font.pixelSize: Theme.fontNormal
        font.bold: control.checked
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
