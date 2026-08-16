// SPDX-License-Identifier: GPL-3.0-or-later
// Audio ricevuto esportato nel formato del Network Sink di SDR++:
// PCM16 little-endian a 48 kHz, senza header.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("AUDIO DI RETE")
    draggable: true
    collapsed: true
    detachable: true

    Settings {
        id: prefs
        category: "network-audio"
        property int protocolIndex: 0
        property string udpHost: "127.0.0.1"
        property string tcpHost: "0.0.0.0"
        property int port: 7355
        property bool stereo: false
    }

    readonly property var status: Session.networkAudioStatus
    readonly property bool active: Session.networkAudioActive
    readonly property string selectedHost: protocol.currentIndex === 0
                                        ? prefs.udpHost : prefs.tcpHost

    Text {
        Layout.fillWidth: true
        text: qsTr("PCM16 little-endian · 48 kHz · compatibile SDR++ Network Sink")
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spacingTight
        rowSpacing: Theme.spacingTight
        enabled: !root.active

        Text {
            text: qsTr("Trasporto")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        DsdrComboBox {
            id: protocol
            Layout.fillWidth: true
            model: [qsTr("UDP (destinazione)"), qsTr("TCP (server)")]
            currentIndex: prefs.protocolIndex
            onActivated: prefs.protocolIndex = currentIndex
        }

        Text {
            text: protocol.currentIndex === 0 ? qsTr("Destinazione") : qsTr("Bind TCP")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        TextField {
            id: hostField
            Layout.fillWidth: true
            text: root.selectedHost
            placeholderText: protocol.currentIndex === 0 ? "192.168.1.20" : "0.0.0.0"
            selectByMouse: true
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceSunken
                border.width: 1
                border.color: hostField.activeFocus ? Theme.accent : Theme.border
            }
            onEditingFinished: {
                if (protocol.currentIndex === 0)
                    prefs.udpHost = text.trim()
                else
                    prefs.tcpHost = text.trim()
            }
        }

        Text {
            text: qsTr("Porta")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        TextField {
            id: portField
            Layout.fillWidth: true
            text: String(prefs.port)
            selectByMouse: true
            validator: IntValidator { bottom: 1; top: 65535 }
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceSunken
                border.width: 1
                border.color: portField.activeFocus ? Theme.accent : Theme.border
            }
            onEditingFinished: prefs.port = Number(text)
        }

        Text {
            text: qsTr("Canali")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        DsdrButton {
            Layout.fillWidth: true
            text: prefs.stereo ? qsTr("Stereo") : qsTr("Mono")
            checkable: true
            checked: prefs.stereo
            onToggled: prefs.stereo = checked
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            Layout.fillWidth: true
            text: root.active ? qsTr("FERMA") : qsTr("AVVIA")
            danger: root.active
            onClicked: {
                if (root.active) {
                    Session.stopNetworkAudio()
                    return
                }
                if (protocol.currentIndex === 0)
                    prefs.udpHost = hostField.text.trim()
                else
                    prefs.tcpHost = hostField.text.trim()
                prefs.port = Number(portField.text)
                Session.startNetworkAudio(protocol.currentIndex === 0 ? "udp" : "tcp",
                                          root.selectedHost, prefs.port, prefs.stereo)
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.status.detail || qsTr("Fermo")
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSmall
            color: root.status.error ? Theme.danger
                  : root.active ? Theme.success : Theme.textDisabled
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.active
        text: qsTr("Inviati %1 frame · mancanti %2")
                  .arg(root.status.framesSent || 0).arg(root.status.framesDropped || 0)
        font.pixelSize: Theme.fontSmall
        font.family: Theme.monoFamily
        color: Theme.textDisabled
    }

    Text {
        Layout.fillWidth: true
        visible: protocol.currentIndex === 1 && !root.active
        text: qsTr("TCP ascolta solo indirizzi IP locali (0.0.0.0 per tutte le interfacce).")
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
    }
}
