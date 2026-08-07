// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

ComboBox {
    id: control

    implicitHeight: Theme.controlHeight

    contentItem: Text {
        leftPadding: Theme.spacing
        rightPadding: control.indicator.width + Theme.spacing
        text: control.displayText
        font.pixelSize: Theme.fontNormal
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Canvas {
        x: control.width - width - Theme.spacing
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 10
        height: 6
        contextType: "2d"

        Connections {
            target: control
            function onPressedChanged() { control.indicator.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.moveTo(0, 0)
            ctx.lineTo(width, 0)
            ctx.lineTo(width / 2, height)
            ctx.closePath()
            ctx.fillStyle = control.pressed ? Theme.accent : Theme.textSecondary
            ctx.fill()
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.pressed ? Theme.surfaceRaised : Theme.surface
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 280)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.surfaceRaised
            border.color: Theme.borderStrong
            border.width: 1
            radius: Theme.radiusSmall
        }
    }

    delegate: ItemDelegate {
        required property var model
        required property int index

        width: control.width
        height: Theme.controlHeight
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: model[control.textRole] !== undefined ? model[control.textRole] : model.modelData
            color: Theme.textPrimary
            font.pixelSize: Theme.fontNormal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: highlighted ? Theme.accentDim : "transparent"
        }
    }
}
