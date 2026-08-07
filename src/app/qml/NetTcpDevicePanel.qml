// SPDX-License-Identifier: GPL-3.0-or-later
// Pannello del backend rtl_tcp: guadagno, correzione in ppm, bias tee.
//
// Sono impostazioni che esistono solo per questo protocollo, ed è la ragione
// per cui vivono qui e non nell'interfaccia generale (§4.1).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("RTL_TCP")

    property bool automaticGain: true
    property real manualGainDb: 28.8
    property int ppm: 0
    property bool biasTee: false

    function applyGain() {
        // Il protocollo vuole decimi di dB; negativo = guadagno automatico.
        Session.nativeCommand("nettcp.setGain",
                              { "tenthsDb": automaticGain ? -1 : Math.round(manualGainDb * 10) })
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
            from: 0; to: 49.6
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
            Layout.preferredWidth: 52
            horizontalAlignment: Text.AlignRight
        }
    }

    // ── Correzione di frequenza ──────────────────────────────────────────
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
                Session.nativeCommand("nettcp.setPpm", { "ppm": root.ppm })
            }
        }

        Text {
            // Il quarzo di una chiavetta economica sbaglia di decine di ppm:
            // senza correzione una stazione si trova a chilohertz di distanza.
            text: root.ppm + " ppm"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 62
            horizontalAlignment: Text.AlignRight
        }
    }

    // ── Bias tee ─────────────────────────────────────────────────────────
    DsdrButton {
        Layout.fillWidth: true
        implicitHeight: 24
        text: root.biasTee ? qsTr("Bias tee acceso") : qsTr("Bias tee spento")
        checkable: true
        checked: root.biasTee
        danger: root.biasTee
        onToggled: {
            root.biasTee = checked
            Session.nativeCommand("nettcp.setBiasTee", { "enabled": checked })
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.biasTee
        // Alimentare un'antenna passiva non la rompe, ma alimentare un
        // preamplificatore già alimentato sì: vale un avviso.
        text: qsTr("Tensione presente sul connettore d'antenna.")
        font.pixelSize: Theme.fontSmall
        color: Theme.warning
        wrapMode: Text.WordWrap
    }
}
