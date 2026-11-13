// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

// A compact navigation aid; it does not change tuning or audio routing.
Flow {
    id: root
    property var channels
    property int selectedIndex: -1
    signal selected(int row)
    spacing: Theme.spacingTight
    Repeater {
        model: root.channels
        delegate: DsdrButton {
            required property int index
            required property string label
            required property color channelColor
            required property string modeName
            objectName: "channelQuick" + index
            text: label + " · " + modeName
            checked: root.selectedIndex === index
            accentColor: channelColor
            implicitWidth: Math.max(80, contentItem.implicitWidth + 16)
            implicitHeight: 28
            onClicked: root.selected(index)
        }
    }
}
