// SPDX-License-Identifier: GPL-3.0-or-later
// Controlli hardware del percorso RTL-SDR nativo.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("DEVICE  ·  RTL-SDR NATIVO")

    readonly property var gainRange: Session.nativeCommand("rtlsdr.gainRange", {}) || ({})
    property bool automaticGain: true
    property real manualGainDb: 28.0
    property int ppm: 0
    property bool biasTee: false
    property int directSampling: 0
    property bool offsetTuning: false

    function applyGain() {
        Session.nativeCommand("rtlsdr.setGain",
                              { "db": automaticGain ? -1 : manualGainDb })
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("Guadagno")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        Item { Layout.fillWidth: true }
        DsdrButton {
            text: qsTr("Auto")
            implicitWidth: 56
            implicitHeight: 22
            checkable: true
            checked: root.automaticGain
            onToggled: {
                root.automaticGain = checked
                root.applyGain()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        enabled: !root.automaticGain

        DsdrSlider {
            Layout.fillWidth: true
            from: root.gainRange.min !== undefined ? root.gainRange.min : 0
            to: root.gainRange.max !== undefined ? root.gainRange.max : 49.6
            value: root.manualGainDb
            onMoved: {
                root.manualGainDb = value
                root.applyGain()
            }
        }
        Text {
            text: root.automaticGain ? qsTr("auto") : root.manualGainDb.toFixed(1) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: root.automaticGain ? Theme.textDisabled : Theme.textPrimary
            Layout.preferredWidth: 54
            horizontalAlignment: Text.AlignRight
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        Text {
            text: qsTr("Correzione")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        DsdrSlider {
            Layout.fillWidth: true
            from: -100; to: 100; stepSize: 1
            value: root.ppm
            onMoved: {
                root.ppm = Math.round(value)
                Session.nativeCommand("rtlsdr.setPpm", { "ppm": root.ppm })
            }
        }
        Text {
            text: root.ppm + " ppm"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 62
            horizontalAlignment: Text.AlignRight
        }
    }

    DsdrButton {
        Layout.fillWidth: true
        implicitHeight: 24
        text: root.biasTee ? qsTr("Bias tee acceso") : qsTr("Bias tee spento")
        checkable: true
        checked: root.biasTee
        danger: root.biasTee
        onToggled: {
            root.biasTee = checked
            Session.nativeCommand("rtlsdr.setBiasTee", { "enabled": checked })
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        Text {
            text: qsTr("Direct sampling")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        DsdrComboBox {
            Layout.fillWidth: true
            model: [qsTr("Off"), qsTr("I"), qsTr("Q")]
            currentIndex: root.directSampling
            onActivated: {
                root.directSampling = currentIndex
                Session.nativeCommand("rtlsdr.setDirectSampling", { "mode": currentIndex })
            }
        }
    }

    DsdrButton {
        Layout.fillWidth: true
        implicitHeight: 24
        text: root.offsetTuning ? qsTr("Offset tuning acceso") : qsTr("Offset tuning spento")
        checkable: true
        checked: root.offsetTuning
        onToggled: {
            root.offsetTuning = checked
            Session.nativeCommand("rtlsdr.setOffsetTuning", { "enabled": checked })
        }
    }
}
