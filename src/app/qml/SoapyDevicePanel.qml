// SPDX-License-Identifier: GPL-3.0-or-later
// Pannello del backend SoapySDR: guadagno, AGC hardware e antenna.
//
// È uno dei pochi punti in cui `Session.nativeCommand()` è lecito (§4.1): i
// comandi qui usati esistono solo per SoapySDR e non hanno senso per una radio
// che demodula a bordo.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("DEVICE  ·  %1").arg(driverName.toUpperCase())

    readonly property string driverName: Session.nativeCommand("soapy.driver", {}) || "soapy"
    readonly property var gainRange: Session.nativeCommand("soapy.gainRange", {}) || ({})
    readonly property var antennas: Session.nativeCommand("soapy.antennas", {}) || []

    property bool automaticGain: true
    property real manualGainDb: 20

    function applyGain() {
        // Un valore negativo significa "lascia decidere al device".
        Session.nativeCommand("soapy.setGain",
                              { "db": automaticGain ? -1 : manualGainDb })
    }

    // ── Guadagno ─────────────────────────────────────────────────────────
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
            // L'AGC hardware non c'è su tutti i device: se manca, resta solo
            // il guadagno manuale.
            enabled: root.gainRange.hasAgc !== false
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
            to: root.gainRange.max !== undefined ? root.gainRange.max : 50
            value: root.manualGainDb
            onMoved: {
                root.manualGainDb = value
                root.applyGain()
            }
        }

        Text {
            text: root.automaticGain ? qsTr("auto") : Math.round(root.manualGainDb) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: root.automaticGain ? Theme.textDisabled : Theme.textPrimary
            Layout.preferredWidth: 48
            horizontalAlignment: Text.AlignRight
        }
    }

    // ── Antenna ──────────────────────────────────────────────────────────
    // Compare solo se il device ne dichiara più di una: mostrare una tendina
    // con una sola voce è rumore.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        visible: root.antennas.length > 1

        Text {
            text: qsTr("Antenna")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrComboBox {
            Layout.fillWidth: true
            implicitHeight: 24
            model: root.antennas
            onActivated: Session.nativeCommand("soapy.setAntenna", { "antenna": currentText })
        }
    }
}
