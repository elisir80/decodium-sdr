// SPDX-License-Identifier: GPL-3.0-or-later
// Channel strip: un blocco di controlli per ogni canale RX.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    color: Theme.surface
    border.width: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Text {
                text: qsTr("Canali")
                font.pixelSize: Theme.fontLarge
                font.bold: true
                color: Theme.textPrimary
                Layout.fillWidth: true
            }

            Text {
                text: Session.channels.count + " / " + Session.capabilities.maxRxChannels
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textSecondary
            }

            DsdrButton {
                text: "+"
                implicitWidth: 32
                enabled: Session.connected
                         && Session.channels.count < Session.capabilities.maxRxChannels
                onClicked: Session.addChannel(Session.centerFrequency)
            }
        }

        // Controlli del backend attivo, se ne dichiara.
        BackendPanelHost {
            Layout.fillWidth: true
            visible: Session.connected && Session.capabilities.nativePanels.length > 0
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacing
            model: Session.channels

            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: entry

                required property int index
                required property color channelColor
                required property string label
                required property real frequencyHz
                required property int mode
                required property string modeName
                required property int filterLowHz
                required property int filterHighHz
                required property int agcMode
                required property real agcThresholdDb
                required property real volume
                required property bool muted
                required property real signalDb
                required property real agcGainDb

                readonly property bool current: Session.channels.currentIndex === index

                width: ListView.view.width
                height: layout.implicitHeight + 2 * Theme.spacing
                radius: Theme.radius
                color: current ? Theme.surfaceRaised : Theme.surface
                border.width: 1
                border.color: current ? entry.channelColor : Theme.border

                MouseArea {
                    anchors.fill: parent
                    onClicked: Session.channels.currentIndex = entry.index
                    z: -1
                }

                ColumnLayout {
                    id: layout
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacingTight

                    // ── Intestazione ─────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing

                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: entry.channelColor
                        }

                        Text {
                            text: entry.label
                            font.pixelSize: Theme.fontNormal
                            font.bold: true
                            color: Theme.textPrimary
                        }

                        Item { Layout.fillWidth: true }

                        DsdrButton {
                            text: entry.muted ? qsTr("Muto") : qsTr("Attivo")
                            implicitWidth: 66
                            checkable: true
                            checked: entry.muted
                            danger: entry.muted
                            onToggled: Session.setChannelMuted(entry.index, checked)
                        }

                        DsdrButton {
                            text: "×"
                            implicitWidth: 28
                            enabled: Session.channels.count > 1
                            onClicked: Session.removeChannel(entry.index)
                        }
                    }

                    // ── Frequenza ────────────────────────────────────────
                    Text {
                        text: (entry.frequencyHz / 1e6).toFixed(6) + " MHz"
                        font.pixelSize: Theme.fontLarge
                        font.family: Theme.monoFamily
                        color: entry.current ? Theme.accent : Theme.textPrimary
                        Layout.fillWidth: true

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeVerCursor
                            onWheel: (wheel) => {
                                const step = wheel.modifiers & Qt.ControlModifier ? 1000
                                           : wheel.modifiers & Qt.ShiftModifier ? 10 : 100
                                Session.nudgeChannel(entry.index,
                                                     wheel.angleDelta.y > 0 ? step : -step)
                            }
                        }
                    }

                    SignalMeter {
                        Layout.fillWidth: true
                        levelDb: entry.signalDb
                    }

                    // ── Modo e AGC ───────────────────────────────────────
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.spacing
                        rowSpacing: Theme.spacingTight

                        Text {
                            text: qsTr("Modo")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }

                        Text {
                            text: qsTr("AGC")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }

                        DsdrComboBox {
                            Layout.fillWidth: true
                            model: Session.modeNames()
                            currentIndex: entry.mode
                            // Solo se la demodulazione è del client il modo è
                            // davvero nostro; con DSP a bordo lo decide la radio.
                            enabled: Session.capabilities.clientDemod
                            onActivated: Session.setChannelMode(entry.index, currentIndex)
                        }

                        DsdrComboBox {
                            Layout.fillWidth: true
                            model: Session.agcModeNames()
                            currentIndex: entry.agcMode
                            enabled: Session.capabilities.clientAgc
                            onActivated: Session.setChannelAgcMode(entry.index, currentIndex)
                        }
                    }

                    // ── AGC-T ────────────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        visible: Session.capabilities.clientAgc && entry.agcMode !== 0

                        Text {
                            text: qsTr("AGC-T")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }

                        DsdrSlider {
                            Layout.fillWidth: true
                            from: -130; to: -30
                            value: entry.agcThresholdDb
                            onMoved: Session.setChannelAgcThreshold(entry.index, value)
                        }

                        Text {
                            text: Math.round(entry.agcThresholdDb) + " dB"
                            font.pixelSize: Theme.fontSmall
                            font.family: Theme.monoFamily
                            color: Theme.textSecondary
                            Layout.preferredWidth: 52
                        }
                    }

                    // ── Filtro ───────────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        visible: Session.capabilities.clientDemod

                        Text {
                            text: qsTr("Filtro")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }

                        DsdrSlider {
                            Layout.fillWidth: true
                            from: -6000; to: 6000; stepSize: 50
                            value: entry.filterLowHz
                            onMoved: Session.setChannelFilter(entry.index, value, entry.filterHighHz)
                        }

                        DsdrSlider {
                            Layout.fillWidth: true
                            from: -6000; to: 6000; stepSize: 50
                            value: entry.filterHighHz
                            onMoved: Session.setChannelFilter(entry.index, entry.filterLowHz, value)
                        }
                    }

                    Text {
                        text: entry.filterLowHz + " … " + entry.filterHighHz + " Hz    "
                              + qsTr("guadagno AGC ") + Math.round(entry.agcGainDb) + " dB"
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.monoFamily
                        color: Theme.textDisabled
                    }

                    // ── Volume ───────────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Vol")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }

                        DsdrSlider {
                            Layout.fillWidth: true
                            from: 0; to: 1
                            value: entry.volume
                            accentColor: entry.channelColor
                            onMoved: Session.setChannelVolume(entry.index, value)
                        }
                    }
                }
            }
        }
    }
}
