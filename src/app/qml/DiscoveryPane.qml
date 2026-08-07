// SPDX-License-Identifier: GPL-3.0-or-later
// Selezione del backend e dei device trovati.
//
// La pagina è identica per ogni backend: elenca ciò che la discovery emette e
// mostra le capability dichiarate. Nessun ramo sul tipo di radio.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLoose
        spacing: Theme.spacing

        Text {
            text: qsTr("Sorgente")
            font.pixelSize: Theme.fontLarge
            font.bold: true
            color: Theme.textPrimary
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            DsdrComboBox {
                id: backendSelector
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"
                model: Session.availableBackends
                onActivated: Session.selectBackend(currentValue)

                Component.onCompleted: {
                    for (let i = 0; i < model.length; ++i) {
                        if (model[i].id === Session.backendId) {
                            currentIndex = i
                            break
                        }
                    }
                }
            }

            DsdrButton {
                text: Session.discovering ? qsTr("Ricerca…") : qsTr("Cerca")
                enabled: !Session.discovering
                onClicked: Session.startDiscovery()
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true
            spacing: Theme.spacingTight
            model: Session.devices

            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: deviceEntry

                required property int index
                required property string displayName
                required property string model
                required property string serial
                required property string transport

                width: ListView.view.width
                height: 56
                radius: Theme.radiusSmall
                color: mouse.containsMouse ? Theme.surfaceRaised : Theme.surfaceSunken
                border.width: 1
                border.color: mouse.containsMouse ? Theme.accent : Theme.border

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onDoubleClicked: Session.connectToDevice(deviceEntry.index)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacing

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            text: deviceEntry.displayName
                            font.pixelSize: Theme.fontNormal
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: deviceEntry.transport + "  ·  " + deviceEntry.serial
                            font.pixelSize: Theme.fontSmall
                            font.family: Theme.monoFamily
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    DsdrButton {
                        text: qsTr("Connetti")
                        onClicked: Session.connectToDevice(deviceEntry.index)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: parent.count === 0 && !Session.discovering
                text: qsTr("Premi «Cerca» per elencare i device")
                font.pixelSize: Theme.fontNormal
                color: Theme.textDisabled
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: Session.discovering
                visible: running
            }
        }

        // ── Capability dichiarate dal backend (§4.2) ─────────────────────
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: capsFlow.implicitHeight + 2 * Theme.spacing
            radius: Theme.radiusSmall
            color: Theme.surfaceSunken
            border.width: 1
            border.color: Theme.border

            Flow {
                id: capsFlow
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacingTight

                Repeater {
                    model: [
                        { text: qsTr("%1 canali RX").arg(Session.capabilities.maxRxChannels), on: true },
                        { text: qsTr("RX coerenti"), on: Session.capabilities.coherentRx },
                        { text: qsTr("TX"), on: Session.capabilities.canTransmit },
                        { text: qsTr("DSP client"), on: Session.capabilities.clientDemod },
                        { text: qsTr("Spettro client"), on: Session.capabilities.clientSpectrum },
                        { text: qsTr("Remoto"), on: Session.capabilities.remoteCapable },
                        { text: qsTr("Registrazione"), on: Session.capabilities.supportsRecording },
                    ]

                    delegate: Rectangle {
                        required property var modelData

                        visible: modelData.on
                        width: capText.implicitWidth + 2 * Theme.spacing
                        height: 22
                        radius: 11
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                        border.width: 1
                        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)

                        Text {
                            id: capText
                            anchors.centerIn: parent
                            text: parent.modelData.text
                            font.pixelSize: Theme.fontSmall
                            color: Theme.accent
                        }
                    }
                }
            }
        }
    }
}
