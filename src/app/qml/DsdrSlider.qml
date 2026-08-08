// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

Slider {
    id: control

    property color accentColor: Theme.accent

    implicitHeight: 22

    // Un cursore disabilitato deve *sembrare* disabilitato. Attenuare il solo
    // riempimento non bastava: la maniglia restava accesa come le altre, e con
    // la scala automatica attiva i cursori del fondo e della vetta sembravano
    // manovrabili mentre non lo erano.
    opacity: enabled ? 1.0 : 0.4

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.border

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.accentColor
            opacity: control.enabled ? 0.85 : 0.3
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 14
        height: 14
        radius: 7
        color: control.pressed ? control.accentColor : Theme.surfaceRaised
        border.width: 2
        border.color: control.accentColor

        Behavior on color {
            ColorAnimation { duration: Theme.animationFast }
        }
    }
}
